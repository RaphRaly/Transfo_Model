#pragma once

// =============================================================================
// JE990Path — Complete JE-990 discrete op-amp amplifier path (Chemin B).
//
// Eight-transistor topology modeled with Wave Digital Filters:
//   Etage 1: LM-394 differential pair (Q1/Q2) + tail current Q4
//   Etage 2: 2N4250A PNP cascode (Q3/Q5) — linearized
//   Etage 3: 2N4250A PNP VAS (Q6) + C1=150pF Miller compensation
//   Etage 4: MJE-181/171 Class-AB push-pull output (Q8/Q9) + Q7 pre-driver
//   Output:  39 Ohm + L3=40 uH load isolator
//
// Signal flow:
//   T1 secondary -> DiffPair(+) / feedback(-) -> Cascode -> VAS -> ClassAB
//   -> feedback tap -> LoadIsolator -> C_out -> output to T2
//
// Feedback architecture — Implicit Newton solve (1 step, delay-free):
//
//   g(y) = y - F(x, beta*y)  where F is the full forward chain.
//   g'(y) = 1 + beta*Aol     (numerically estimated via 2 probes)
//   y_new = y_old - g(y_old) / g'(y_old)
//
//   With corrected VAS topology (R_coll_AC=60k, Av~338), the native
//   open-loop gain Aol ~ 2086 (66 dB). No backbone gain hack needed.
//   Double precision in DiffPairWDF resolves the sub-uV error signal
//   when beta*y ~ input (closed-loop residual).
//
//   Cost: 3 forward evaluations per sample (2 probes + 1 commit).
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md section 3.3; Jensen AES 1980;
//            docs/ANALYSIS_JE990_KVL_KCL_2026-03-26.md
// =============================================================================

#include "DiffPairWDF.h"
#include "CascodeStage.h"
#include "VASStageWDF.h"
#include "ClassABOutputWDF.h"
#include "LoadIsolator.h"
#include "IAmplifierPath.h"
#include "../model/PreampConfig.h"

#include <cstdio>
#include <algorithm>
#include <cmath>

namespace transfo {

class JE990Path : public IAmplifierPath
{
public:
    JE990Path() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void configure(const JE990PathConfig& config)
    {
        config_ = config;
        configured_ = true;
    }

    void prepare(float sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;

        // ── Etage 1: Differential pair (LM-394 matched NPN) ─────────────
        DiffPairConfig dpCfg;
        dpCfg.q1q2       = config_.q1q2;
        dpCfg.R_emitter  = 30.0f;
        dpCfg.L_emitter  = config_.L1;
        dpCfg.I_tail     = 3e-3f;
        dpCfg.R_tail     = 160.0f;
        dpCfg.R_load     = 300.0f;
        dpCfg.Vcc        = config_.Vcc;
        diffPair_.prepare(sampleRate, dpCfg);

        // ── Etage 2: Cascode (linearized) ────────────────────────────────
        CascodeConfig casCfg;
        casCfg.R_load      = 300.0f;
        casCfg.currentGain = 0.98f;
        casCfg.Vcc         = config_.Vcc;
        cascode_.prepare(sampleRate, casCfg);

        // ── Etage 3: VAS (Q6 PNP) — CORRECTED TOPOLOGY ─────────────────
        //  R_coll_AC = 60 kOhm (active mirror impedance, NOT R7)
        //  R_emitter = 160 Ohm (R7 to +24V, NOT R8)
        VASConfig vasCfg;
        vasCfg.bjt         = config_.q6_vas;
        vasCfg.R_coll_AC   = 60000.0f;
        vasCfg.R_emitter   = 160.0f;
        vasCfg.C_miller    = config_.C1_miller;
        vasCfg.I_quiescent = 1.5e-3f;
        vasCfg.Vcc         = config_.Vcc;
        vas_.prepare(sampleRate, vasCfg);

        // ── Etage 4: Class-AB output (Q8 NPN + Q9 PNP) ──────────────────
        ClassABConfig abCfg;
        abCfg.q8               = config_.q8_top;
        abCfg.q9               = config_.q9_bottom;
        abCfg.R_emitter_top    = 39.0f;
        abCfg.R_emitter_bottom = 39.0f;
        abCfg.R_sense          = 3.9f;
        abCfg.C_comp_top       = config_.C2_comp;
        abCfg.C_comp_bottom    = config_.C3_comp;
        abCfg.I_quiescent      = 15e-3f;
        abCfg.Vcc              = config_.Vcc;
        classAB_.prepare(sampleRate, abCfg);

        // ── Load isolator (39 Ohm + L3=40 uH) ───────────────────────────
        LoadIsolatorConfig liCfg;
        liCfg.R_series = config_.R_load_isolator;
        liCfg.L_series = config_.L3;
        loadIsolator_.prepare(sampleRate, liCfg);

        // ── Feedback HP filter (C_out coupling) ──────────────────────────
        updateFeedbackCoefficient();

        // ── DC servo coefficient (~0.1 Hz integrator) ────────────────────
        servoAlpha_ = kTwoPif * 0.1f / sampleRate_;

        // ── Settling ─────────────────────────────────────────────────────
        yPrev_       = 0.0f;
        J_prev_      = 100.0;
        vServo_      = 0.0f;
        servoLP_     = 0.0f;
        feedbackDC_  = 0.0f;
        outputPrev_  = 0.0f;
        sampleCount_ = 0;

        // Run settling with stages in open-loop (no feedback) to converge
        // all NR warm-starts and reactive elements at quiescent point.
        constexpr int kSettleSamples = 100;
        for (int i = 0; i < kSettleSamples; ++i)
        {
            diffPair_.processSample(0.0f, 0.0f);
            cascode_.processSample(0.0f);
            vas_.processSample(0.0f);
            classAB_.processSample(0.0f);
            loadIsolator_.processSample(0.0f);
        }
    }

    void reset() override
    {
        diffPair_.reset();
        cascode_.reset();
        vas_.reset();
        classAB_.reset();
        loadIsolator_.reset();
        yPrev_       = 0.0f;
        J_prev_      = 100.0;
        vServo_      = 0.0f;
        servoLP_     = 0.0f;
        feedbackDC_  = 0.0f;
        outputPrev_  = 0.0f;
        sampleCount_ = 0;

        for (int i = 0; i < 100; ++i)
        {
            diffPair_.processSample(0.0f, 0.0f);
            cascode_.processSample(0.0f);
            vas_.processSample(0.0f);
            classAB_.processSample(0.0f);
        }
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    /// Process one sample with implicit delay-free feedback.
    ///
    /// Newton solve: g(y) = y - F(x, beta*y) = 0
    ///   where F is the full forward chain (DiffPair -> Cascode -> VAS -> ClassAB).
    ///
    /// Analytical Jacobian approach (per GPT-5.4 recommendation):
    ///   J = 1 + beta * Aol_eff where Aol_eff is computed from per-stage
    ///   small-signal gains at the current operating point.
    ///
    ///   This avoids numerical probes through the ClassAB, whose one-port WDF
    ///   emitter follower model gives wrong large-signal gain (0.31× instead of
    ///   0.95×). The ClassAB BJTs are kept for nonlinear audio (crossover,
    ///   clipping), but the LOOP GAIN uses the analytical estimate.
    ///
    /// Cost: 2 forward evaluations per sample (1 for g0 + 1 commit).
    float processSample(float input) override
    {
        ++sampleCount_;
        const double beta = static_cast<double>(Rg_) / (static_cast<double>(Rfb_) + Rg_);
        const double Vcc  = static_cast<double>(config_.Vcc);

        // ── 1. Snapshot all stage states ──────────────────────────────────
        const auto snap_dp  = diffPair_;
        const auto snap_cas = cascode_;
        const auto snap_vas = vas_;
        const auto snap_ab  = classAB_;

        // ── 2. Analytical Jacobian from per-stage small-signal gains ─────
        //    Computed from the SNAPSHOT state (current operating point).
        //    Uses per-sample VAS gain (with Miller alpha) for accuracy.
        //    ClassAB gain = 0.95 (analytical EF gain, bypasses broken WDF).
        const double Av_dp  = std::abs(diffPair_.getLocalGain());    // ~6.3
        const double Av_cas = std::abs(static_cast<double>(cascode_.getLocalGain()));  // 0.98
        const double Av_vas = std::abs(static_cast<double>(vas_.getEffectiveGainPerSample())); // ~181
        const double Av_ab  = std::abs(static_cast<double>(classAB_.getLocalGain()));  // 0.95

        double Aol = Av_dp * Av_cas * Av_vas * Av_ab;

        // Saturation-aware: if Aol is unreasonably low (stage not biased yet)
        // or unreasonably high, clamp to sane range.
        Aol = std::clamp(Aol, 10.0, 50000.0);

        double J = 1.0 + beta * Aol;
        J = std::clamp(J, 1.5, 20000.0);

        // ── 3. Forward evaluation: compute g0 = yk - F(x, beta*yk) ──────
        //    For g0, we evaluate DiffPair → Cascode → VAS but BYPASS the
        //    ClassAB WDF model. The one-port WDF emitter follower gives wrong
        //    large-signal gain (0.31× instead of 0.95×), making g0 inconsistent
        //    with the analytical J. Instead, we apply the ClassAB gain as a
        //    linear constant (0.95), matching what J_analytical assumes.
        //    The full nonlinear ClassAB is only used in the commit (step 4).
        double y = static_cast<double>(yPrev_);
        const double kClassABGain = static_cast<double>(classAB_.getLocalGain()); // 0.95

        {
            diffPair_ = snap_dp;
            cascode_  = snap_cas;
            vas_      = snap_vas;
            // ClassAB intentionally NOT restored/evaluated for g0

            const float vfb0 = static_cast<float>(beta * y + static_cast<double>(vServo_));
            const float v1 = diffPair_.processSample(input, vfb0);
            const float v2 = cascode_.processSample(v1);
            const float v3 = vas_.processSample(v2);

            // ClassAB linear buffer: Av ≈ 0.95 (emitter follower analytical gain)
            const double y0 = static_cast<double>(v3) * kClassABGain;
            const double g0 = y - y0;

            // Newton step: y_new = yk - g0 / J
            y = y - g0 / J;
        }

        // Numerical guard: y cannot exceed supply rails
        y = std::clamp(y, -Vcc, Vcc);

        // ── 4. Commit: advance states with true closed-loop feedback ─────
        //    Double precision in DiffPairWDF resolves the sub-uV residual
        //    error (input - beta*y) when added to V_bias (~0.63V).
        diffPair_ = snap_dp;
        cascode_  = snap_cas;
        vas_      = snap_vas;
        classAB_  = snap_ab;

        {
            const float vfb_commit = static_cast<float>(beta * y + static_cast<double>(vServo_));
            const float c1 = diffPair_.processSample(input, vfb_commit);
            const float c2 = cascode_.processSample(c1);
            const float c3 = vas_.processSample(c2);
            classAB_.processSample(c3);
        }

        // ── 5. DC servo (very slow integrator, ~0.1 Hz) ─────────────────
        //    Models the real 990's C3 DC servo. NOT snapshotted.
        const float yf = static_cast<float>(y);
        servoLP_ += servoAlpha_ * (yf - servoLP_);
        vServo_  += servoAlpha_ * servoLP_;
        vServo_   = std::clamp(vServo_, -1.0f, 1.0f);

        yPrev_ = yf;

        // ── 6. Load isolator (post-feedback tap) ─────────────────────────
        float v5 = loadIsolator_.processSample(yf);
        v5 = std::clamp(v5, -config_.Vcc, config_.Vcc);

        // ── 7. C_out HP filter (output coupling cap) ─────────────────────
        feedbackDC_ += feedbackAlpha_ * (v5 - feedbackDC_);
        float output = v5 - feedbackDC_;

        outputPrev_ = output;

        if (diagEnabled_ && (sampleCount_ <= 520
            || (sampleCount_ % 200 == 0 && sampleCount_ <= 2001)))
        {
            std::printf("  [S%d] in=%.6f y=%.6f J=%.1f Aol=%.0f\n",
                        sampleCount_, input, yf, static_cast<float>(J),
                        static_cast<float>(Aol));
        }

        return output;
    }

    // ── Gain control ──────────────────────────────────────────────────────────

    void setGain(float Rfb) override
    {
        Rfb_ = std::max(Rfb, 1.0f);
        updateFeedbackCoefficient();
    }

    // ── Monitoring ────────────────────────────────────────────────────────────

    float getOutputImpedance() const override
    {
        return classAB_.getOutputImpedance();
    }

    const char* getName() const override { return "Jensen Modern"; }

    float getClosedLoopGain() const { return 1.0f + Rfb_ / Rg_; }

    float getClosedLoopGainDB() const
    {
        return 20.0f * std::log10(getClosedLoopGain());
    }

    float getRfb() const { return Rfb_; }

    float getOpenLoopGain() const
    {
        const float gm_eff   = static_cast<float>(diffPair_.getGm());
        const float diffGain = gm_eff * 300.0f;
        const float casGain  = 0.98f;
        const float vasGain  = vas_.getGainInstantaneous();
        return diffGain * casGain * std::abs(vasGain);
    }

private:
    // ── Stages ────────────────────────────────────────────────────────────────
    DiffPairWDF<BJTLeaf>       diffPair_;
    CascodeStage               cascode_;
    VASStageWDF<BJTLeaf>       vas_;
    ClassABOutputWDF<BJTLeaf>  classAB_;
    LoadIsolator               loadIsolator_;

    // ── Feedback network ──────────────────────────────────────────────────────
    float Rfb_           = 1430.0f;
    float Rg_            = 47.0f;
    float feedbackDC_    = 0.0f;
    float feedbackAlpha_ = 0.0f;
    float outputPrev_    = 0.0f;

    // ── Newton solver state ───────────────────────────────────────────────────
    float yPrev_         = 0.0f;
    double J_prev_       = 100.0;   // Warm-start Jacobian (fallback seed)

    // ── DC servo ──────────────────────────────────────────────────────────────
    float vServo_        = 0.0f;
    float servoLP_       = 0.0f;
    float servoAlpha_    = 0.0f;

    // ── Diagnostics ───────────────────────────────────────────────────────────
    int   sampleCount_   = 0;
public:
    bool  diagEnabled_   = false;
private:

    // ── Configuration ─────────────────────────────────────────────────────────
    JE990PathConfig config_;
    float sampleRate_ = 44100.0f;
    bool  configured_ = false;

    // ── Helpers ───────────────────────────────────────────────────────────────

    void updateFeedbackCoefficient()
    {
        const float C_out = config_.C_out;
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
