#pragma once

// =============================================================================
// Neve1063Path — Neve 1063 Channel Amplifier path (Chemin A).
//
// Faithful model of the EH10013 schematic (Neve 1063 Channel Amplifier),
// reverse-engineered node-by-node from the original drawings, cross-referenced
// with JLM Audio BA283, Celestial NeveInfo2.PDF, and GroupDIY documentation.
//
// Architecture: three cascaded modules from the 1063 card:
//
//   BA184 N-V (B112-type mic preamp):
//     TR4 BC109 (CE, low-noise input)
//     TR5 BC107 (CE, second gain stage — inverts, making module non-inverting)
//     TR6 BC107 (EF output)
//     Cross-feedback: BA183 NV output → BA184 input via R_cross_fb
//
//   BA183 N-V (B112-type variable gain preamp):
//     TR4 BC109 (CE, low-noise input)
//     TR5 BC107 (CE, second gain stage — inverts, making module non-inverting)
//     TR6 BC107 (EF output)
//     Gain control: emitter degeneration R18 via T-V pins
//     Internal feedback: 18K S->U (local Newton, delay-free)
//
//   BA183 A-M (B110-type Class-A line amplifier):
//     TR1 BC184C (CE input)
//     TR2 BC184C (CE driver, DC-coupled to TR3)
//     TR3 BDY61 (CE power output, ~70mA Class-A, drives LO1166)
//     Internal feedback: C4 AC cap (TR2 collector -> TR1 emitter, local Newton)
//
// Feedback levels:
//   Level 1: Local emitter degeneration on each transistor
//   Level 2: Cross-feedback BA184↔BA183 NV via R1/R3 (one-sample delay)
//   Level 3: Per-module local Newton (NV: 18K S→U, AM: C4 cap)
//   Gain:    Analytical closed-loop scaling Acl/Aol (like NeveClassAPath)
//
// Each BA184/BA183 module has 2 CE stages + 1 EF = net NON-inverting.
// This makes local feedback Jacobian J = 1 + beta*Aol > 0 (always converges).
//
// All transistors are NPN (no PNP in the signal path).
// TR3 BDY61 drives ~70mA DC through LO1166 output transformer primary,
// creating the asymmetric B-H signature (H2 generation) characteristic of
// Neve preamps.
//
// Pattern: Strategy (GoF) — implements IAmplifierPath for runtime A/B
//          switching with JE990Path in PreampModel.
//
// Reference: neve_1063_retranscription.md (EH10013, 86% verified);
//            BA283AV Class A Preamp Guide; Celestial NeveInfo2.PDF
// =============================================================================

#include "CEStageWDF.h"
#include "EFStageWDF.h"
#include "IAmplifierPath.h"
#include "../model/PreampConfig.h"

#include <algorithm>
#include <cmath>

namespace transfo {

class Neve1063Path : public IAmplifierPath
{
    // ── T-V table calibration constants ───────────────────────────────────
    // Documented BA283 first stage: Gain = K_tv / (Re_tv ∥ R_TV)
    // WDF model:                    Gain = K_wdf / (Re_tr4 + 1/gm)
    // Calibration: Re_tr4 = (K_wdf/K_tv) × (Re_tv ∥ R_TV) - 1/gm
    static constexpr float kReTv       = 259.0f;   // T-V table internal Re [Ohm]
    static constexpr float kKTv        = 2057.0f;  // T-V table gain constant
    static constexpr float kKWdf       = 6136.0f;  // WDF gain constant (Rc × |Av5×Av6|)
    static constexpr float kNvCalRatio = kKWdf / kKTv;  // ≈ 2.983
    static constexpr float kNvRe       = 15.0f;    // 1/gm approx for BC109 @ ~1.8mA

    // ── R_TV exponential mapping constants ────────────────────────────────
    // Maps Acl_dB → R_TV matching documented T-V range (8.2Ω to open)
    // Anchored: pos 6 (Acl=30dB) → R_TV=330Ω, pos 11 (Acl=50dB) → R_TV=8.2Ω
    static constexpr float kTVslope    = 0.1847f;  // ln(330/8.2) / 20
    static constexpr float kAclRefDB   = 30.0f;    // Anchor point (position 6)
    static constexpr float kRTVRef     = 330.0f;   // R_TV at anchor

public:
    Neve1063Path() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void configure(const Neve1063PathConfig& config)
    {
        config_ = config;
        configured_ = true;
    }

    void prepare(float sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;

        // ── BA184 N-V: Mic preamp (B112-type) ───────────────────────────

        // TR4: BC109 common-emitter input (low-noise)
        CEStageConfig ba184_4Cfg;
        ba184_4Cfg.bjt          = config_.ba184_tr4;
        ba184_4Cfg.R_collector  = config_.ba184_Rc_tr4;
        ba184_4Cfg.R_emitter    = config_.ba184_R_emitter;
        ba184_4Cfg.R_base_bias  = 100000.0f;
        ba184_4Cfg.C_input      = 0.0f;
        ba184_4Cfg.C_miller     = 0.0f;
        ba184_4Cfg.C_bypass     = 0.0f;
        ba184_4Cfg.Vcc          = config_.Vcc;
        ba184_tr4_.prepare(sampleRate, ba184_4Cfg);

        // TR5: BC107 common-emitter second stage (inverts → module non-inverting)
        CEStageConfig ba184_5Cfg;
        ba184_5Cfg.bjt          = config_.ba184_tr5;
        ba184_5Cfg.R_collector  = config_.ba184_Rc_tr5;
        ba184_5Cfg.R_emitter    = config_.ba184_Re_tr5;
        ba184_5Cfg.R_base_bias  = config_.ba184_Rc_tr4;  // Driven from TR4 collector
        ba184_5Cfg.C_input      = 0.0f;                  // DC-coupled
        ba184_5Cfg.C_miller     = 0.0f;
        ba184_5Cfg.C_bypass     = 0.0f;
        ba184_5Cfg.Vcc          = config_.Vcc;
        ba184_tr5_.prepare(sampleRate, ba184_5Cfg);

        // TR6: BC107 emitter follower output
        EFStageConfig ba184_6Cfg;
        ba184_6Cfg.bjt          = config_.ba184_tr6;
        ba184_6Cfg.R_bias       = 2200.0f;
        ba184_6Cfg.C_out        = 100e-6f;
        ba184_6Cfg.C_out_film   = 0.0f;
        ba184_6Cfg.R_series_out = 0.0f;
        ba184_6Cfg.Vcc          = config_.Vcc;
        ba184_tr6_.prepare(sampleRate, ba184_6Cfg);

        // ── BA183 N-V: Variable gain preamp ──────────────────────────────

        // TR4: BC109 common-emitter input (low-noise)
        // T-V calibration: Re_tr4 = ratio × (Re_tv ∥ R18) - 1/gm
        // so the WDF module gain matches the documented BA283 T-V table.
        const float Re_tv_init = kReTv * config_.nv_R18
                               / (kReTv + config_.nv_R18);
        const float Re_nv4_init = std::max(
            kNvCalRatio * Re_tv_init - kNvRe, 1.0f);

        CEStageConfig nv4Cfg;
        nv4Cfg.bjt          = config_.nv_tr4;
        nv4Cfg.R_collector  = config_.nv_Rc_tr4;
        nv4Cfg.R_emitter    = Re_nv4_init;   // T-V calibrated
        nv4Cfg.R_base_bias  = 100000.0f;
        nv4Cfg.C_input      = 10e-6f;            // Input coupling
        nv4Cfg.C_miller     = 0.0f;
        nv4Cfg.C_bypass     = 0.0f;
        nv4Cfg.Vcc          = config_.Vcc;
        nv_tr4_.prepare(sampleRate, nv4Cfg);

        // TR5: BC107 common-emitter second stage (inverts → module non-inverting)
        CEStageConfig nv5Cfg;
        nv5Cfg.bjt          = config_.nv_tr5;
        nv5Cfg.R_collector  = config_.nv_Rc_tr5;
        nv5Cfg.R_emitter    = config_.nv_Re_tr5;
        nv5Cfg.R_base_bias  = config_.nv_Rc_tr4;  // Driven from TR4 collector
        nv5Cfg.C_input      = 0.0f;               // DC-coupled
        nv5Cfg.C_miller     = 0.0f;
        nv5Cfg.C_bypass     = 0.0f;
        nv5Cfg.Vcc          = config_.Vcc;
        nv_tr5_.prepare(sampleRate, nv5Cfg);

        // TR6: BC107 emitter follower output
        EFStageConfig nv6Cfg;
        nv6Cfg.bjt          = config_.nv_tr6;
        nv6Cfg.R_bias       = 2200.0f;
        nv6Cfg.C_out        = 100e-6f;
        nv6Cfg.C_out_film   = 0.0f;
        nv6Cfg.R_series_out = 0.0f;
        nv6Cfg.Vcc          = config_.Vcc;
        nv_tr6_.prepare(sampleRate, nv6Cfg);

        // ── BA183 A-M: Class-A line amplifier ────────────────────────────

        // TR1: BC184C common-emitter input
        CEStageConfig am1Cfg;
        am1Cfg.bjt          = config_.am_tr1;
        am1Cfg.R_collector  = config_.am_Rc_tr1;
        am1Cfg.R_emitter    = config_.am_Re_tr1;
        am1Cfg.R_base_bias  = 47000.0f;
        am1Cfg.C_input      = 0.0f;
        am1Cfg.C_miller     = 100e-12f;
        am1Cfg.C_bypass     = 0.0f;
        am1Cfg.Vcc          = config_.Vcc;
        am_tr1_.prepare(sampleRate, am1Cfg);

        // TR2: BC184C common-emitter driver
        CEStageConfig am2Cfg;
        am2Cfg.bjt          = config_.am_tr2;
        am2Cfg.R_collector  = config_.am_Rc_tr2;
        am2Cfg.R_emitter    = config_.am_Re_tr2;
        am2Cfg.R_base_bias  = config_.am_Rc_tr1;
        am2Cfg.C_input      = 0.0f;
        am2Cfg.C_miller     = 0.0f;
        am2Cfg.C_bypass     = 0.0f;
        am2Cfg.Vcc          = config_.Vcc;
        am_tr2_.prepare(sampleRate, am2Cfg);

        // TR3: BDY61 common-emitter power output (~70mA Class-A)
        CEStageConfig am3Cfg;
        am3Cfg.bjt          = config_.am_tr3;
        am3Cfg.R_collector  = config_.am_Rc_tr3;
        am3Cfg.R_emitter    = config_.am_Re_output;
        am3Cfg.R_base_bias  = config_.am_Rc_tr2;
        am3Cfg.C_input      = 0.0f;
        am3Cfg.C_miller     = 0.0f;
        am3Cfg.C_bypass     = 0.0f;
        am3Cfg.Vcc          = config_.Vcc;
        am_tr3_.prepare(sampleRate, am3Cfg);

        // ── C4 AC feedback HP filter (BA183 A-M internal) ────────────────
        updateC4FeedbackCoefficient();

        // ── C4 feedback fraction ─────────────────────────────────────────
        // C4 wraps all 3 AM stages (TR1+TR2+TR3) to match documented
        // BA283 line stage gain of 15 dB (J-K open).
        // beta ≈ 1/Acl_target for high Aol.
        constexpr float kAmTargetGain = 5.623f;   // 10^(15/20) = 15 dB
        betaC4_ = 1.0f / kAmTargetGain;           // ≈ 0.178

        // ── Feedback network ─────────────────────────────────────────────
        updateFeedbackCoefficient();

        // ── DC servo coefficient (~0.1 Hz integrator) ────────────────────
        const float fc_servo_w = prewarpHz(0.1f, sampleRate_);
        servoAlpha_ = kTwoPif * fc_servo_w / sampleRate_;

        // ── DC settling ──────────────────────────────────────────────────
        c4FeedbackDC_ = 0.0f;
        y12Prev_      = 0.0f;
        vServo_       = 0.0f;
        servoLP_      = 0.0f;
        feedbackDC_   = 0.0f;
        outputPrev_   = 0.0f;

        // Phase 1: Open-loop CASCADE settling.
        // Stages process as a connected chain so TR3 gets base drive
        // from TR2's collector output — prevents cutoff bootstrap failure.
        for (int i = 0; i < 200; ++i)
        {
            forwardChainEval(0.0f);
        }

        // Phase 1b: Measure actual chain gain at quiescent operating point.
        // This captures real gm, WDF loading, EF attenuation — no approximations.
        designAol_ = measureChainGain();
        trackedAol_ = designAol_;

        // Phase 2: Switch to slow DC tracking before closed-loop
        ba184_tr4_.skipFastSettle();
        ba184_tr5_.skipFastSettle();
        nv_tr4_.skipFastSettle();
        nv_tr5_.skipFastSettle();
        am_tr1_.skipFastSettle();
        am_tr2_.skipFastSettle();
        am_tr3_.skipFastSettle();

        // Phase 3: Closed-loop settling (analytical gain, no source stepping)
        vServo_    = 0.0f;
        servoLP_   = 0.0f;
        feedbackDC_ = 0.0f;
        outputPrev_ = 0.0f;

        constexpr int kSettleSamples = 500;
        for (int i = 0; i < kSettleSamples; ++i)
            processSample(0.0f);
    }

    void reset() override
    {
        // 1. Reset all WDF stages (each runs 64-sample internal warmup)
        ba184_tr4_.reset();
        ba184_tr5_.reset();
        ba184_tr6_.reset();
        nv_tr4_.reset();
        nv_tr5_.reset();
        nv_tr6_.reset();
        am_tr1_.reset();
        am_tr2_.reset();
        am_tr3_.reset();

        // 2. Clear feedback state
        c4FeedbackDC_ = 0.0f;
        y12Prev_      = 0.0f;

        // 3. Open-loop CASCADE settling
        for (int i = 0; i < 200; ++i)
        {
            forwardChainEval(0.0f);
        }

        // 3b. Measure actual chain gain at quiescent
        designAol_ = measureChainGain();
        trackedAol_ = designAol_;

        // 4. Switch to slow DC tracking
        ba184_tr4_.skipFastSettle();
        ba184_tr5_.skipFastSettle();
        nv_tr4_.skipFastSettle();
        nv_tr5_.skipFastSettle();
        am_tr1_.skipFastSettle();
        am_tr2_.skipFastSettle();
        am_tr3_.skipFastSettle();

        // 5. Closed-loop settling
        vServo_     = 0.0f;
        servoLP_    = 0.0f;
        feedbackDC_ = 0.0f;
        outputPrev_ = 0.0f;

        constexpr int kSettleSamples = 500;
        for (int i = 0; i < kSettleSamples; ++i)
            processSample(0.0f);
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    float processSample(float input) override
    {
        // Error-signal drive: compute the signal the chain would see in a
        // closed-loop system, feed it to the chain, use output directly.
        //
        // In a real feedback amp: error = input / (1 + beta*Aol)
        // Chain output = Aol * error ≈ Acl * input (closed-loop gain)
        //
        // This keeps all 9 BJTs at realistic signal levels (no saturation).

        const float beta = Rg_ / (Rfb_ + Rg_);
        const float Aol = designAol_;
        const float errorSignal = input / (1.0f + beta * Aol);

        // Drive the chain at error-signal level
        const float chainOut = forwardChainEval(errorSignal);

        // chainOut ≈ Aol × errorSignal ≈ Acl × input
        float output = chainOut;
        output = std::clamp(output, -config_.Vcc, config_.Vcc);

        // 4. DC servo (sub-Hz integrator)
        servoLP_ += servoAlpha_ * (output - servoLP_);
        vServo_  += servoAlpha_ * servoLP_;
        vServo_   = std::clamp(vServo_, -1.0f, 1.0f);

        // 5. Output coupling HP filter
        feedbackDC_ += feedbackAlpha_ * (output - feedbackDC_);
        output -= feedbackDC_;

        outputPrev_ = output;
        return output;
    }

    // ── Gain control ──────────────────────────────────────────────────────────

    void setGain(float Rfb) override
    {
        Rfb_ = std::max(Rfb, 1.0f);

        // ── Map Rfb → R_TV (T-V external resistor) ──────────────────────
        // Exponential mapping anchored at position 6 (Acl=30dB, R_TV=330Ω)
        // and calibrated to reach R_TV=8.2Ω at Acl=50dB (position 11).
        // At low Acl: R_TV → ∞ (T-V open), Re_eff → Re_int (18 dB module)
        // At high Acl: R_TV → 8.2Ω (48 dB module)
        const float Acl_new = 1.0f + Rfb_ / Rg_;
        const float Acl_dB  = 20.0f * std::log10(Acl_new);
        const float R_TV = kRTVRef
                         * std::exp(-kTVslope * (Acl_dB - kAclRefDB));

        // ── T-V calibrated emitter resistance ────────────────────────────
        // Re_tr4 = (K_wdf/K_tv) × (Re_tv ∥ R_TV) - 1/gm
        const float Re_tv_eff = kReTv * R_TV / (kReTv + R_TV);
        const float Re_eff = std::max(
            kNvCalRatio * Re_tv_eff - kNvRe, 1.0f);
        nv_tr4_.setEmitterResistance(Re_eff);

        updateFeedbackCoefficient();

        // Re-measure chain gain with new R18 (stages still at quiescent)
        designAol_ = measureChainGain();
        trackedAol_ = designAol_;

        // DIAGNOSTIC: print gain decomposition at the new Rfb setting
        std::printf("[Neve1063] setGain(Rfb=%.0f) Re_eff=%.1f -> designAol=%.1f  "
                    "Acl=%.1f  beta=%.6f  1+bA=%.1f\n",
                    Rfb_, Re_eff, designAol_,
                    designAol_ / (1.0f + (Rg_/(Rfb_+Rg_)) * designAol_),
                    Rg_/(Rfb_+Rg_),
                    1.0f + (Rg_/(Rfb_+Rg_)) * designAol_);
    }

    // ── Monitoring ────────────────────────────────────────────────────────────

    float getOutputImpedance() const override
    {
        const float rce = config_.am_tr3.Vaf / (config_.am_Ic_q_tr3 + 1e-6f);
        const float Rc = config_.am_Rc_tr3;
        return (Rc * rce) / (Rc + rce);
    }

    const char* getName() const override { return "Neve 1063"; }

    float getClosedLoopGain() const { return 1.0f + Rfb_ / Rg_; }

    float getClosedLoopGainDB() const
    {
        return 20.0f * std::log10(getClosedLoopGain());
    }

    float getRfb() const { return Rfb_; }

    float getOpenLoopGain() const
    {
        // BA184: feedforward (CE × CE, no local feedback)
        const float A184 = std::abs(
            ba184_tr4_.getGainInstantaneous()
          * ba184_tr5_.getGainInstantaneous());

        // NV: feedforward (no S→U feedback — S-U is input network, not fb)
        const float AnvOL = std::abs(
            nv_tr4_.getGainInstantaneous()
          * nv_tr5_.getGainInstantaneous());

        // AM: C4 wraps TR1+TR2+TR3 (3 stages, beta=0.178 for 15 dB)
        const float A123 = std::abs(
            am_tr1_.getGainInstantaneous()
          * am_tr2_.getGainInstantaneous()
          * am_tr3_.getGainInstantaneous());
        const float Acl123 = A123 / (1.0f + betaC4_ * A123);

        return std::max(A184 * AnvOL * Acl123, 1.0f);
    }

    struct OperatingPoint
    {
        float Vce_184_4 = 0.0f, Ic_184_4 = 0.0f;
        float Vce_nv4 = 0.0f, Ic_nv4  = 0.0f;
        float Vce_am1 = 0.0f, Ic_am1  = 0.0f;
        float Vce_am2 = 0.0f, Ic_am2  = 0.0f;
        float Vce_am3 = 0.0f, Ic_am3  = 0.0f;
    };

    OperatingPoint getOperatingPoint() const
    {
        OperatingPoint op;
        op.Vce_184_4 = ba184_tr4_.getVce();
        op.Ic_184_4  = ba184_tr4_.getIc();
        op.Vce_nv4 = nv_tr4_.getVce();
        op.Ic_nv4  = nv_tr4_.getIc();
        op.Vce_am1 = am_tr1_.getVce();
        op.Ic_am1  = am_tr1_.getIc();
        op.Vce_am2 = am_tr2_.getVce();
        op.Ic_am2  = am_tr2_.getIc();
        op.Vce_am3 = am_tr3_.getVce();
        op.Ic_am3  = am_tr3_.getIc();
        return op;
    }

private:
    // ── BA184 N-V stages ──────────────────────────────────────────────────────
    CEStageWDF<BJTLeaf> ba184_tr4_;         // BC109 CE input
    CEStageWDF<BJTLeaf> ba184_tr5_;         // BC107 CE second stage
    EFStageWDF<BJTLeaf> ba184_tr6_;         // BC107 EF output

    // ── BA183 N-V stages ──────────────────────────────────────────────────────
    CEStageWDF<BJTLeaf> nv_tr4_;            // BC109 CE input
    CEStageWDF<BJTLeaf> nv_tr5_;            // BC107 CE second stage
    EFStageWDF<BJTLeaf> nv_tr6_;            // BC107 EF output

    // ── BA183 A-M stages ──────────────────────────────────────────────────────
    CEStageWDF<BJTLeaf> am_tr1_;            // BC184C CE input
    CEStageWDF<BJTLeaf> am_tr2_;            // BC184C CE driver
    CEStageWDF<BJTLeaf> am_tr3_;            // BDY61 CE power output

    // ── Feedback network ──────────────────────────────────────────────────────
    float Rfb_            = 1430.0f;
    float Rg_             = 47.0f;

    // C4 feedback (AM local Newton)
    float c4FeedbackDC_   = 0.0f;
    float c4Alpha_        = 0.0f;
    float betaC4_         = 0.054f;
    float y12Prev_        = 0.0f;

    // DC servo and output coupling
    float vServo_         = 0.0f;
    float servoLP_        = 0.0f;
    float servoAlpha_     = 0.0f;
    float feedbackDC_     = 0.0f;
    float feedbackAlpha_  = 0.0f;
    float outputPrev_     = 0.0f;
    float designAol_      = 1000.0f;
    float trackedAol_     = 1000.0f;

    // ── Configuration ─────────────────────────────────────────────────────────
    Neve1063PathConfig config_;
    float sampleRate_ = 44100.0f;
    bool  configured_ = false;

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Run the full BA184(ff) + NV(local Newton) + AM(C4 Newton) + TR3 chain.
    /// @param externalInput  Signal from T1 secondary [V]
    /// @return               TR3 output voltage [V]
    float forwardChainEval(float externalInput)
    {
        // ── BA184 N-V (feedforward: CE + CE + EF) ────────────────────────
        const float ba184_in = externalInput;
        const float v184_4 = ba184_tr4_.processSample(ba184_in);
        const float v184_5 = ba184_tr5_.processSample(v184_4);
        const float v184_6 = ba184_tr6_.processSample(v184_5);

        // ── BA183 N-V (feedforward: CE + CE + EF) ────────────────────────
        const float nv_in = v184_6;
        const float v4 = nv_tr4_.processSample(nv_in);
        const float v5 = nv_tr5_.processSample(v4);
        const float v6 = nv_tr6_.processSample(v5);
        // NV output feeds AM input (pure feedforward)

        // ── BA183 A-M: C4 Newton around TR1+TR2+TR3 ─────────────────────
        // C4 wraps all 3 stages to match documented 15 dB gain (J-K open).
        // Chain = 3 CE stages = inverting → feedback uses + sign.
        // J = 1 + beta × |A123| (always positive, guaranteed convergence).
        const float am_in = v6;
        const auto s1 = am_tr1_;
        const auto s2 = am_tr2_;
        const auto s3 = am_tr3_;

        const double A1 = std::abs(static_cast<double>(
                              am_tr1_.getGainInstantaneous()));
        const double A2 = std::abs(static_cast<double>(
                              am_tr2_.getGainInstantaneous()));
        const double A3 = std::abs(static_cast<double>(
                              am_tr3_.getGainInstantaneous()));
        double Aol123 = std::clamp(A1 * A2 * A3, 1.0, 1000000.0);
        double Jc4 = std::clamp(
            1.0 + static_cast<double>(betaC4_) * Aol123, 1.5, 100000.0);

        // C4 Newton iterations — closed-loop predictor initialization.
        // For the inverting 3-stage chain: y_eq ≈ -am_in / beta.
        // This prevents Newton divergence when am_in × Aol >> Vcc
        // (the chain would saturate if probed from a naive warm-start).
        double y123 = -static_cast<double>(am_in)
                    / static_cast<double>(betaC4_);
        y123 = std::clamp(y123,
            -static_cast<double>(config_.Vcc),
             static_cast<double>(config_.Vcc));
        for (int c4iter = 0; c4iter < 3; ++c4iter)
        {
            am_tr1_ = s1;  am_tr2_ = s2;  am_tr3_ = s3;
            float ac = static_cast<float>(y123) - c4FeedbackDC_;
            float fb = static_cast<float>(
                static_cast<double>(betaC4_) * static_cast<double>(ac));
            // + fb: chain is inverting (3 CE), so additive fb = negative feedback
            float v1 = am_tr1_.processSample(am_in + fb);
            float v2 = am_tr2_.processSample(v1);
            float v3 = am_tr3_.processSample(v2);
            double g = y123 - static_cast<double>(v3);
            y123 = y123 - g / Jc4;
            y123 = std::clamp(y123,
                -static_cast<double>(config_.Vcc),
                 static_cast<double>(config_.Vcc));
        }

        // C4 commit
        am_tr1_ = s1;  am_tr2_ = s2;  am_tr3_ = s3;
        {
            float ac = static_cast<float>(y123) - c4FeedbackDC_;
            float fb = static_cast<float>(
                static_cast<double>(betaC4_) * static_cast<double>(ac));
            float v1 = am_tr1_.processSample(am_in + fb);
            float v2 = am_tr2_.processSample(v1);
            am_tr3_.processSample(v2);
        }

        // C4 state update (y123 is now TR3 output, not TR2)
        float y123f = static_cast<float>(y123);
        c4FeedbackDC_ += c4Alpha_ * (y123f - c4FeedbackDC_);
        y12Prev_ = y123f;

        return y123f;
    }

    /// Measure actual chain gain by perturbation at the current quiescent point.
    /// Uses symmetric two-sided probe to cancel DC offset and even-order effects.
    /// Must be called when stages are at quiescent (after settling, before audio).
    float measureChainGain()
    {
        // Save all stage states
        const auto s184_4 = ba184_tr4_;
        const auto s184_5 = ba184_tr5_;
        const auto s184_6 = ba184_tr6_;
        const auto snv4   = nv_tr4_;
        const auto snv5   = nv_tr5_;
        const auto snv6   = nv_tr6_;
        const auto sam1   = am_tr1_;
        const auto sam2   = am_tr2_;
        const auto sam3   = am_tr3_;
        const float sc4dc = c4FeedbackDC_;
        const float sy12  = y12Prev_;

        // Positive probe
        constexpr float delta = 1e-5f;
        const float outP = forwardChainEval(delta);

        // Restore state for negative probe
        ba184_tr4_ = s184_4; ba184_tr5_ = s184_5; ba184_tr6_ = s184_6;
        nv_tr4_ = snv4; nv_tr5_ = snv5; nv_tr6_ = snv6;
        am_tr1_ = sam1; am_tr2_ = sam2; am_tr3_ = sam3;
        c4FeedbackDC_ = sc4dc; y12Prev_ = sy12;

        // Negative probe
        const float outN = forwardChainEval(-delta);

        // Restore state completely
        ba184_tr4_ = s184_4; ba184_tr5_ = s184_5; ba184_tr6_ = s184_6;
        nv_tr4_ = snv4; nv_tr5_ = snv5; nv_tr6_ = snv6;
        am_tr1_ = sam1; am_tr2_ = sam2; am_tr3_ = sam3;
        c4FeedbackDC_ = sc4dc; y12Prev_ = sy12;

        // Symmetric derivative: Aol = (outP - outN) / (2 * delta)
        const float measuredAol = std::abs((outP - outN) / (2.0f * delta));
        return std::max(measuredAol, 1.0f);
    }

    void updateC4FeedbackCoefficient()
    {
        const float C4 = config_.am_C4;
        if (C4 <= 0.0f) { c4Alpha_ = 0.0f; return; }
        const float R_eff = config_.am_Rc_tr2;
        const float f_hp = 1.0f / (kTwoPif * C4 * R_eff);
        const float f_hp_w = prewarpHz(f_hp, sampleRate_);
        const float omega = kTwoPif * f_hp_w / sampleRate_;
        c4Alpha_ = omega / (1.0f + omega);
    }

    void updateFeedbackCoefficient()
    {
        const float C_out = 220e-6f;
        const float f_hp = 1.0f / (kTwoPif * C_out * Rfb_);
        const float f_hp_w = prewarpHz(f_hp, sampleRate_);
        const float omega = kTwoPif * f_hp_w / sampleRate_;
        feedbackAlpha_ = omega / (1.0f + omega);
    }
};

} // namespace transfo
