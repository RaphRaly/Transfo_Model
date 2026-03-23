#pragma once

// =============================================================================
// JE990Path — Complete JE-990 discrete op-amp amplifier path (Chemin B).
//
// Eight-transistor topology modeled with Wave Digital Filters:
//   Etage 1: LM-394 differential pair (Q1/Q2) + tail current Q4
//   Etage 2: 2N4250A PNP cascode (Q3/Q5) — linearized
//   Etage 3: 2N4250A PNP VAS (Q6) + C1=150pF Miller compensation
//   Etage 4: MJE-181/171 Class-AB push-pull output (Q8/Q9) + Q7 pre-driver
//   Output:  39Ω + L3=40µH load isolator
//
// Signal flow:
//   T1 secondary → DiffPair(+) / feedback(−) → Cascode → VAS → ClassAB
//   → LoadIsolator → C_out (220µF) → output to T2
//
// Negative feedback:
//   Output → C_out (AC coupling) → Rfb (variable) → DiffPair(−) input
//   Closed-loop gain: Acl = 1 + Rfb / Rg,  Rg = 47 Ohm
//   Same GainTable as Neve path (11 positions, 100 to 14700 Ohm).
//
// The feedback is applied analytically (same approach as NeveClassAPath):
//   output = v_openloop * (Acl / Aol)
// This avoids 1-sample-delay instability with the ~125 dB open-loop gain.
//
// DC operating point:
//   prepare() runs settling samples to let the WDF stages converge.
//
// Pattern: Strategy (GoF) — implements IAmplifierPath for runtime A/B
//          switching with NeveClassAPath in PreampModel.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md §3.3 (JE-990 DIY);
//            Jensen AES paper (1980); Hardy 990C datasheet
// =============================================================================

#include "DiffPairWDF.h"
#include "CascodeStage.h"
#include "VASStageWDF.h"
#include "ClassABOutputWDF.h"
#include "LoadIsolator.h"
#include "IAmplifierPath.h"
#include "../model/PreampConfig.h"

#include <algorithm>
#include <cmath>

namespace transfo {

class JE990Path : public IAmplifierPath
{
public:
    JE990Path() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Configure from a JE990PathConfig.
    /// Must be called before prepare() (or reconfigure on-the-fly).
    void configure(const JE990PathConfig& config)
    {
        config_ = config;
        configured_ = true;
    }

    /// Prepare all four WDF stages and settle the DC operating point.
    /// @param sampleRate   Host sample rate [Hz].
    /// @param maxBlockSize Maximum expected block size (unused, reserved).
    void prepare(float sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;

        // ── Etage 1: Differential pair (LM-394 matched) ─────────────────
        DiffPairConfig dpCfg;
        dpCfg.q1q2       = config_.q1q2;
        dpCfg.R_emitter  = 30.0f;           // R1, R2 degeneration
        dpCfg.L_emitter  = config_.L1;       // Jensen L1, L2
        dpCfg.I_tail     = 3e-3f;           // Q4 tail current
        dpCfg.R_tail     = 160.0f;          // R3
        dpCfg.R_load     = 300.0f;          // R4, R5 cascode loads
        dpCfg.Vcc        = config_.Vcc;
        diffPair_.prepare(sampleRate, dpCfg);

        // ── Etage 2: Cascode (linearized) ────────────────────────────────
        CascodeConfig casCfg;
        casCfg.R_load      = 300.0f;        // R4, R5
        casCfg.currentGain = 0.98f;         // alpha ≈ 1
        casCfg.Vcc         = config_.Vcc;
        cascode_.prepare(sampleRate, casCfg);

        // ── Etage 3: VAS (Q6 PNP + Miller C1=150pF) ─────────────────────
        VASConfig vasCfg;
        vasCfg.bjt         = config_.q6_vas;
        vasCfg.R_collector = 160.0f;         // R7
        vasCfg.R_emitter   = 130.0f;         // R8
        vasCfg.C_miller    = config_.C1_miller;  // 150 pF
        vasCfg.I_quiescent = 1.5e-3f;        // Set by diff pair current
        vasCfg.Vcc         = config_.Vcc;
        vas_.prepare(sampleRate, vasCfg);

        // ── Etage 4: Class-AB output (Q8 NPN + Q9 PNP) ──────────────────
        ClassABConfig abCfg;
        abCfg.q8              = config_.q8_top;
        abCfg.q9              = config_.q9_bottom;
        abCfg.R_emitter_top   = 39.0f;      // R14
        abCfg.R_emitter_bottom = 39.0f;     // R15
        abCfg.R_sense         = 3.9f;       // R13
        abCfg.C_comp_top      = config_.C2_comp;   // 62 pF
        abCfg.C_comp_bottom   = config_.C3_comp;   // 91 pF
        abCfg.I_quiescent     = 15e-3f;     // Bias current
        abCfg.Vcc             = config_.Vcc;
        classAB_.prepare(sampleRate, abCfg);

        // ── Load isolator (39Ω + L3=40µH) ────────────────────────────────
        LoadIsolatorConfig liCfg;
        liCfg.R_series = config_.R_load_isolator;  // 39 Ohm
        liCfg.L_series = config_.L3;                // 40 µH
        loadIsolator_.prepare(sampleRate, liCfg);

        // ── Feedback HP filter coefficient (C_out coupling) ──────────────
        updateFeedbackCoefficient();

        // ── Design-time open-loop gain from component values ─────────────
        // DiffPair: gm_eff * R_load. At Ic_tail/2 per side:
        //   gm_raw = Ic/(2*Vt) = 1.5e-3/0.052 ≈ 29 mS, Re_degen = 30 Ohm
        //   gm_eff = gm_raw/(1+gm_raw*Re) ≈ 16 mS
        //   DiffGain = gm_eff * R_load = 0.016 * 300 ≈ 4.8
        // Cascode: ~0.98 (current mirror)
        // VAS: Av = Rc_vas / (Re_vas + 1/gm_vas) ≈ 160/(130+1) ≈ 1.22
        // ClassAB: ~1 (emitter follower pair)
        // Aol = DiffGain * 0.98 * VAS_gain ≈ 4.8 * 0.98 * 1.22 ≈ 5.7
        {
            const float Itail_half = 1.5e-3f;
            const float gm_raw = Itail_half / (2.0f * 0.02585f);
            const float Re_diff = 30.0f;
            const float gm_eff = gm_raw / (1.0f + gm_raw * Re_diff);
            const float diffGain = gm_eff * 300.0f;
            const float casGain = 0.98f;
            const float vasGain = 160.0f / (130.0f + 1.0f);
            designAol_ = std::max(diffGain * casGain * vasGain, 1.0f);
        }

        // ── DC settling ─────────────────────────────────────────────────────
        feedbackDC_  = 0.0f;
        outputPrev_  = 0.0f;

        constexpr int kSettleSamples = 500;
        for (int i = 0; i < kSettleSamples; ++i)
            processSample(0.0f);
    }

    /// Clear all internal state.
    void reset() override
    {
        diffPair_.reset();
        cascode_.reset();
        vas_.reset();
        classAB_.reset();
        loadIsolator_.reset();
        feedbackDC_  = 0.0f;
        outputPrev_  = 0.0f;
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    /// Process a single sample through the JE-990 path.
    ///
    /// Signal chain:
    ///   1. DiffPair: differential input (signal vs feedback=0)
    ///   2. Cascode: diff→SE conversion (linear, gain ≈ 1)
    ///   3. VAS: voltage amplification + Miller compensation
    ///   4. ClassAB: push-pull output buffer
    ///   5. LoadIsolator: 39Ω + L3 HF attenuation
    ///   6. Analytical gain correction (Acl / Aol)
    ///   7. C_out HP filter (feedback coupling)
    ///
    /// @param input  Voltage from T1 secondary [V].
    /// @return       Amplified output voltage [V].
    float processSample(float input) override
    {
        // 1. Drive all WDF stages (open-loop path)
        const float v1 = diffPair_.processSample(input, 0.0f);   // Differential pair
        const float v2 = cascode_.processSample(v1);              // Cascode: diff→SE
        const float v3 = vas_.processSample(v2);                  // VAS: voltage gain
        const float v4 = classAB_.processSample(v3);              // Class-AB output
        float v5 = loadIsolator_.processSample(v4);               // Load isolator

        // Clamp to supply rails
        v5 = std::clamp(v5, -config_.Vcc, config_.Vcc);

        // 2. Apply closed-loop gain correction analytically
        const float Acl = 1.0f + Rfb_ / Rg_;
        const float Aol = getOpenLoopGain();

        float output;
        if (Aol > 1.0f) {
            output = v5 * std::min(Acl / Aol, Acl);
        } else {
            output = v5 * std::min(Acl / designAol_, Acl);
        }

        // 3. C_out HP filter (output coupling cap)
        feedbackDC_ += feedbackAlpha_ * (output - feedbackDC_);
        output -= feedbackDC_;

        outputPrev_ = output;
        return output;
    }

    // ── Gain control ──────────────────────────────────────────────────────────

    /// Set the feedback resistor value [Ohm].
    /// Same GainTable as Neve path (11 positions, 100 to 14700 Ohm).
    void setGain(float Rfb) override
    {
        Rfb_ = std::max(Rfb, 1.0f);
        updateFeedbackCoefficient();
    }

    // ── Monitoring / output stage coupling ────────────────────────────────────

    /// Output impedance [Ohm].
    /// JE-990: Zout ≈ ClassAB Zout + LoadIsolator R_series
    /// Typically < 5 Ohm (before load isolator adds 39Ω).
    /// After isolator: ~44 Ohm. But for T2 coupling, report pre-isolator.
    float getOutputImpedance() const override
    {
        return classAB_.getOutputImpedance();
    }

    /// Human-readable name for UI / diagnostics.
    const char* getName() const override { return "Jensen Heritage"; }

    // ── Operating point monitoring ────────────────────────────────────────────

    /// Instantaneous closed-loop gain [linear].
    float getClosedLoopGain() const
    {
        return 1.0f + Rfb_ / Rg_;
    }

    /// Instantaneous closed-loop gain [dB].
    float getClosedLoopGainDB() const
    {
        return 20.0f * std::log10(getClosedLoopGain());
    }

    /// Current feedback resistor value [Ohm].
    float getRfb() const { return Rfb_; }

    /// Open-loop gain estimate: product of DiffPair, Cascode, VAS gains.
    /// ClassAB is unity gain (emitter follower).
    ///
    /// IMPORTANT: Each stage's processSample() already applies its own
    /// emitter degeneration. So the actual open-loop gain through the
    /// signal path includes degeneration effects. We must estimate Aol
    /// consistently with what processSample() produces.
    ///
    /// DiffPair: output = dIc * degenFactor * R_load
    ///   → voltage gain = gm_raw * degenFactor * R_load
    ///   where gm_raw = Ic/(2*Vt) per half, degenFactor = 1/(1+gm*Re)
    ///   → effectively gm_eff * R_load (gm_eff already reported by getGm())
    ///
    /// VAS: output = (Vc - Vc_dc) with degeneration applied
    ///   → voltage gain = getGainInstantaneous() (already degenerated)
    float getOpenLoopGain() const
    {
        // DiffPair effective gain (gm_eff already includes degeneration)
        const float gm_eff = diffPair_.getGm();
        const float diffGain = gm_eff * 300.0f;  // R_load = 300Ω

        // Cascode: linear pass-through
        const float casGain = 0.98f;

        // VAS: getGainInstantaneous() returns degenerated gain
        const float vasGain = vas_.getGainInstantaneous();

        // ClassAB: unity gain (emitter follower)
        // Total Aol = diffGain * casGain * |vasGain|
        return diffGain * casGain * std::abs(vasGain);
    }

private:
    // ── Amplifier stages ──────────────────────────────────────────────────────
    DiffPairWDF<BJTLeaf>       diffPair_;
    CascodeStage               cascode_;
    VASStageWDF<BJTLeaf>       vas_;
    ClassABOutputWDF<BJTLeaf>  classAB_;
    LoadIsolator               loadIsolator_;

    // ── Feedback network ──────────────────────────────────────────────────────
    float Rfb_           = 1430.0f;     // Feedback resistor [Ohm] (default: +30 dB)
    float Rg_            = 47.0f;       // Gain reference resistor [Ohm]
    float feedbackDC_    = 0.0f;        // LP filter tracking DC component
    float feedbackAlpha_ = 0.0f;        // One-pole LP coefficient for DC tracking
    float outputPrev_    = 0.0f;        // Previous output sample
    float designAol_     = 8.0f;        // Design-time open-loop gain (from component values)

    // ── Configuration ─────────────────────────────────────────────────────────
    JE990PathConfig config_;
    float sampleRate_ = 44100.0f;
    bool  configured_ = false;

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Recompute the feedback HP filter coefficient.
    /// C_out (220µF) with Rfb forms a HP corner:
    ///   f_hp = 1 / (2 * pi * C_out * Rfb)
    void updateFeedbackCoefficient()
    {
        const float C_out = config_.C_out;  // 220 µF
        if (C_out <= 0.0f || Rfb_ <= 0.0f) {
            feedbackAlpha_ = 0.0f;
            return;
        }
        const float f_hp = 1.0f / (kTwoPif * C_out * Rfb_);
        const float omega = kTwoPif * f_hp / sampleRate_;
        feedbackAlpha_ = omega / (1.0f + omega);
    }
};

} // namespace transfo
