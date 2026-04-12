#pragma once

// [ARCHIVED] Part of JE990Path — on hold pending Sprint 4 BJT tuning.
// =============================================================================
// CascodeStage — Linearized cascode stage for the JE-990 discrete op-amp.
//
// Models the PNP cascode transistors Q3/Q5 (2N4250A) that sit between the
// differential pair collectors and the collector load resistors R4/R5 (300 Ohm).
// Also performs differential-to-single-ended conversion via the active
// current mirror on the Q5 side.
//
// Circuit topology (JE-990):
//
//                          +Vcc (+24V)
//                            |           |
//                         R4[300]     R5[300]
//                            |           |
//                         Q3 (C)      Q5 (C)
//           CR3 bias ---> Q3 (B)      Q5 (B) <--- CR3 bias
//                         Q3 (E)      Q5 (E)
//                            |           |
//                       Q1 collector  Q2 collector
//                       (from DiffPair)
//
//   Active mirror on Q5 side reflects current; Q3 side takes the sum.
//   Single-ended output from Q3 collector feeds the VAS input.
//
// Key simplification (linearized model):
//
//   The cascode operates at a quasi-fixed bias point set by CR3. It does NOT
//   add voltage gain — it is a current buffer (alpha ~ 0.98) that:
//     1. Shields the diff pair from Miller effect (high output impedance)
//     2. Converts differential current (Ic1 - Ic2) to single-ended voltage
//
//   Because the cascode is linear in normal operation (no clipping, no
//   saturation), there is no need for a BJTLeaf or NR solver here. The
//   stage is modeled as a simple linear transfer:
//
//       Vout = diffInput * currentGain
//
//   Output impedance: Rout ~ beta * rce ~ 250 * 60k = 15 MOhm, but the
//   effective load is R4 = R5 = 300 Ohm (dominates).
//
// Pattern: Linear sub-circuit (header-only, no template needed).
//
// Reference: JE-990 schematic; ANALYSE_ET_DESIGN_Rev2.md;
//            Jensen AN-004 (cascode biasing)
// =============================================================================

#include <algorithm>
#include <cmath>

namespace transfo {

// ── Configuration ────────────────────────────────────────────────────────────

struct CascodeConfig
{
    float R_load       = 300.0f;   // R4, R5 collector loads [Ohm]
    float currentGain  = 0.98f;    // Alpha (Ic/Ie ~ 1 for cascode)
    float Vcc          = 24.0f;    // Supply voltage [V]

    bool isValid() const
    {
        return R_load > 0.0f
            && currentGain > 0.0f
            && currentGain <= 1.0f
            && Vcc > 0.0f;
    }
};

// ── Cascode Stage ────────────────────────────────────────────────────────────

class CascodeStage
{
public:
    CascodeStage() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the cascode stage from config and sample rate.
    /// The sample rate is stored for potential future use (e.g., bandwidth
    /// limiting), but the linearized model is sample-rate independent.
    void prepare(float sampleRate, const CascodeConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;

        // Output impedance seen at the Q3 collector node:
        // The intrinsic cascode output impedance is very high (~15 MOhm),
        // but R_load dominates in parallel. For monitoring purposes, we
        // report R_load as the effective output impedance.
        outputImpedance_ = config.R_load;

        reset();
    }

    /// Clear internal state.
    void reset()
    {
        outputVoltage_ = 0.0f;
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the linearized cascode stage.
    ///
    /// Algorithm:
    ///   1. Receive differential voltage from DiffPairWDF
    ///   2. Apply current gain (alpha ~ 0.98) — the cascode transfers
    ///      nearly all of the diff pair signal current
    ///   3. The active current mirror performs diff-to-single-ended
    ///      conversion, which is implicit in the linear gain
    ///   4. Output single-ended voltage for the VAS
    ///
    /// The cascode does not add harmonic content in normal operation.
    /// It is a current buffer that provides high output impedance and
    /// shields the diff pair from Miller effect.
    ///
    /// @param diffInput  Differential voltage from DiffPairWDF [V]
    /// @return           Single-ended voltage for VAS input [V]
    float processSample(float diffInput)
    {
        // Linear transfer: Vout = -diffInput * alpha
        // The cascode is unity-gain for current; the voltage at the
        // cascode collector is INVERTED relative to the input current:
        //   increased Ic from diff pair → larger drop across R4/R5 →
        //   LOWER collector voltage. This physical inversion, combined
        //   with the VAS CE inversion, gives a net non-inverting forward
        //   path required for stable negative feedback in the JE-990.
        outputVoltage_ = -diffInput * config_.currentGain;

        return outputVoltage_;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring / diagnostics ─────────────────────────────────────────────

    /// Last computed output voltage [V] (single-ended, for VAS input).
    float getOutputVoltage() const
    {
        return outputVoltage_;
    }

    /// Effective output impedance [Ohm] at the cascode collector node.
    /// In practice this is R_load (300 Ohm), since the intrinsic cascode
    /// impedance (~15 MOhm) is in parallel and thus negligible.
    float getOutputImpedance() const
    {
        return outputImpedance_;
    }

    /// Signed local small-signal gain: −currentGain (inverting).
    /// Physical inversion from current→voltage through R4/R5.
    float getLocalGain() const { return -config_.currentGain; }

    /// Access the current configuration.
    const CascodeConfig& getConfig() const { return config_; }

private:
    // ── Configuration ────────────────────────────────────────────────────────
    CascodeConfig config_;
    float sampleRate_       = 44100.0f;

    // ── Output state ─────────────────────────────────────────────────────────
    float outputVoltage_    = 0.0f;       // Last output [V]
    float outputImpedance_  = 300.0f;     // Effective Zout [Ohm]
};

} // namespace transfo
