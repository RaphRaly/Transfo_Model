#pragma once

// =============================================================================
// CEStageWDF — Generic Common-Emitter WDF amplifier stage.
//
// Reusable for both Q1 (BC184C NPN) and Q2 (BC214C PNP) in the Neve Heritage
// Class-A path (ANALYSE_ET_DESIGN_Rev2.md section 3.2).
//
// Circuit topology:
//
//                              Vcc (+24V for NPN bias, -24V supply for PNP)
//                               |
//                          R_collector
//                               |
//                               |C
// Signal_in --[C_input]--+-- Q Base ---- Collector_out -> next stage
//                        |      |E
//                   R_base_bias |
//                        |   R_emitter_dc
//                       GND     |
//                          [C_bypass] -- R_emitter_ac -- GND
//
// WDF Implementation Strategy:
//
// The CE stage uses a companion source approach:
//   - The WDF port sees the base-emitter junction (via BJTLeaf)
//   - An optional input coupling capacitor (AdaptedCapacitor) sits in series
//     before the BJT, connected through a WDFSeriesAdaptor
//   - The collector current from BJTLeaf computes the collector voltage drop:
//       NPN: Vc = +Vcc - Ic * R_collector
//       PNP: Vc = -Vcc + |Ic| * R_collector
//   - The output voltage is the AC component of the collector voltage
//   - Gain is approximately -gm * R_collector (inverting for CE)
//
// This is the standard approach for WDF BJT modeling in single-ended
// configurations, avoiding the complexity of a full multi-port BJT model.
//
// Template parameter: NonlinearLeaf (default BJTLeaf) — the CRTP WDOnePort
// element that models the BE junction via Newton-Raphson.
//
// Pattern: CRTP-composing WDF sub-circuit (header-only).
//
// Reference: Ebers-Moll 1954; ANALYSE_ET_DESIGN_Rev2.md section 3.2;
//            chowdsp_wdf DiodeT.h (same companion source pattern)
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include "../wdf/AdaptedElements.h"
#include "../wdf/BJTLeaf.h"
#include <algorithm>
#include <cmath>

namespace transfo {

// ── Configuration ────────────────────────────────────────────────────────────

struct CEStageConfig
{
    BJTParams bjt;
    float R_collector   = 15000.0f;   // Collector load [Ohm]
    float R_emitter     = 47.0f;      // Total DC emitter resistance [Ohm]
    float R_base_bias   = 50000.0f;   // Thevenin base bias resistance [Ohm]
    float C_input       = 0.0f;       // Input coupling cap [F] (0 = DC coupled)
    float C_miller      = 0.0f;       // Miller compensation [F] (0 = none)
    float C_bypass      = 0.0f;       // Emitter bypass cap [F] (0 = none)
    float Vcc           = 24.0f;      // Supply voltage [V] (magnitude, positive)

    bool isValid() const
    {
        return bjt.isValid()
            && R_collector > 0.0f
            && R_emitter >= 0.0f
            && R_base_bias > 0.0f
            && C_input >= 0.0f
            && C_miller >= 0.0f
            && C_bypass >= 0.0f
            && Vcc > 0.0f;
    }
};

// ── CE Stage WDF ─────────────────────────────────────────────────────────────

template <typename NonlinearLeaf = BJTLeaf>
class CEStageWDF
{
public:
    CEStageWDF() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the CE stage from config and sample rate.
    /// Sets up the WDF sub-circuit: optional input coupling cap in series
    /// with the base bias resistor, feeding the BJTLeaf.
    void prepare(float sampleRate, const CEStageConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;
        Ts_         = 1.0f / sampleRate;

        // Polarity: +1 for NPN, -1 for PNP
        sign_ = config.bjt.polaritySign();

        // ── Configure BJTLeaf ────────────────────────────────────────────────
        bjtLeaf_.configure(config.bjt);
        bjtLeaf_.prepare(sampleRate);

        // ── Input coupling capacitor (optional) ──────────────────────────────
        hasCouplingCap_ = (config.C_input > 0.0f);
        if (hasCouplingCap_)
        {
            couplingCap_.setCapacitance(config.C_input, Ts_);
        }

        // ── Miller capacitor filter ──────────────────────────────────────────
        // Modeled as a first-order lowpass on the collector output.
        // fc = 1 / (2*pi * Rc * Cmiller), alpha = 1 / (1 + 2*pi*fc*Ts)
        hasMillerCap_ = (config.C_miller > 0.0f);
        if (hasMillerCap_)
        {
            const float fc = 1.0f / (kTwoPif * config.R_collector * config.C_miller);
            millerAlpha_ = 1.0f / (1.0f + kTwoPif * fc * Ts_);
        }

        // ── Emitter bypass cap filter ────────────────────────────────────────
        // Models the emitter bypass as a frequency-dependent degeneration.
        // At DC: full R_emitter degeneration. At HF: bypassed by C_bypass.
        // Effective R_emitter(f) = R_emitter / (1 + j*w*C_bypass*R_emitter)
        // Implemented as a simple one-pole HPF on the emitter degeneration.
        hasBypassCap_ = (config.C_bypass > 0.0f && config.R_emitter > 0.0f);
        if (hasBypassCap_)
        {
            // Time constant tau = R_emitter * C_bypass
            // alpha_bypass = exp(-Ts / tau) for a discrete first-order filter
            const float tau = config.R_emitter * config.C_bypass;
            bypassAlpha_ = std::exp(-Ts_ / tau);
        }

        // ── DC bias for forward-active operation ──────────────────────────────
        // Target collector at mid-supply: Ic_target = Vcc / (2 * Rc).
        // Compute the Vbe needed to achieve this quiescent current.
        // At WDF steady state, Vbe converges to baseDrive, so adding
        // V_bias_base_ to baseDrive naturally forward-biases the junction.
        const float Ic_target = config.Vcc / (2.0f * config.R_collector);
        V_bias_base_ = sign_ * config.bjt.Vt
                      * std::log(Ic_target / config.bjt.Is + 1.0f);

        // Initialize DC tracking to the quiescent collector voltage
        Vc_quiescent_ = sign_ * config.Vcc
                       - sign_ * Ic_target * config.R_collector;
        Vc_dc_   = Vc_quiescent_;
        Vc_last_ = Vc_quiescent_;

        reset();
    }

    /// Clear all reactive element states and NR warm-starts.
    void reset()
    {
        bjtLeaf_.reset();

        if (hasCouplingCap_)
            couplingCap_.reset();

        millerState_   = Vc_quiescent_;
        bypassState_   = 0.0f;
        Vc_dc_         = Vc_quiescent_;
        Vc_last_       = Vc_quiescent_;
        outputVoltage_ = 0.0f;
        dcSettleCount_ = 0;

        // Warm up: run zero-input samples so the BJT NR solver converges
        // at the bias point, eliminating the initial transient that would
        // otherwise be amplified by the high open-loop gain.
        for (int i = 0; i < 32; ++i)
            processSample(0.0f);

        // Re-initialize DC tracker to the converged collector voltage
        Vc_dc_       = Vc_last_;
        millerState_ = Vc_last_;
        outputVoltage_ = 0.0f;
        dcSettleCount_ = 0;
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the CE stage.
    ///
    /// Algorithm:
    ///   1. Input coupling (if C_input > 0): pass through adapted capacitor
    ///   2. Compute effective base drive voltage
    ///   3. Drive BJTLeaf: scatter input through BE junction
    ///   4. Read Ic from BJTLeaf companion model
    ///   5. Compute Vc = Vcc_sign - Ic * R_collector
    ///   6. Apply emitter bypass degeneration modulation
    ///   7. Apply Miller cap (first-order lowpass on collector)
    ///   8. Output = AC component of collector voltage
    ///
    /// @param input  Voltage from previous stage [V]
    /// @return       Collector voltage (AC component) [V]
    float processSample(float input)
    {
        float baseDrive = input;

        // ── Step 1: Input coupling capacitor ─────────────────────────────────
        // AC-couple the input: the cap blocks DC and passes audio.
        if (hasCouplingCap_)
        {
            const float b_cap_prev = couplingCap_.getReflectedWaveFromState();
            const float a_cap = 2.0f * input - b_cap_prev;
            const float b_cap = couplingCap_.scatter(a_cap);
            baseDrive = (a_cap + b_cap) * 0.5f;
        }

        // ── Step 2: Add DC bias for forward-active operation ─────────────────
        // The bias voltage forward-biases the BE junction at the target
        // quiescent current. At WDF steady state, Vbe → baseDrive.
        baseDrive += V_bias_base_;

        // ── Step 3: Drive BJTLeaf (BE junction NR solve) ─────────────────────
        const float b_prev = bjtLeaf_.getReflectedWave();
        const float a_bjt = 2.0f * baseDrive - b_prev;
        bjtLeaf_.scatter(a_bjt);

        // ── Step 4: Read collector current from companion model ──────────────
        const float Ic = bjtLeaf_.getCollectorCurrent();

        // ── Step 5: Compute collector voltage ────────────────────────────────
        // NPN: Vc = +Vcc - Ic * Rc  (Ic positive in forward-active)
        // PNP: Vc = -Vcc + |Ic| * Rc  (Ic negative for PNP)
        //
        // Unified: Vc = sign * Vcc - Ic * Rc
        //   NPN: sign=+1, Ic>0 => Vc = Vcc - Ic*Rc  (correct)
        //   PNP: sign=-1, Ic<0 => Vc = -Vcc - (-|Ic|)*Rc = -Vcc + |Ic|*Rc (correct)
        float Vc = sign_ * config_.Vcc - Ic * config_.R_collector;

        // Clamp collector voltage to supply rail limits.
        // In a real circuit, Vc cannot exceed the supply rails.
        // NPN: Vc in [~0, Vcc], PNP: Vc in [-Vcc, ~0].
        // We use ±Vcc as a conservative clamp.
        Vc = std::clamp(Vc, -config_.Vcc, config_.Vcc);

        // ── Step 5b: Emitter degeneration ──────────────────────────────────
        // Models the gain reduction from R_emitter in the signal path.
        // Av = -gm * Rc / (1 + gm * Re) = -Rc / (Re + 1/gm)
        // Applied as a scaling factor on the AC component of Vc.
        // Only when no bypass cap handles frequency-dependent degeneration.
        if (config_.R_emitter > 0.0f && !hasBypassCap_)
        {
            const float gm = bjtLeaf_.getGm();
            if (gm > kEpsilonF)
            {
                const float degen = 1.0f / (1.0f + gm * config_.R_emitter);
                const float Vc_ac = Vc - Vc_dc_;
                Vc = Vc_dc_ + Vc_ac * degen;
            }
        }

        // ── Step 6: Emitter bypass degeneration ──────────────────────────────
        // At low frequencies, the emitter resistor reduces gain.
        // The bypass cap shorts R_emitter at HF, restoring full gain.
        // Model as a frequency-dependent gain modifier:
        //   effective_Re = R_emitter * (1 - bypass_filter_output)
        // where bypass_filter_output is a HPF that goes to 1 at HF.
        if (hasBypassCap_)
        {
            // First-order highpass on the AC component of Ic:
            //   HPF: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
            // This modulates the effective emitter degeneration.
            // For simplicity, we apply it as a gain correction on Vc:
            //   At DC: full degeneration, gain = -Rc / (Re + rbe/gm)
            //   At HF: Re bypassed, gain = -gm * Rc
            const float gm = bjtLeaf_.getGm();
            const float Re = config_.R_emitter;
            if (gm > kEpsilonF)
            {
                // Bypass filter: tracks the AC fraction of emitter current
                const float Ic_ac = Ic - bypassState_;
                bypassState_ = bypassAlpha_ * bypassState_
                              + (1.0f - bypassAlpha_) * Ic;

                // Effective gain modulation from emitter degeneration:
                // gain_full = -gm * Rc (no degeneration)
                // gain_degen = -Rc / (Re + 1/gm) (full degeneration)
                // The bypass cap interpolates between these based on frequency.
                // We apply the degeneration as a voltage correction on Vc:
                const float V_emitter_degen = bypassState_ * Re;
                Vc -= sign_ * V_emitter_degen;
            }
        }

        // ── Step 7: Miller capacitor (first-order lowpass) ───────────────────
        // y[n] = y[n-1] + alpha * (x[n] - y[n-1])
        if (hasMillerCap_)
        {
            millerState_ += millerAlpha_ * (Vc - millerState_);
            Vc = millerState_;
        }

        // ── Step 8: Output = AC component of collector voltage ───────────────
        // Subtract DC operating point for a cleaner signal.
        // The DC estimate tracks slowly to handle bias drift.
        if (dcSettleCount_ < kDCSettleSamples)
        {
            // During initial settling, track DC rapidly
            Vc_dc_ += 0.01f * (Vc - Vc_dc_);
            ++dcSettleCount_;
        }
        else
        {
            // Steady state: very slow DC tracking (sub-Hz)
            // alpha_dc = 1 / (1 + 2*pi*f_dc*Ts), f_dc ~ 0.1 Hz
            constexpr float kDCTrackAlpha = 0.0001f;
            Vc_dc_ += kDCTrackAlpha * (Vc - Vc_dc_);
        }

        Vc_last_ = Vc;
        outputVoltage_ = Vc - Vc_dc_;

        return outputVoltage_;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring / diagnostics ─────────────────────────────────────────────

    /// Collector-emitter voltage [V] at the current operating point.
    /// Vce = Vc - Ve, where Ve ~ sign * (Vbe + Ic*Re)
    float getVce() const
    {
        const float Ve = bjtLeaf_.getVbe() + bjtLeaf_.getIc() * config_.R_emitter;
        return Vc_last_ - Ve;
    }

    /// Collector current [A] at the current operating point.
    float getIc() const
    {
        return bjtLeaf_.getIc();
    }

    /// Instantaneous small-signal voltage gain at the current operating point.
    /// For CE stage: Av = -gm * Rc (inverting)
    /// With emitter degeneration: Av = -Rc / (Re + 1/gm)
    float getGainInstantaneous() const
    {
        const float gm = bjtLeaf_.getGm();
        if (gm < kEpsilonF)
            return 0.0f;

        if (hasBypassCap_)
        {
            // At the instantaneous frequency, the effective Re varies.
            // Return the fully-bypassed gain as the "instantaneous" value,
            // since the bypass cap is effective at audio frequencies.
            return -gm * config_.R_collector;
        }

        // With fixed emitter degeneration:
        const float Re = config_.R_emitter;
        return -config_.R_collector / (Re + 1.0f / gm);
    }

    /// Last computed collector voltage (full DC + AC) [V].
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
    float getVbe() const
    {
        return bjtLeaf_.getVbe();
    }

    /// Transconductance gm [S] at the current operating point.
    float getGm() const
    {
        return bjtLeaf_.getGm();
    }

    /// NR iteration count from the last sample.
    int getLastIterCount() const
    {
        return bjtLeaf_.getLastIterCount();
    }

    /// Access the underlying BJTLeaf for advanced diagnostics.
    const NonlinearLeaf& getBJTLeaf() const { return bjtLeaf_; }
    NonlinearLeaf& getBJTLeaf() { return bjtLeaf_; }

    /// Access the current configuration.
    const CEStageConfig& getConfig() const { return config_; }

private:
    // ── WDF elements ─────────────────────────────────────────────────────────
    NonlinearLeaf     bjtLeaf_;          // BE junction (nonlinear, NR-solved)
    AdaptedCapacitor  couplingCap_;      // Input coupling cap (optional)

    // ── Configuration ────────────────────────────────────────────────────────
    CEStageConfig config_;
    float sampleRate_ = 44100.0f;
    float Ts_         = 1.0f / 44100.0f;
    float sign_       = 1.0f;           // +1 NPN, -1 PNP

    // ── Flags ────────────────────────────────────────────────────────────────
    bool hasCouplingCap_ = false;
    bool hasMillerCap_   = false;
    bool hasBypassCap_   = false;

    // ── Miller cap filter state ──────────────────────────────────────────────
    float millerAlpha_ = 0.0f;          // Lowpass coefficient
    float millerState_ = 0.0f;          // y[n-1]

    // ── Emitter bypass filter state ──────────────────────────────────────────
    float bypassAlpha_ = 0.0f;          // Exponential decay coefficient
    float bypassState_ = 0.0f;          // Filtered Ic (DC tracking)

    // ── DC bias ────────────────────────────────────────────────────────────
    float V_bias_base_    = 0.0f;       // DC bias voltage for forward-active
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
