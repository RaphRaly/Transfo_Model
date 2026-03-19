#pragma once

// =============================================================================
// EFStageWDF — WDF Emitter Follower (Common Collector) stage for Q3 (BD139).
//
// Models the output buffer stage in the Neve Heritage Class-A path (Chemin A).
// The emitter follower provides:
//   - Unity voltage gain (Av ~ 1)
//   - High current gain (Ai = beta ~ 40 for BD139)
//   - Low output impedance: Zout ~ 1/gm + R_series ~ 11 Ohm
//   - No signal inversion (unlike CE stages)
//
// Circuit (ANALYSE_ET_DESIGN_Rev2.md section 3.2):
//
//          +24V --- Q3 Collector (fixed, no collector resistor for EF)
//                    |
//               Q3 (BD139 NPN)
//                    |B <-- Input from Q2 collector (DC coupled)
//                    |E
//                    |
//           +--------+
//           |        |
//      R_bias      C_out (220uF + 4.7uF film in parallel)
//      [390R]        |
//           |    [R_series = 10R] --> Output to T2
//         -24V
//
// WDF implementation:
//   The WDF port is the BE junction, solved by BJTLeaf (NR companion model).
//   The emitter voltage follows the base: Ve = Vbase - Vbe.
//   Output is AC-coupled through C_out (electrolytic + film in parallel)
//   with a series output resistor before the T2 transformer primary.
//
//   Emitter network (bias resistor + coupling path) is modeled as a
//   parallel junction of:
//     Port 0: R_bias to -Vcc (sets quiescent current)
//     Port 1: C_out + R_series (AC output path)
//
//   The BJTLeaf handles the nonlinear BE junction scattering.
//   Collector current Ic ~ Ie is available for monitoring.
//
// Template parameter: NonlinearLeaf (default BJTLeaf) — the WDF one-port
// element solving the BE junction. Allows swapping in a more detailed
// BJT model without changing the stage topology.
//
// Pattern: WDF sub-circuit with companion source (same approach as CE stages).
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md section 3.2;
//            BD139 datasheet (ST); Fettweis 1986 (WDF theory)
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include "../wdf/AdaptedElements.h"
#include "../wdf/BJTLeaf.h"
#include "../wdf/DynamicParallelAdaptor.h"
#include "../wdf/WDFSeriesAdaptor.h"
#include <algorithm>
#include <cmath>

namespace transfo {

// ── Configuration ────────────────────────────────────────────────────────────

struct EFStageConfig
{
    BJTParams bjt;
    float R_bias         = 390.0f;      // Emitter bias resistor to -Vcc [Ohm]
    float C_out          = 220e-6f;     // Output coupling capacitor [F]
    float C_out_film     = 4.7e-6f;     // Film cap in parallel with C_out [F]
    float R_series_out   = 10.0f;       // Series output resistor [Ohm]
    float Vcc            = 24.0f;       // Supply voltage magnitude [V] (+/- 24V)

    bool isValid() const
    {
        return bjt.isValid()
            && R_bias > 0.0f
            && C_out > 0.0f
            && C_out_film >= 0.0f
            && R_series_out >= 0.0f
            && Vcc > 0.0f;
    }

    /// Default Neve Heritage Q3 configuration.
    static EFStageConfig BD139_Default()
    {
        EFStageConfig cfg;
        cfg.bjt = BJTParams::BD139();
        // All other values are already set to Neve Heritage defaults
        return cfg;
    }
};

// ── EF Stage WDF ─────────────────────────────────────────────────────────────

template <typename NonlinearLeaf = BJTLeaf>
class EFStageWDF
{
public:
    EFStageWDF() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the emitter follower stage.
    ///
    /// Sets up the WDF tree:
    ///   - BJTLeaf for the BE junction (nonlinear one-port)
    ///   - R_bias: adapted resistor (390 Ohm to -Vcc, sets quiescent Ic)
    ///   - C_out: adapted capacitor (C_out + C_out_film in parallel)
    ///   - R_series: adapted resistor (10 Ohm output series resistance)
    ///   - Series adaptor: C_out in series with R_series (output coupling path)
    ///   - Parallel adaptor: R_bias || (C_out + R_series) at the emitter node
    void prepare(float sampleRate, const EFStageConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;
        Ts_         = 1.0f / sampleRate;

        // ── Configure BJT leaf ──────────────────────────────────────────────
        bjtLeaf_.configure(config_.bjt);
        bjtLeaf_.prepare(sampleRate);

        // ── Output coupling capacitor: C_eff = C_out + C_out_film ───────────
        // Electrolytics in parallel effectively sum their capacitances.
        const float C_eff = config_.C_out + config_.C_out_film;
        cOut_.setCapacitance(C_eff, Ts_);

        // ── Series output resistor ──────────────────────────────────────────
        rSeriesOut_.setResistance(config_.R_series_out);

        // ── Bias resistor (390 Ohm to -Vcc) ────────────────────────────────
        rBias_.setResistance(config_.R_bias);

        // ── Series adaptor: C_out -- R_series (output coupling path) ────────
        // Port 0 = C_out, Port 1 = R_series_out
        seriesOut_.setPortImpedances(
            cOut_.getPortResistance(),
            rSeriesOut_.getPortResistance()
        );

        // ── Parallel adaptor: R_bias || (C_out + R_series) at emitter node ─
        // Port 0 = R_bias
        // Port 1 = series adaptor (C_out + R_series), with adapted Z = Zc + Zr
        parallelEmitter_.setPortImpedance(0, rBias_.getPortResistance());
        parallelEmitter_.setPortImpedance(1, seriesOut_.getAdaptedImpedance());
        parallelEmitter_.recalculateScattering();

        // ── DC bias for forward-active operation ───────────────────────────
        // EF emitter current set by R_bias to -Vcc.
        // Target: Ie ≈ Vcc / R_bias (emitter near ground at quiescence).
        // V_bias = Vbe at this target current → BJT forward-biased.
        Vcc_ = config_.Vcc;
        const float Ie_target = config_.Vcc / config_.R_bias;
        V_bias_base_ = config_.bjt.Vt
                      * std::log(Ie_target / config_.bjt.Is + 1.0f);

        // ── DC tracking filter for AC coupling ───────────────────────────────
        // Time constant: tau = C_eff * R_bias (dominant path)
        const float tau = C_eff * config_.R_bias;
        dcAlpha_ = Ts_ / (Ts_ + tau);

        reset();
    }

    /// Clear all reactive element states and the nonlinear leaf.
    void reset()
    {
        bjtLeaf_.reset();
        cOut_.reset();

        emitterVoltage_ = 0.0f;
        lastOutput_     = 0.0f;
        dcState_        = 0.0f;
        lastVce_        = Vcc_;

        // Warm up: settle the BJT at the bias point
        for (int i = 0; i < 32; ++i)
            processSample(0.0f);

        // Re-initialize DC tracker to the converged emitter voltage
        dcState_    = emitterVoltage_;
        lastOutput_ = 0.0f;
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the emitter follower.
    ///
    /// Algorithm:
    ///   1. Input is the base voltage from Q2 collector (DC coupled).
    ///   2. Drive BJTLeaf with the input signal to solve for Vbe.
    ///   3. Compute emitter voltage: Ve = Vbase - Vbe.
    ///   4. Scatter through the emitter network (parallel junction of
    ///      R_bias and the series output path C_out + R_series).
    ///   5. Extract the AC-coupled output from the series adaptor.
    ///
    /// The emitter follower's key property is that the emitter "follows"
    /// the base with a nearly constant Vbe offset (~0.65V for BD139).
    float processSample(float input)
    {
        // ── Companion-source approach for emitter follower ──────────────────
        //
        // The EF is fundamentally simple:
        //   1. Drive BJTLeaf with the base voltage (input)
        //   2. Ve = Vbase - Vbe (emitter follows base minus one diode drop)
        //   3. AC-couple the emitter voltage through C_out
        //   4. Add series resistance R_series_out
        //
        // This is simpler than a full WDF tree and more numerically stable,
        // since the EF gain is ~1 and doesn't require adaptor scattering.

        // ── Step 1: Add DC bias and drive BJTLeaf ─────────────────────────
        // V_bias forward-biases the BE junction at the target emitter current.
        const float baseDrive = input + V_bias_base_;
        const float b_prev = bjtLeaf_.getReflectedWave();
        const float a_bjt = 2.0f * baseDrive - b_prev;
        bjtLeaf_.scatter(a_bjt);

        // ── Step 2: Compute emitter voltage ─────────────────────────────────
        const float Vbe = bjtLeaf_.getVbe();
        emitterVoltage_ = baseDrive - Vbe;

        // Clamp emitter to supply rails: Ve in [-Vcc, +Vcc]
        emitterVoltage_ = std::clamp(emitterVoltage_, -Vcc_, Vcc_);

        // ── Step 3: Collector-emitter voltage ───────────────────────────────
        lastVce_ = Vcc_ - emitterVoltage_;

        // ── Step 4: AC-coupling through C_out ───────────────────────────────
        // C_eff = C_out + C_out_film is a large electrolytic (224.7µF).
        // Model as a one-pole HP filter: blocks DC, passes audio.
        //   dcState_ tracks the DC component of Ve
        //   AC output = Ve - dcState_
        //
        // Time constant: tau = C_eff * R_load (R_load ~ R_bias || downstream)
        // For R_bias=390 and typical downstream load, tau ~ 0.1s.
        // alpha = Ts / (Ts + tau)
        dcState_ += dcAlpha_ * (emitterVoltage_ - dcState_);
        const float Ve_ac = emitterVoltage_ - dcState_;

        // ── Step 5: Series output resistance ────────────────────────────────
        // The 10 Ohm resistor attenuates slightly and adds output impedance.
        // In a real circuit, it forms a voltage divider with the load.
        // For simplicity, pass through (load >> 10 Ohm).
        lastOutput_ = Ve_ac;
        return lastOutput_;
    }

    // ── Monitoring ───────────────────────────────────────────────────────────

    /// Collector-emitter voltage [V].
    /// For EF: Vce = Vcc - Ve (collector tied to +Vcc).
    float getVce() const { return lastVce_; }

    /// Collector current [A] (approximately equal to emitter current).
    /// Ic = beta * Ib; Ie = (1 + beta) * Ib ~ Ic for beta >> 1.
    float getIc() const { return bjtLeaf_.getIc(); }

    /// Output impedance [Ohm].
    /// Zout ~ 1/gm + Re_parasitic + R_series ~ Vt/Ic + 0.3 + 10 ~ 11 Ohm.
    /// This is the key advantage of the emitter follower: very low Zout
    /// for driving the T2 transformer primary.
    float getOutputImpedance() const
    {
        const float gm = bjtLeaf_.getGm();
        const float r_intrinsic = (gm > kEpsilonF) ? (1.0f / gm) : 1e6f;
        return r_intrinsic + config_.bjt.Re + config_.R_series_out;
    }

    /// Emitter voltage [V] (DC + AC).
    /// Ve = Vbase - Vbe ~ Vbase - 0.65V.
    float getEmitterVoltage() const { return emitterVoltage_; }

    /// Base-emitter voltage [V] from the BJT leaf.
    float getVbe() const { return bjtLeaf_.getVbe(); }

    /// Transconductance gm = Ic / Vt [S] at the current operating point.
    float getGm() const { return bjtLeaf_.getGm(); }

    /// Quiescent collector current estimate [A].
    /// Ic_q ~ (2 * Vcc - Vbe) / R_bias for the full +/- Vcc swing.
    float getQuiescentIc() const
    {
        const float Vbe = std::abs(bjtLeaf_.getVbe());
        const float Vbe_est = (Vbe > 0.1f) ? Vbe : 0.65f;
        return (2.0f * Vcc_ - Vbe_est) / config_.R_bias;
    }

    /// NR iteration count from the last BJT scatter.
    int getLastIterCount() const { return bjtLeaf_.getLastIterCount(); }

    /// Last output sample value [V].
    float getLastOutput() const { return lastOutput_; }

    /// Access the underlying BJT leaf for advanced diagnostics.
    const NonlinearLeaf& getBJTLeaf() const { return bjtLeaf_; }
    NonlinearLeaf& getBJTLeaf() { return bjtLeaf_; }

    /// Access the stage configuration.
    const EFStageConfig& getConfig() const { return config_; }

private:
    // ── WDF Elements ─────────────────────────────────────────────────────────
    NonlinearLeaf            bjtLeaf_;         // BE junction nonlinear one-port
    AdaptedResistor          rBias_;           // Emitter bias resistor to -Vcc
    AdaptedCapacitor         cOut_;            // Output coupling cap (electrolytic + film)
    AdaptedResistor          rSeriesOut_;      // Series output resistor (10 Ohm)

    // ── WDF Adaptors ─────────────────────────────────────────────────────────
    WDFSeriesAdaptor         seriesOut_;       // C_out in series with R_series
    DynamicParallelAdaptor<2> parallelEmitter_; // R_bias || (C_out + R_series)

    // ── Configuration & State ────────────────────────────────────────────────
    EFStageConfig config_;
    float sampleRate_     = 44100.0f;
    float Ts_             = 1.0f / 44100.0f;
    float Vcc_            = 24.0f;

    // ── DC bias ─────────────────────────────────────────────────────────────
    float V_bias_base_    = 0.0f;   // DC bias voltage for forward-active

    // ── DC coupling filter state ─────────────────────────────────────────────
    float dcState_        = 0.0f;   // DC tracking for AC-coupling output
    float dcAlpha_        = 0.0f;   // LP coefficient for DC tracking

    // ── Output state ─────────────────────────────────────────────────────────
    float emitterVoltage_ = 0.0f;
    float lastOutput_     = 0.0f;
    float lastVce_        = 24.0f;
};

} // namespace transfo
