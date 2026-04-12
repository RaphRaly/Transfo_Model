#pragma once

// [ARCHIVED] Part of JE990Path — on hold pending Sprint 4 BJT tuning.
// =============================================================================
// VASStageWDF — Voltage Amplifier Stage with Miller compensation for JE-990.
//
// Models Q6 (2N4250A PNP) in the JE-990 discrete op-amp.
//
// CORRECTED TOPOLOGY (verified against je990-2.gif schematic 2026-03-26):
//
//                          +24V
//                            |
//                       R7 [160 Ohm] — Q6 EMITTER resistor
//                            |E
//  From cascode --> Q6 Base (2N4250A PNP)
//                            |C
//                            |
//                   Active current mirror output (~60 kOhm AC impedance)
//                            |
//                   To output stage bases + C1 Miller
//
//   C1 [150pF] Miller compensation: Q6 collector --> diff pair input
//
//   R8 [130 Ohm] is base stabilisation, NOT in the emitter signal path.
//
// KEY CORRECTION from prior code:
//   OLD (wrong): R_collector=160 (R7), R_emitter=130 (R8) → Av = 1.09
//   NEW (correct): R_emitter=160 (R7), R_coll_AC=60000 (mirror) → Av ≈ 338
//   The backboneGain_=500 hack in JE990Path compensated the old error.
//
// AC gain (degenerated):
//   Av = R_coll_AC / (R_emitter + 1/gm)
//      = 60000 / (160 + 17.2)
//      ≈ 338
//
// Miller dominant pole:
//   fc = 1 / (2π × R_coll_AC × C_miller)
//      = 1 / (2π × 60000 × 150e-12)
//      ≈ 17.7 kHz
//
// DC model:
//   With an active current mirror load, the collector DC voltage is set by
//   the mirror bias, NOT by Ic × R_coll. We model only the AC behaviour:
//     output = -(Ic - Ic_q) × R_coll_AC × degenFactor
//   The DC is removed by subtracting the frozen quiescent offset.
//
// Reference: Ebers-Moll 1954; Jensen JE-990 schematic (je990-2.gif);
//            docs/ANALYSIS_JE990_KVL_KCL_2026-03-26.md §4
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
    BJTParams bjt;                          // Q6: 2N4250A PNP
    float R_coll_AC     = 60000.0f;        // Active mirror AC output impedance [Ohm]
    float R_emitter     = 160.0f;          // R7 emitter resistor [Ohm]
    float C_miller      = 150e-12f;        // C1 Miller compensation [F]
    float Vcc           = 24.0f;           // Supply voltage magnitude [V]
    float I_quiescent   = 1.5e-3f;         // Quiescent collector current [A]

    bool isValid() const
    {
        return bjt.isValid()
            && R_coll_AC > 0.0f
            && R_emitter >= 0.0f
            && C_miller >= 0.0f
            && Vcc > 0.0f
            && I_quiescent > 0.0f;
    }

    static VASConfig Q6_Default()
    {
        VASConfig cfg;
        cfg.bjt = BJTParams::N2N4250A();
        return cfg;
    }
};

// ── VAS Stage WDF ────────────────────────────────────────────────────────────

template <typename NonlinearLeaf = BJTLeaf>
class VASStageWDF
{
public:
    VASStageWDF() = default;

    void prepare(float sampleRate, const VASConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;
        Ts_         = 1.0f / sampleRate;
        sign_       = config.bjt.polaritySign();

        bjtLeaf_.configure(config.bjt);
        bjtLeaf_.prepare(sampleRate);

        // ── Miller cap: dominant pole at ~17.7 kHz ──────────────────────
        hasMillerCap_ = (config.C_miller > 0.0f);
        if (hasMillerCap_)
        {
            const float fc = 1.0f / (kTwoPif * config.R_coll_AC * config.C_miller);
            // Prewarp cutoff for sample-rate invariance
            const float fc_w = prewarpHz(fc, sampleRate_);
            const float omega = kTwoPif * fc_w * Ts_;
            millerAlpha_ = omega / (1.0f + omega);
        }

        // ── DC bias for PNP forward-active ──────────────────────────────
        V_bias_base_ = sign_ * config.bjt.Vt
                      * std::log(config.I_quiescent / config.bjt.Is + 1.0f);

        // Store quiescent Ic for AC extraction (PNP: Ic_q < 0)
        Ic_quiescent_ = sign_ * config.I_quiescent;  // negative for PNP

        reset();
    }

    void reset()
    {
        bjtLeaf_.reset();
        millerState_   = 0.0f;
        outputVoltage_ = 0.0f;
        lastInputDC_   = 0.0f;

        // Warmup: 64 zero-input samples to converge NR at bias point
        for (int i = 0; i < 64; ++i)
            processSample(0.0f);

        // Calibrate: replace analytical Ic_quiescent with NR-converged value.
        // The WDF port impedance shifts the actual operating point from the
        // analytically predicted one. With R_coll_AC=60kΩ, even 1µA mismatch
        // gives 60mV DC offset → amplified by Aol → saturates the chain.
        Ic_quiescent_ = static_cast<float>(bjtLeaf_.getCollectorCurrent());

        // Re-run warmup with calibrated Ic_q so Miller LP converges to true zero
        millerState_   = 0.0f;
        outputVoltage_ = 0.0f;
        for (int i = 0; i < 64; ++i)
            processSample(0.0f);

        // Freeze: the warmup output becomes the DC offset to subtract
        outputDC_      = outputVoltage_;
        millerState_   = 0.0f;
        outputVoltage_ = 0.0f;
    }

    /// Process one sample through the VAS.
    ///
    /// AC-only model with active load:
    ///   1. Bias + scatter through BJTLeaf (NR solve)
    ///   2. Extract AC collector current: Ic_ac = Ic - Ic_q
    ///   3. AC collector voltage: Vc_ac = -Ic_ac × R_coll_AC (PNP sign handled)
    ///   4. Apply emitter degeneration
    ///   5. Miller lowpass
    ///   6. Subtract frozen DC offset
    float processSample(float input)
    {
        // 1. Bias + WDF scatter
        float baseDrive = input + V_bias_base_;

        const float b_prev = bjtLeaf_.getReflectedWave();
        const float a_bjt  = 2.0f * baseDrive - b_prev;
        bjtLeaf_.scatter(a_bjt);

        // 2. AC collector current
        const float Ic    = static_cast<float>(bjtLeaf_.getCollectorCurrent());
        const float Ic_ac = Ic - Ic_quiescent_;

        // 3. AC collector voltage from active load impedance
        //    For PNP: Ic < 0 normally. When |Ic| increases, Vc goes more negative.
        //    ΔVc = -ΔIc × R_coll_AC. Since Ic_ac = Ic - Ic_q:
        //      Vc_ac = -Ic_ac × R_coll_AC
        float Vc_ac = -Ic_ac * config_.R_coll_AC;

        // ── Even-harmonic suppression (emulates push-pull cancellation) ──
        // Single-ended CE generates H2 from the BJT quadratic term:
        //   Ic ≈ Ic_q + gm·vbe + (gm / 2Vt)·vbe² + ...
        // The vbe² term is always positive → asymmetric Ic swing → H2.
        // In the real JE-990, ~125 dB loop gain suppresses this. Our WDF
        // loop gain (~60 dB) is lower, so we analytically subtract the
        // estimated even component scaled by a loop-gain-dependent factor.
        //
        // The correction is smooth, differentiable, and zero when vbe_ac=0,
        // preserving Newton solver Jacobian continuity and injecting no DC.
        {
            const float gm = static_cast<float>(bjtLeaf_.getGm());

            // Track DC component of input with a slow one-pole filter (~0.1% per sample)
            lastInputDC_ += 0.001f * (input - lastInputDC_);
            const float vbe_ac = input - lastInputDC_;

            // Only apply correction when we have meaningful signal and valid gm
            if (gm > kEpsilonF && std::abs(vbe_ac) > kEpsilonF)
            {
                // Estimated even (H2) component of collector current
                const float Vt = config_.bjt.Vt;
                const float Ic_even = (gm / (2.0f * Vt)) * vbe_ac * vbe_ac;

                // Suppression factor eta: at high loop gain → 0 (full suppression),
                // at low loop gain → 1 (no suppression, saturation regime).
                // Loop gain estimated from instantaneous Vc_ac / vbe_ac ratio.
                constexpr float kEvenSuppK = 0.6f;
                constexpr float kEvenFloor = 0.15f;
                constexpr float kEvenCeil  = 1.0f;

                const float loopGainEst = std::abs(Vc_ac)
                                         / (std::abs(vbe_ac) + 1e-10f);
                float eta = 1.0f / (1.0f + kEvenSuppK
                            * std::min(loopGainEst, 100.0f));
                eta = std::clamp(eta, kEvenFloor, kEvenCeil);

                // Correct: add (1-eta) * Ic_even * R_coll to Vc_ac.
                // Since Vc_ac = -Ic_ac * R_coll, the even term (always positive)
                // biases Vc_ac negative; we add a positive correction to cancel.
                Vc_ac += (1.0f - eta) * Ic_even * config_.R_coll_AC;
            }
        }

        // Soft saturation: models the active mirror compression as Q6 approaches rails.
        // Vc_clip = 90% of Vcc (~21.6V for Vcc=24V).
        // tanh soft-clips smoothly, preserving the Newton Jacobian continuity.
        // The ±200V hard clamp was a numerical guard that never activated in
        // normal operation; this tanh model activates at realistic signal levels
        // and generates asymmetric soft-clipping harmonics from the VAS.
        {
            const float Vc_clip = config_.Vcc * 0.9f;
            Vc_ac = Vc_clip * std::tanh(Vc_ac / Vc_clip);
        }

        // 4. Emitter degeneration: Av_degen = 1/(1 + gm × R_emitter)
        if (config_.R_emitter > 0.0f)
        {
            const float gm = static_cast<float>(bjtLeaf_.getGm());
            if (gm > kEpsilonF)
            {
                const float degen = 1.0f / (1.0f + gm * config_.R_emitter);
                Vc_ac *= degen;
            }
        }

        // 5. Miller lowpass (dominant pole ~17.7 kHz)
        if (hasMillerCap_)
        {
            millerState_ += millerAlpha_ * (Vc_ac - millerState_);
            Vc_ac = millerState_;
        }

        // 6. Remove frozen DC offset
        outputVoltage_ = Vc_ac - outputDC_;

        return outputVoltage_;
    }

    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring ────────────────────────────────────────────────────────────

    float getGainInstantaneous() const
    {
        const float gm = static_cast<float>(bjtLeaf_.getGm());
        if (gm < kEpsilonF) return 0.0f;
        return -config_.R_coll_AC / (config_.R_emitter + 1.0f / gm);
    }

    float getOutputVoltage() const { return outputVoltage_; }
    float getLocalGain() const { return getGainInstantaneous(); }

    /// Per-sample effective gain including Miller LP attenuation.
    /// For the Newton Jacobian, this is the gain actually seen by a single
    /// forward evaluation from snapshot (millerAlpha applied once).
    float getEffectiveGainPerSample() const
    {
        return getGainInstantaneous() * millerAlpha_;
    }
    float getMillerPoleFreq() const
    {
        if (!hasMillerCap_ || config_.C_miller <= 0.0f) return 0.0f;
        return 1.0f / (kTwoPif * config_.R_coll_AC * config_.C_miller);
    }

    float getIc() const { return static_cast<float>(bjtLeaf_.getIc()); }
    float getGm() const { return static_cast<float>(bjtLeaf_.getGm()); }
    float getVbe() const { return static_cast<float>(bjtLeaf_.getVbe()); }
    int getLastIterCount() const { return bjtLeaf_.getLastIterCount(); }

    const NonlinearLeaf& getBJTLeaf() const { return bjtLeaf_; }
    NonlinearLeaf& getBJTLeaf() { return bjtLeaf_; }
    const VASConfig& getConfig() const { return config_; }

    // ── AC state snapshot ─────────────────────────────────────────────────────
    struct AcSnap {
        typename NonlinearLeaf::AcState bjt;
        float millerState, outputVoltage;
        float lastInputDC;
    };

    AcSnap saveAcState() const
    {
        return { bjtLeaf_.saveAcState(), millerState_, outputVoltage_, lastInputDC_ };
    }

    void restoreAcState(const AcSnap& s)
    {
        bjtLeaf_.restoreAcState(s.bjt);
        millerState_   = s.millerState;
        outputVoltage_ = s.outputVoltage;
        lastInputDC_   = s.lastInputDC;
    }

private:
    NonlinearLeaf bjtLeaf_;

    VASConfig config_;
    float sampleRate_ = 44100.0f;
    float Ts_         = 1.0f / 44100.0f;
    float sign_       = -1.0f;          // -1 for PNP (Q6)

    bool  hasMillerCap_  = false;
    float millerAlpha_   = 0.0f;
    float millerState_   = 0.0f;

    float V_bias_base_   = 0.0f;
    float Ic_quiescent_  = 0.0f;        // Signed quiescent Ic

    float outputDC_      = 0.0f;        // Frozen DC offset
    float outputVoltage_ = 0.0f;

    float lastInputDC_   = 0.0f;        // Slow DC tracker for even-harmonic suppression
};

} // namespace transfo
