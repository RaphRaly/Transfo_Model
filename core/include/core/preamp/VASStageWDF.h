#pragma once

// =============================================================================
// VASStageWDF — Voltage Amplifier Stage with Miller compensation for JE-990.
//
// Models Q6 (2N4250A PNP) in the JE-990 discrete op-amp. The VAS is the
// dominant-pole amplifier that sets the open-loop bandwidth and DC gain of
// the entire feedback loop.
//
// Circuit topology (ANALYSE_ET_DESIGN_Rev2.md section 3.3):
//
//                          +24V
//                            |
//                       R7 [160 Ohm] (collector load)
//                            |
//                            |C
//  From cascode --> Q6 Base (2N4250A PNP)
//                            |E
//                            |
//                       R8 [130 Ohm] (emitter resistor to -Vcc)
//                            |
//                          -24V
//
//  C1 [150pF] Miller compensation: Q6 collector --> Q6 base
//
// Operating point:
//   The VAS collector current is constrained by the cascode mirror feeding
//   the base from the differential pair. Typical quiescent Ic ~ 1.5 mA.
//   This sets a moderate gm ~ 58 mS, giving high voltage gain:
//     Av = -R_collector / (R_emitter + 1/gm)
//        = -160 / (130 + 17.2)
//        ~ -1.09
//   However, without emitter degeneration in the AC path (bypass cap), the
//   raw gain would be -gm * R_collector = -9.3. The degeneration from R8
//   linearizes the stage at the cost of reduced gain.
//
// Miller compensation:
//   The 150pF Miller cap from collector to base creates the dominant pole:
//     fc = 1 / (2*pi * R_collector * C_miller)
//        = 1 / (2*pi * 160 * 150e-12)
//        ~ 6.6 MHz
//   This is the dominant pole that ensures loop stability in the 990's
//   three-stage topology (diff pair -> cascode -> VAS -> output stage).
//   The Miller effect multiplies the effective capacitance by (1 + Av),
//   further lowering the pole frequency seen from the base.
//
// WDF Implementation Strategy:
//
//   Uses the same companion-source approach as CEStageWDF:
//     - BJTLeaf models the BE junction (NR-solved in wave domain)
//     - Collector current from BJTLeaf computes collector voltage drop:
//         PNP: Vc = -Vcc + |Ic| * R_collector
//     - Miller cap modeled as a first-order lowpass on collector output
//     - Emitter degeneration from R8 applied in processSample()
//     - Independent DC bias (AC-coupled between stages)
//
//   Q6 is PNP: sign = -1, V_bias is negative, Vc sits near -Vcc at quiescence.
//
// Design rules applied (from Sprint 3 feedback):
//   1. V_bias_base always added to baseDrive (auto-computed from I_quiescent)
//   2. Emitter degeneration applied in processSample() via R8 = 130 Ohm
//   3. 32-sample warmup after reset()
//   4. All filter states initialized to quiescent values
//   5. One scatter per reactive WDF element per sample
//   6. AC coupling — VAS has independent bias from cascode
//
// Template parameter: NonlinearLeaf (default BJTLeaf) — the CRTP WDOnePort
// element that models the BE junction via Newton-Raphson.
//
// Pattern: CRTP-composing WDF sub-circuit (header-only).
//
// Reference: Ebers-Moll 1954; ANALYSE_ET_DESIGN_Rev2.md section 3.3;
//            John Hardy JE-990 schematic; chowdsp_wdf DiodeT.h
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include "../wdf/AdaptedElements.h"
#include "../wdf/BJTLeaf.h"
#include <algorithm>
#include <cmath>

namespace transfo {

// ── Configuration ────────────────────────────────────────────────────────────

struct VASConfig
{
    BJTParams bjt;                      // Q6: 2N4250A PNP
    float R_collector   = 160.0f;       // R7 collector load [Ohm]
    float R_emitter     = 130.0f;       // R8 emitter resistor [Ohm]
    float C_miller      = 150e-12f;     // C1 Miller compensation [F]
    float Vcc           = 24.0f;        // Supply voltage magnitude [V] (+/- 24V)
    float I_quiescent   = 1.5e-3f;      // Quiescent collector current [A]

    bool isValid() const
    {
        return bjt.isValid()
            && R_collector > 0.0f
            && R_emitter >= 0.0f
            && C_miller >= 0.0f
            && Vcc > 0.0f
            && I_quiescent > 0.0f;
    }

    /// Default JE-990 Q6 VAS configuration.
    /// Q6 = 2N4250A PNP, R7 = 160 Ohm, R8 = 130 Ohm, C1 = 150pF.
    static VASConfig Q6_Default()
    {
        VASConfig cfg;
        cfg.bjt = BJTParams::N2N4250A();
        // All other values are already set to JE-990 defaults
        return cfg;
    }
};

// ── VAS Stage WDF ────────────────────────────────────────────────────────────

template <typename NonlinearLeaf = BJTLeaf>
class VASStageWDF
{
public:
    VASStageWDF() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the VAS stage from config and sample rate.
    ///
    /// Sets up the companion-source WDF sub-circuit:
    ///   - BJTLeaf for Q6 BE junction (PNP, NR-solved)
    ///   - Miller cap as first-order lowpass on collector
    ///   - DC bias computed for PNP forward-active at I_quiescent
    ///   - Emitter degeneration from R8 applied per sample
    void prepare(float sampleRate, const VASConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;
        Ts_         = 1.0f / sampleRate;

        // Polarity: +1 for NPN, -1 for PNP
        // Q6 is PNP: sign_ = -1
        sign_ = config.bjt.polaritySign();

        // ── Configure BJTLeaf ────────────────────────────────────────────────
        bjtLeaf_.configure(config.bjt);
        bjtLeaf_.prepare(sampleRate);

        // ── Miller capacitor filter ──────────────────────────────────────────
        // The Miller cap C1 from collector to base creates the dominant pole.
        // Modeled as a first-order lowpass on the collector output:
        //   fc = 1 / (2*pi * Rc * Cmiller)
        //      = 1 / (2*pi * 160 * 150e-12)
        //      ~ 6.63 MHz
        // Discrete: alpha = 1 / (1 + 2*pi*fc*Ts)
        //
        // At 44.1 kHz, this pole is far above Nyquist, so the filter has
        // negligible effect. At oversampled rates (4x = 176.4 kHz) the
        // pole still dominates the open-loop response, ensuring stability
        // when the VAS is placed inside the 990's feedback loop.
        hasMillerCap_ = (config.C_miller > 0.0f);
        if (hasMillerCap_)
        {
            const float fc = 1.0f / (kTwoPif * config.R_collector * config.C_miller);
            millerAlpha_ = 1.0f / (1.0f + kTwoPif * fc * Ts_);
        }

        // ── DC bias for PNP forward-active operation ─────────────────────────
        // The VAS quiescent current is set by the cascode mirror feeding the
        // base, not by R7/R8 (which would give an unreasonably high current).
        // We use the config I_quiescent to compute the required Vbe bias.
        //
        // For PNP: V_bias = -Vt * ln(Ic / Is + 1)
        //   sign_ = -1 for PNP, so:
        //   V_bias = sign_ * Vt * ln(Ic / Is + 1) = -Vt * ln(...)
        // This gives a negative bias voltage, forward-biasing the PNP BE junction.
        V_bias_base_ = sign_ * config.bjt.Vt
                      * std::log(config.I_quiescent / config.bjt.Is + 1.0f);

        // ── Quiescent collector voltage ──────────────────────────────────────
        // PNP: Vc = -Vcc + |Ic| * R_collector
        //   sign_ = -1: Vc = sign_ * Vcc - Ic * Rc
        //   With Ic < 0 for PNP: Vc = -Vcc - (-|Ic|) * Rc = -Vcc + |Ic| * Rc
        //
        // At I_quiescent = 1.5 mA, R_collector = 160 Ohm:
        //   Vc = -24 + 0.0015 * 160 = -24 + 0.24 = -23.76V
        //
        // The collector sits near -Vcc because the quiescent current is small
        // relative to what R7 could carry. This is correct for the VAS: the
        // collector voltage swing is the output signal.
        Vc_quiescent_ = sign_ * config.Vcc
                       - sign_ * config.I_quiescent * config.R_collector;

        // Initialize DC tracking to the quiescent operating point
        Vc_dc_   = Vc_quiescent_;
        Vc_last_ = Vc_quiescent_;

        reset();
    }

    /// Clear all reactive element states and NR warm-starts.
    ///
    /// After clearing, runs a 32-sample warmup with zero input so the BJT
    /// NR solver converges at the bias point. This eliminates the initial
    /// transient that would otherwise be amplified by the VAS's high gain
    /// and fed back through the Miller cap.
    void reset()
    {
        bjtLeaf_.reset();

        millerState_   = Vc_quiescent_;
        Vc_dc_         = Vc_quiescent_;
        Vc_last_       = Vc_quiescent_;
        outputVoltage_ = 0.0f;
        dcSettleCount_ = 0;

        // Warm up: run zero-input samples so the BJT NR solver converges
        // at the bias point. The 32-sample count matches CEStageWDF and
        // is sufficient for the companion model to reach steady-state Vbe.
        for (int i = 0; i < 32; ++i)
            processSample(0.0f);

        // Re-initialize DC tracker to the converged collector voltage.
        // After warmup, Vc_last_ reflects the true quiescent point
        // including any nonlinear shifts from the NR solution.
        Vc_dc_       = Vc_last_;
        millerState_ = Vc_last_;
        outputVoltage_ = 0.0f;
        dcSettleCount_ = 0;
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the VAS stage.
    ///
    /// Algorithm:
    ///   1. Add DC bias for PNP forward-active operation
    ///   2. Drive BJTLeaf: scatter input through BE junction (NR solve)
    ///   3. Read Ic from BJTLeaf companion model
    ///   4. Compute Vc = sign * Vcc - Ic * R_collector (PNP collector voltage)
    ///   5. Apply emitter degeneration from R8 = 130 Ohm
    ///   6. Apply Miller cap (first-order lowpass on collector output)
    ///   7. Output = AC component of collector voltage
    ///
    /// @param input  Voltage from cascode stage [V]
    /// @return       Miller-compensated collector voltage (AC component) [V]
    float processSample(float input)
    {
        // ── Step 1: Add DC bias for PNP forward-active operation ─────────────
        // The bias voltage forward-biases the BE junction at the target
        // quiescent current. For PNP, V_bias is negative (~-0.68V).
        // The cascode stage output is AC-coupled; the VAS has its own
        // independent bias (design rule #6).
        float baseDrive = input + V_bias_base_;

        // ── Step 2: Drive BJTLeaf (BE junction NR solve) ─────────────────────
        // The BJTLeaf solves the nonlinear BE junction in the wave domain.
        // For PNP, the polarity handling is internal to BJTLeaf/BJTCompanionModel.
        // One scatter per reactive element per sample (design rule #5).
        const float b_prev = bjtLeaf_.getReflectedWave();
        const float a_bjt = 2.0f * baseDrive - b_prev;
        bjtLeaf_.scatter(a_bjt);

        // ── Step 3: Read collector current from companion model ──────────────
        // Ic is the primary output of the BJT companion source.
        // For PNP in forward-active: Ic < 0 (current flows into collector).
        const float Ic = bjtLeaf_.getCollectorCurrent();

        // ── Step 4: Compute collector voltage ────────────────────────────────
        // PNP: Vc = -Vcc + |Ic| * R_collector
        // Using unified formula: Vc = sign * Vcc - Ic * Rc
        //   sign=-1, Ic<0 => Vc = -Vcc - (-|Ic|)*Rc = -Vcc + |Ic|*Rc
        //
        // For Q6 at quiescence (Ic = -1.5mA):
        //   Vc = -24 + 0.0015 * 160 = -23.76V
        // Signal swings modulate Vc around this point.
        float Vc = sign_ * config_.Vcc - Ic * config_.R_collector;

        // Clamp collector voltage to supply rail limits.
        // PNP: Vc nominally in [-Vcc, ~0], but we use symmetric clamp
        // to handle transients gracefully.
        Vc = std::clamp(Vc, -config_.Vcc, config_.Vcc);

        // ── Step 5: Emitter degeneration from R8 ─────────────────────────────
        // R8 = 130 Ohm in the emitter path reduces voltage gain:
        //   Av = -R_collector / (R_emitter + 1/gm)
        //      = -160 / (130 + 1/gm)
        //
        // At Ic = 1.5mA: gm = Ic/Vt = 0.058 S, 1/gm = 17.2 Ohm
        //   Av = -160 / (130 + 17.2) = -1.09
        //
        // The degeneration is applied as a scaling factor on the AC component
        // of Vc, leaving the DC operating point untouched.
        // Design rule #2: emitter degeneration must be in processSample().
        if (config_.R_emitter > 0.0f)
        {
            const float gm = bjtLeaf_.getGm();
            if (gm > kEpsilonF)
            {
                // degen = 1 / (1 + gm * Re) is the gain reduction factor.
                // Without degeneration: Av = -gm * Rc
                // With degeneration:    Av = -gm * Rc * degen = -Rc / (Re + 1/gm)
                const float degen = 1.0f / (1.0f + gm * config_.R_emitter);
                const float Vc_ac = Vc - Vc_dc_;
                Vc = Vc_dc_ + Vc_ac * degen;
            }
        }

        // ── Step 6: Miller capacitor (first-order lowpass) ───────────────────
        // The Miller cap C1 = 150pF creates the dominant pole at ~6.6 MHz.
        // Implemented as a simple one-pole IIR lowpass:
        //   y[n] = y[n-1] + alpha * (x[n] - y[n-1])
        //
        // At audio sample rates (44.1-192 kHz), the pole is well above
        // Nyquist, so the filter has minimal audible effect. Its purpose is
        // to model the open-loop bandwidth correctly for:
        //   (a) Oversampled operation where the pole approaches Nyquist
        //   (b) Correct transient response in the feedback loop
        //   (c) Accurate loop gain simulation for stability analysis
        if (hasMillerCap_)
        {
            millerState_ += millerAlpha_ * (Vc - millerState_);
            Vc = millerState_;
        }

        // ── Step 7: Output = AC component of collector voltage ───────────────
        // Subtract the DC operating point for a clean AC output signal.
        // The DC estimate tracks slowly to handle bias drift without
        // introducing low-frequency artifacts.
        if (dcSettleCount_ < kDCSettleSamples)
        {
            // During initial settling, track DC rapidly so the output
            // stabilizes quickly after reset/prepare.
            Vc_dc_ += 0.01f * (Vc - Vc_dc_);
            ++dcSettleCount_;
        }
        else
        {
            // Steady state: very slow DC tracking (sub-Hz)
            // alpha_dc ~ 0.0001 at 44.1kHz gives f_dc ~ 0.7 Hz.
            // This removes any residual DC drift from the NR solver
            // without affecting the lowest audio frequencies.
            constexpr float kDCTrackAlpha = 0.0001f;
            Vc_dc_ += kDCTrackAlpha * (Vc - Vc_dc_);
        }

        Vc_last_ = Vc;
        outputVoltage_ = Vc - Vc_dc_;

        return outputVoltage_;
    }

    /// Block-based processing for efficiency.
    /// Processes numSamples through the VAS, reading from in[] and
    /// writing to out[].
    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring / diagnostics ─────────────────────────────────────────────

    /// Collector-emitter voltage [V] at the current operating point.
    /// Vce = Vc - Ve, where Ve ~ sign * (Vbe + Ic * Re)
    /// For PNP: Vce is negative in forward-active (collector more negative
    /// than emitter).
    float getVce() const
    {
        const float Ve = bjtLeaf_.getVbe() + bjtLeaf_.getIc() * config_.R_emitter;
        return Vc_last_ - Ve;
    }

    /// Collector current [A] at the current operating point.
    /// For PNP: Ic < 0 (current flows into collector).
    float getIc() const
    {
        return bjtLeaf_.getIc();
    }

    /// Transconductance gm = |Ic| / Vt [S] at the current operating point.
    /// At I_quiescent = 1.5mA: gm ~ 58 mS.
    float getGm() const
    {
        return bjtLeaf_.getGm();
    }

    /// Instantaneous small-signal voltage gain at the current operating point.
    ///
    /// Av = -gm * R_collector / (1 + gm * R_emitter)
    ///    = -R_collector / (R_emitter + 1/gm)
    ///
    /// This is the gain with emitter degeneration from R8.
    /// At Ic = 1.5mA: Av = -160 / (130 + 17.2) = -1.09.
    /// The low gain is by design: the VAS in the 990 trades voltage gain
    /// for linearity via heavy emitter degeneration.
    float getGainInstantaneous() const
    {
        const float gm = bjtLeaf_.getGm();
        if (gm < kEpsilonF)
            return 0.0f;

        // With emitter degeneration:
        //   Av = -Rc / (Re + 1/gm) = -gm * Rc / (1 + gm * Re)
        return -config_.R_collector / (config_.R_emitter + 1.0f / gm);
    }

    /// Last computed output voltage (AC component of collector) [V].
    float getOutputVoltage() const
    {
        return outputVoltage_;
    }

    /// Last full collector voltage (before DC removal) [V].
    float getCollectorVoltage() const
    {
        return Vc_last_;
    }

    /// Estimated DC operating point of the collector [V].
    float getCollectorDC() const
    {
        return Vc_dc_;
    }

    /// Base-emitter voltage [V] from the BJTLeaf.
    /// For PNP: negative in forward-active (~-0.68V).
    float getVbe() const
    {
        return bjtLeaf_.getVbe();
    }

    /// NR iteration count from the last sample.
    int getLastIterCount() const
    {
        return bjtLeaf_.getLastIterCount();
    }

    /// Miller compensation pole frequency [Hz].
    /// fc = 1 / (2*pi * Rc * Cmiller)
    /// Returns 0 if no Miller cap is configured.
    float getMillerPoleFreq() const
    {
        if (!hasMillerCap_ || config_.C_miller <= 0.0f)
            return 0.0f;
        return 1.0f / (kTwoPif * config_.R_collector * config_.C_miller);
    }

    /// Access the underlying BJTLeaf for advanced diagnostics.
    const NonlinearLeaf& getBJTLeaf() const { return bjtLeaf_; }
    NonlinearLeaf& getBJTLeaf() { return bjtLeaf_; }

    /// Access the current configuration.
    const VASConfig& getConfig() const { return config_; }

private:
    // ── WDF elements ─────────────────────────────────────────────────────────
    NonlinearLeaf bjtLeaf_;             // BE junction (nonlinear, NR-solved)

    // ── Configuration ────────────────────────────────────────────────────────
    VASConfig config_;
    float sampleRate_ = 44100.0f;
    float Ts_         = 1.0f / 44100.0f;
    float sign_       = -1.0f;          // -1 for PNP (Q6 default)

    // ── Flags ────────────────────────────────────────────────────────────────
    bool hasMillerCap_ = false;

    // ── Miller cap filter state ──────────────────────────────────────────────
    float millerAlpha_ = 0.0f;          // Lowpass coefficient
    float millerState_ = 0.0f;          // y[n-1] (initialized to Vc_quiescent_)

    // ── DC bias ──────────────────────────────────────────────────────────────
    float V_bias_base_    = 0.0f;       // DC bias voltage for PNP forward-active
    float Vc_quiescent_   = 0.0f;       // Quiescent Vc (for init after reset)

    // ── Output state ─────────────────────────────────────────────────────────
    float Vc_dc_          = 0.0f;       // DC operating point estimate
    float Vc_last_        = 0.0f;       // Last full collector voltage
    float outputVoltage_  = 0.0f;       // Last AC output (Vc - Vc_dc)

    // ── DC settling ──────────────────────────────────────────────────────────
    static constexpr int kDCSettleSamples = 4096;  // ~93ms at 44.1kHz
    int dcSettleCount_ = 0;
};

} // namespace transfo
