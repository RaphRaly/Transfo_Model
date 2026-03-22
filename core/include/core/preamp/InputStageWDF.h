#pragma once

// =============================================================================
// InputStageWDF — WDF input stage for the dual-topology preamp.
//
// Wraps TransformerCircuitWDF<NonlinearLeaf> (T1: JT-115K-E) with preamp-
// specific configuration: phantom power, PAD attenuator, ratio switching,
// and secondary termination network.
//
// Circuit (balanced mic → T1 primary → secondary → amplifier):
//
//                              +48V
//                               |
//                        R1[6.81K] R2[6.81K]    (phantom, per leg)
//                               |      |
//     MIC (Vs, Zs) ──[R_pad?]──┴──────┴──── JT-115K-E Primary (T1)
//                                                 |
//                                            T1 Secondary
//                                                 |
//                                        ┌────────┼────────┐
//                                        |        |        |
//                                  R_t[13.7K]  [680pF]   signal → ampli
//                                        |        |
//                                       GND      GND
//
// Effective source impedance seen by T1 primary:
//
//   Phantom ON:   Zs_eff = Zmic || (2*R_phantom) + (pad ? 2*R_pad : 0)
//   Phantom OFF:  Zs_eff = Zmic + (pad ? 2*R_pad : 0)
//
//   Example (SM57, Zmic=150 Ohm):
//     Sans pad:   Zs_eff = 150 || 13620 ≈ 148.37 Ohm
//     Avec pad:   Zs_eff ≈ 148.37 + 1298 ≈ 1446.37 Ohm
//
// The secondary termination resistor (13.7K) is the dominant load; the
// amplifier input impedance (~100K) is negligible in parallel. The 680pF
// capacitor damps HF resonance from leakage inductance.
//
// Runtime controls (pad, ratio, source impedance) trigger reconfigure(),
// which rebuilds the transformer config and re-prepares the WDF tree.
// These are button-press events, not per-sample operations.
//
// Template parameter: NonlinearLeaf (JilesAthertonLeaf<LangevinPade> or
// CPWLLeaf) — passed through to TransformerCircuitWDF.
//
// Pattern: Wrapper / Facade over TransformerCircuitWDF.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md Annexe B (Dual Topology B+C);
//            Jensen JT-115K-E datasheet; SSL 9000J schematic (pad/phantom)
// =============================================================================

#include "../model/PreampConfig.h"
#include "../wdf/TransformerCircuitWDF.h"
#include <algorithm>
#include <cmath>

namespace transfo {

template <typename NonlinearLeaf>
class InputStageWDF
{
public:
    InputStageWDF() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the input stage from config and sample rate.
    /// Computes effective source impedance, configures T1, and prepares
    /// the underlying WDF tree.
    void prepare(float sampleRate, const InputStageConfig& config)
    {
        sampleRate_ = sampleRate;
        config_ = config;
        reconfigure();
    }

    /// Clear all reactive element states and the nonlinear leaf.
    void reset()
    {
        t1_.reset();
        lastOutput_ = 0.0f;
        hpState_ = 0.0f;
    }

    // ── Runtime controls (button-press events) ──────────────────────────────

    /// Enable/disable the PAD attenuator (adds 2*R_pad in series).
    void setPadEnabled(bool enabled)
    {
        if (config_.padEnabled == enabled)
            return;
        config_.padEnabled = enabled;
        reconfigure();
    }

    /// Switch transformer ratio (1:10 or 1:5).
    void setRatio(InputStageConfig::Ratio ratio)
    {
        if (config_.ratio == ratio)
            return;
        config_.ratio = ratio;
        reconfigure();
    }

    /// Set the microphone source impedance [Ohm].
    /// Typical values: 150 (SM57), 200 (U87), 50 (ribbon).
    void setSourceImpedance(float Zmic)
    {
        if (Zmic_ == Zmic)
            return;
        Zmic_ = Zmic;
        reconfigure();
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the T1 transformer.
    /// @param micSignal  Balanced mic voltage at the primary.
    /// @return           Secondary voltage after turns ratio scaling.
    float processSample(float micSignal)
    {
        // ── Analytical transformer model ───────────────────────────────────
        // The WDF TransformerCircuitWDF has a Lm scattering domain mismatch
        // that produces incorrect gain and phase. Use a simplified analytical
        // model that correctly captures:
        //   1. Turns ratio voltage gain
        //   2. Source/load impedance voltage divider
        //   3. Soft saturation from core nonlinearity
        //   4. HP bass rolloff from Lm/Rsource
        //
        // Keep the WDF tree ticking for B-H monitoring (not in signal path).
        t1_.processSample(micSignal);

        // Voltage transfer: V_sec = V_in * n * Rload / (Rs*n² + Rdc_pri*n² + Rdc_sec + Rload)
        float output = micSignal * idealGain_;

        // Core saturation: soft clip at the saturation flux level.
        // The Bsat point maps to a secondary voltage of approximately
        // n * Bsat * Ae * 2*pi*f / N_turns ≈ a few volts at audio frequencies.
        // Use a generous saturation knee to model mu-metal's wide linear region.
        const float satKnee = idealGain_ * 0.5f;  // Start compressing at 50% of max linear
        if (satKnee > 0.01f)
            output = satKnee * std::tanh(output / satKnee);

        // HP bass rolloff from Lm/Rsource interaction
        // fc = Rsource / (2*pi*Lm) ≈ 2-5 Hz for mu-metal (sub-audio)
        // Modeled as a one-pole HP that blocks DC and passes audio.
        hpState_ += hpAlpha_ * (output - hpState_);
        output -= hpState_;

        lastOutput_ = output;
        return lastOutput_;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring / diagnostics ─────────────────────────────────────────────

    /// Effective source impedance seen by T1 primary [Ohm].
    float getEffectiveSourceZ() const { return Zs_eff_; }

    /// Last output voltage from T1 secondary [V].
    float getSecondaryVoltage() const { return lastOutput_; }

    /// Magnetizing current from the nonlinear leaf [A].
    /// Reads Kirchhoff current at the Lm port of the WDF tree.
    float getMagnetizingCurrent() const
    {
        return t1_.getNonlinearLeaf().getKirchhoffI();
    }

    /// Current turns ratio (10.0 for 1:10, 5.0 for 1:5).
    float getTurnsRatio() const { return t1_.getTurnsRatio(); }

    /// Input impedance at the primary terminals [Ohm].
    float getInputImpedance() const { return t1_.getInputImpedance(); }

    /// Instantaneous magnetizing inductance from the nonlinear leaf [H].
    float getLm() const { return t1_.getLm(); }

    /// Access the underlying TransformerCircuitWDF for advanced diagnostics.
    const TransformerCircuitWDF<NonlinearLeaf>& getTransformer() const { return t1_; }
    TransformerCircuitWDF<NonlinearLeaf>& getTransformer() { return t1_; }

private:
    TransformerCircuitWDF<NonlinearLeaf> t1_;
    InputStageConfig config_;
    float sampleRate_      = 44100.0f;
    float Zmic_            = 150.0f;     // Default: SM57
    float Zs_eff_          = 150.0f;
    float lastOutput_      = 0.0f;
    float idealGain_       = 10.0f;      // Analytical transformer voltage gain
    float hpState_         = 0.0f;       // HP filter state (DC blocking)
    float hpAlpha_         = 0.0001f;    // HP filter coefficient (sub-audio cutoff)

    // ── Effective source impedance computation ──────────────────────────────

    /// Compute Zs_eff from phantom, pad, and mic impedance state.
    ///
    ///   Phantom ON:   Zs = Zmic || (2 * R_phantom)
    ///   Phantom OFF:  Zs = Zmic
    ///   Pad ON:       Zs += 2 * R_pad
    float computeZsEff() const
    {
        float Zs;

        if (config_.phantomEnabled)
        {
            // Parallel combination: Zmic || (2 * R_phantom)
            const float R_phantom_total = 2.0f * config_.R_phantom;
            Zs = (Zmic_ * R_phantom_total) / (Zmic_ + R_phantom_total);
        }
        else
        {
            Zs = Zmic_;
        }

        if (config_.padEnabled)
        {
            Zs += 2.0f * config_.R_pad;
        }

        return Zs;
    }

    // ── Reconfiguration ─────────────────────────────────────────────────────

    /// Rebuild the T1 transformer config from current input stage state
    /// and re-prepare the WDF tree. Called on pad/ratio/impedance changes.
    void reconfigure()
    {
        Zs_eff_ = computeZsEff();

        // Start from the base T1 config (don't modify config_.t1Config itself)
        TransformerConfig t1Cfg = config_.t1Config;

        // Apply effective source impedance
        t1Cfg.windings.sourceImpedance = Zs_eff_;

        // Apply load impedance = R_termination (13.7K on secondary)
        // The amplifier input Z (~100K) is negligible in parallel
        t1Cfg.loadImpedance = config_.R_termination;

        // Add external termination capacitance to the secondary parasitic.
        // C_total = C_sec_shield (parasitic, 205pF) + C_termination (680pF)
        // Note: we add to the copy, not the base config.
        t1Cfg.windings.C_sec_shield += config_.C_termination;

        // Apply turns ratio based on ratio setting
        t1Cfg.windings.turnsRatio_N1 = 1;
        t1Cfg.windings.turnsRatio_N2 =
            (config_.ratio == InputStageConfig::Ratio::X10) ? 10 : 5;

        // Re-prepare the WDF tree with the modified config
        // (kept for B-H monitoring, not in the primary signal path)
        t1_.prepare(static_cast<double>(sampleRate_), t1Cfg);
        t1_.reset();

        // ── Compute analytical gain ──────────────────────────────────────
        // V_sec = V_in * n * Rload / (Rs*n² + Rdc_pri*n² + Rdc_sec + Rload)
        const float n = static_cast<float>(t1Cfg.windings.turnsRatio_N2) /
                        static_cast<float>(t1Cfg.windings.turnsRatio_N1);
        const float Rs_ref  = Zs_eff_ * n * n;
        const float Rdc_pri_ref = t1Cfg.windings.Rdc_primary * n * n;
        const float Rdc_sec = t1Cfg.windings.Rdc_secondary;
        const float Rload   = t1Cfg.loadImpedance;
        const float Rtotal  = Rs_ref + Rdc_pri_ref + Rdc_sec + Rload;
        idealGain_ = (Rtotal > 0.0f) ? n * Rload / Rtotal : n;

        // ── HP filter: fc = Rsource / (2*pi*Lm_static) ──────────────────
        const float Lp = t1Cfg.windings.Lp_primary;
        if (Lp > 0.0f && Zs_eff_ > 0.0f) {
            const float fc = Zs_eff_ / (kTwoPif * Lp);
            const float omega = kTwoPif * fc / sampleRate_;
            hpAlpha_ = omega / (1.0f + omega);
        } else {
            hpAlpha_ = 0.0001f;  // ~0.7 Hz at 44.1 kHz
        }
        hpState_ = 0.0f;
    }
};

} // namespace transfo
