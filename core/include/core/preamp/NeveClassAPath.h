#pragma once

// =============================================================================
// [ARCHIVED] NeveClassAPath — Neve Heritage Class-A amplifier path.
//
// STATUS: ARCHIVED (2026-04-03). Replaced by Neve1063Path (EH10013 model).
//         Kept for reference and comparison. Not used in PreampModel.
//
// ORIGINAL:
// Three-transistor topology modeled with Wave Digital Filters:
//   Q1 BC184C  — Common-emitter input stage (NPN, voltage gain)
//   Q2 BC214C  — Common-emitter second stage (PNP, voltage gain)
//   Q3 BD139   — Emitter-follower output (NPN, current gain / buffer)
//
// Signal flow:
//   T1 secondary → C_input (100µF) → Q1(CE) → Q2(CE) → Q3(EF) → output
//
// The two CE stages each invert, so the overall open-loop gain is positive
// (product of two negative gains). Q3 provides unity voltage gain with low
// output impedance (~11 Ohm) for driving the output transformer T2.
//
// Negative feedback:
//   Q3 emitter → C6 (470µF, AC coupling) → Rfb (variable) → Q1 emitter
//   Closed-loop gain: Acl = 1 + Rfb / Rg,  Rg = 47 Ohm
//   Rfb is selected from GainTable via setGain():
//     Rfb = 100 Ohm  → +10 dB   (position 1)
//     Rfb = 1430 Ohm → +30 dB   (position 6, default)
//     Rfb = 14700 Ohm → +50 dB  (position 11)
//
// The feedback coupling capacitor C6 with Rfb forms a high-pass filter at
//   f_hp = 1 / (2*pi*C6*Rfb) ≈ 0.24 Hz (Rfb=1430) to 2.4 Hz (Rfb=14700)
// This is modeled as a one-pole HP filter on the feedback path.
//
// DC operating point:
//   prepare() runs 2000 zero-input samples to let the three WDF stage
//   solutions converge to their quiescent operating points.
//
// Pattern: Strategy (GoF) — implements IAmplifierPath for runtime A/B
//          switching with JE990Path in PreampModel.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md §3.2 (Neve Heritage Class-A);
//            Neve 1073 / 1084 amplifier card schematic analysis
// =============================================================================

#include "CEStageWDF.h"
#include "EFStageWDF.h"
#include "IAmplifierPath.h"
#include "../model/PreampConfig.h"

#include <algorithm>
#include <cmath>

namespace transfo {

class NeveClassAPath : public IAmplifierPath
{
public:
    NeveClassAPath() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Configure from a NevePathConfig.
    /// Must be called before prepare() (or reconfigure on-the-fly).
    void configure(const NevePathConfig& config)
    {
        config_ = config;
        configured_ = true;
    }

    /// Prepare the three WDF stages and settle the DC operating point.
    /// @param sampleRate   Host sample rate [Hz].
    /// @param maxBlockSize Maximum expected block size (unused, reserved).
    void prepare(float sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;

        // ── Q1: BC184C common-emitter ─────────────────────────────────────
        CEStageConfig q1Cfg;
        q1Cfg.bjt          = config_.q1;
        q1Cfg.R_collector  = config_.R_collector_q1;
        q1Cfg.R_emitter    = Rg_;                      // Rg = 47 Ohm (gain reference)
        q1Cfg.R_base_bias  = config_.R_bias_base_q1;   // Thevenin of R6A||R7A
        q1Cfg.C_input      = config_.C_input;           // 100 µF input coupling
        q1Cfg.C_miller     = config_.C_miller;          // 100 pF Miller comp
        q1Cfg.C_bypass     = 0.0f;                      // No bypass — Rg sets gain
        q1Cfg.Vcc          = config_.Vcc;
        q1Stage_.prepare(sampleRate, q1Cfg);

        // ── Q2: BC214C common-emitter (PNP) ──────────────────────────────
        CEStageConfig q2Cfg;
        q2Cfg.bjt          = config_.q2;
        q2Cfg.R_collector  = config_.R_collector_q2;
        q2Cfg.R_emitter    = config_.R_emitter_q2;
        q2Cfg.R_base_bias  = config_.R_collector_q1;    // Q1 collector = Q2 base bias path
        q2Cfg.C_input      = 0.0f;                      // DC-coupled Q1→Q2
        q2Cfg.C_miller     = 0.0f;                      // No Miller on second stage
        q2Cfg.C_bypass     = 0.0f;
        q2Cfg.Vcc          = config_.Vcc;
        q2Stage_.prepare(sampleRate, q2Cfg);

        // ── Q3: BD139 emitter follower ────────────────────────────────────
        EFStageConfig q3Cfg;
        q3Cfg.bjt          = config_.q3;
        q3Cfg.R_bias       = config_.R_bias_q3;         // 390 Ohm to -Vcc
        q3Cfg.C_out        = config_.C_out;              // 220 µF electrolytic
        q3Cfg.C_out_film   = config_.C_out_film;         // 4.7 µF film parallel
        q3Cfg.R_series_out = config_.R_series_out;       // 10 Ohm series output
        q3Cfg.Vcc          = config_.Vcc;
        q3Stage_.prepare(sampleRate, q3Cfg);

        // ── Feedback HP filter coefficient (C6 coupling) ──────────────────
        updateFeedbackCoefficient();

        // ── Design-time open-loop gain from component values ─────────────
        // Q1 CE: Av1 = Rc1 / (Re1 + rbe/gm1). At Ic_q = Vcc/(2*Rc):
        //   gm = Ic/Vt, Re = Rg = 47 Ohm → Av1 ≈ Rc1/Re1 at high gm
        // Q2 CE: Av2 = Rc2 / (Re2 + 1/gm2). Re2 = 7500 Ohm → Av2 ≈ Rc2/Re2
        // Q3 EF: Av3 ≈ 1
        // Aol = |Av1 * Av2 * Av3|
        {
            const float Rc1 = config_.R_collector_q1;
            const float Re1 = Rg_;    // 47 Ohm
            const float Rc2 = config_.R_collector_q2;
            const float Re2 = config_.R_emitter_q2;
            const float Av1 = Rc1 / (Re1 + 1.0f);  // +1 for 1/gm approx
            const float Av2 = Rc2 / (Re2 + 1.0f);
            designAol_ = std::max(Av1 * Av2, 1.0f);
        }

        // ── DC settling ─────────────────────────────────────────────────────
        // Each stage's prepare() already includes reset + warmup.
        // Run a few zero-input samples to let the cascaded stages and
        // C6 HP filter settle. With analytical gain, no instability risk.
        feedbackDC_  = 0.0f;
        outputPrev_  = 0.0f;

        constexpr int kSettleSamples = 500;
        for (int i = 0; i < kSettleSamples; ++i)
            processSample(0.0f);
    }

    /// Clear all internal state (capacitor memories, NR warm-starts).
    void reset() override
    {
        q1Stage_.reset();
        q2Stage_.reset();
        q3Stage_.reset();
        feedbackDC_  = 0.0f;
        outputPrev_  = 0.0f;
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    /// Process a single sample through the three-transistor Neve path.
    ///
    /// Signal chain:
    ///   1. Compute AC-coupled feedback voltage (C6 HP + Rfb/Rg divider)
    ///   2. Subtract feedback from input (negative feedback)
    ///   3. Q1 CE stage (NPN, inverts)
    ///   4. Q2 CE stage (PNP, inverts → net positive gain)
    ///   5. Q3 EF stage (unity gain buffer)
    ///   6. Update feedback state (one-pole HP for C6)
    ///
    /// @param input  Voltage from T1 secondary [V].
    /// @return       Amplified output voltage [V].
    float processSample(float input) override
    {
        // 1. Drive the three WDF stages (open-loop amplifier)
        const float v1 = q1Stage_.processSample(input);      // Q1 CE: inverts
        const float v2 = q2Stage_.processSample(v1);          // Q2 CE: inverts (net positive)
        float v3 = q3Stage_.processSample(v2);                // Q3 EF: ~unity gain buffer

        // Clamp to supply rails
        v3 = std::clamp(v3, -config_.Vcc, config_.Vcc);

        // 2. Apply closed-loop gain correction analytically
        // Acl = 1 + Rfb/Rg (feedback network gain)
        // Aol = measured open-loop gain from WDF stages
        // Output = v3 * (Acl / Aol) if Aol > Acl, else just v3
        const float Acl = 1.0f + Rfb_ / Rg_;
        const float Aol = std::abs(getOpenLoopGain());

        float output;
        if (Aol > 1.0f) {
            // Scale WDF output by feedback ratio
            output = v3 * std::min(Acl / Aol, Acl);
        } else {
            // Aol too low (startup/convergence) — use design-time estimate
            output = v3 * std::min(Acl / designAol_, Acl);
        }

        // 3. C6 HP filter (feedback coupling cap)
        feedbackDC_ += feedbackAlpha_ * (output - feedbackDC_);
        output -= feedbackDC_;

        outputPrev_ = output;
        return output;
    }

    // ── Gain control ──────────────────────────────────────────────────────────

    /// Set the feedback resistor value [Ohm].
    /// Rfb values come from GainTable (11 positions, 100 to 14700 Ohm).
    /// Closed-loop gain: Acl = 1 + Rfb / Rg, Rg = 47 Ohm.
    void setGain(float Rfb) override
    {
        Rfb_ = std::max(Rfb, 1.0f);  // Protect against zero/negative
        updateFeedbackCoefficient();
    }

    // ── Monitoring / output stage coupling ────────────────────────────────────

    /// Output impedance [Ohm].
    /// Zout ≈ 1/gm_Q3 + R_series_out ≈ 1 Ohm + 10 Ohm ≈ 11 Ohm.
    /// Used by T2 for impedance matching.
    float getOutputImpedance() const override
    {
        return q3Stage_.getOutputImpedance();
    }

    /// Human-readable name for UI / diagnostics.
    const char* getName() const override { return "Neve Heritage"; }

    // ── Operating point monitoring ────────────────────────────────────────────

    /// DC operating point of all three transistors.
    /// Useful for BHScope-style visualization and validation against
    /// SPICE reference (ANALYSE_ET_DESIGN_Rev2.md Table 3.2.1).
    struct OperatingPoint
    {
        float Vce_q1 = 0.0f;
        float Ic_q1  = 0.0f;
        float Vce_q2 = 0.0f;
        float Ic_q2  = 0.0f;
        float Vce_q3 = 0.0f;
        float Ic_q3  = 0.0f;
    };

    OperatingPoint getOperatingPoint() const
    {
        OperatingPoint op;
        op.Vce_q1 = q1Stage_.getVce();
        op.Ic_q1  = q1Stage_.getIc();
        op.Vce_q2 = q2Stage_.getVce();
        op.Ic_q2  = q2Stage_.getIc();
        op.Vce_q3 = q3Stage_.getVce();
        op.Ic_q3  = q3Stage_.getIc();
        return op;
    }

    /// Instantaneous closed-loop gain [linear].
    /// Theoretical: 1 + Rfb / Rg.
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

    /// Open-loop gain estimate: product of Q1 and Q2 voltage gains.
    /// Aol ≈ gm1*Rc1 * gm2*Rc2 (positive, two inversions cancel).
    float getOpenLoopGain() const
    {
        return q1Stage_.getGainInstantaneous() * q2Stage_.getGainInstantaneous();
    }

private:
    // ── WDF amplifier stages ──────────────────────────────────────────────────
    CEStageWDF<BJTLeaf> q1Stage_;
    CEStageWDF<BJTLeaf> q2Stage_;
    EFStageWDF<BJTLeaf> q3Stage_;

    // ── Feedback network ──────────────────────────────────────────────────────
    float Rfb_           = 1430.0f;     // Feedback resistor [Ohm] (default: position 6, +30 dB)
    float Rg_            = 47.0f;       // Gain reference resistor [Ohm]
    float feedbackDC_    = 0.0f;        // LP filter tracking DC component of v3
    float feedbackAlpha_ = 0.0f;        // One-pole LP coefficient for DC tracking
    float outputPrev_    = 0.0f;        // Previous output sample (for feedback)
    float designAol_     = 290.0f;      // Design-time open-loop gain (from component values)

    // ── Configuration ─────────────────────────────────────────────────────────
    NevePathConfig config_;
    float sampleRate_ = 44100.0f;
    bool  configured_ = false;

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Recompute the feedback HP filter coefficient after Rfb or sample rate change.
    ///
    /// C6 (470µF) with Rfb forms a HP corner:
    ///   f_hp = 1 / (2 * pi * C6 * Rfb)
    ///
    /// One-pole discretization (bilinear-ish):
    ///   omega = 2 * pi * f_hp / sampleRate
    ///   alpha = omega / (1 + omega)
    ///
    /// At Rfb=1430 Ohm: f_hp ≈ 0.24 Hz (well below audio band).
    /// At Rfb=100 Ohm:  f_hp ≈ 3.4 Hz  (slight LF rolloff at max gain).
    void updateFeedbackCoefficient()
    {
        const float C6 = config_.C_emitter_bypass;  // 470 µF
        const float f_hp = 1.0f / (kTwoPif * C6 * Rfb_);
        // Prewarp cutoff for sample-rate invariance
        const float f_hp_w = prewarpHz(f_hp, sampleRate_);
        const float omega = kTwoPif * f_hp_w / sampleRate_;
        feedbackAlpha_ = omega / (1.0f + omega);
    }
};

} // namespace transfo
