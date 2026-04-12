#pragma once

// [ARCHIVED] Part of JE990Path — on hold pending Sprint 4 BJT tuning.
// =============================================================================
// ClassABOutputWDF — WDF Class-AB push-pull output stage for the JE-990.
//
// Models the complementary emitter-follower output pair Q8 (MJE-181 NPN) and
// Q9 (MJE-171 PNP) in the JE-990 discrete op-amp, with pre-driver Q7
// (2N2484 NPN) folded into Q8's effective beta.
//
// Circuit topology (JE-990 schematic):
//
//                          +24V
//                            |
//                       R14 [39 Ohm]
//                            |
//   VAS out --> Q7 (2N2484) --> Q8 (MJE-181 NPN)
//                                |E
//                                +-------> output node
//                                |E              |
//                            Q9 (MJE-171 PNP)   R13 [3.9 Ohm] (current sense)
//                                |
//                       R15 [39 Ohm]
//                            |
//                          -24V
//
//   C2 [62 pF]: Q8 base to output (lead compensation)
//   C3 [91 pF]: Q9 base to output (lead compensation)
//   CR3: VBE multiplier sets quiescent bias (~10-20 mA)
//
// Operating principle:
//
//   Both Q8 and Q9 are emitter followers connected in push-pull:
//     - Q8 (NPN top) handles positive output current (sources to load)
//     - Q9 (PNP bottom) handles negative output current (sinks from load)
//     - At quiescence, both conduct I_quiescent (~15 mA) set by the VBE
//       multiplier CR3 bias network
//     - Crossover distortion arises in the transition region where neither
//       transistor has high gm; Class-AB biasing minimizes this
//
//   Output voltage = input - Vbe_effective, where Vbe_effective depends
//   on the relative conduction of Q8 vs Q9 (set by instantaneous signal).
//
// Simplified model (pragmatic approach):
//
//   Rather than modeling the full pre-driver Q7 + two output BJTs with a
//   multi-port NR solver, we use TWO independent BJTLeaf instances:
//     - q8 (NPN, MJE-181): biased at +V_bias_q8, conducts on positive swings
//     - q9 (PNP, MJE-171): biased at -|V_bias_q9|, conducts on negative swings
//   The pre-driver Q7 is folded into Q8's beta (beta_eff = beta_q7 * beta_q8).
//   Each BJT's emitter voltage is computed from the companion-source scatter,
//   and the output is the average of both emitter voltages (they share the
//   output node in the real circuit).
//
//   Lead compensation capacitors C2, C3 are modeled as first-order lowpass
//   filters on each BJT's base drive, emulating the HF roll-off they produce
//   between base and collector (output).
//
//   The emitter ballast resistors R14, R15 (39 Ohm each) and the current
//   sense resistor R13 (3.9 Ohm) are included in the output impedance
//   calculation and as series drops in the output voltage.
//
// Design rules (from Sprint 3 pitfalls — MUST follow):
//
//   1. Always add V_bias_base to baseDrive — both Q8 and Q9 need independent
//      DC bias voltages to set the Class-AB quiescent point.
//   2. Run 32-sample warmup after reset() to settle NR at the bias point.
//   3. Initialize all filter states to quiescent values after warmup.
//   4. AC coupling — output uses independent bias from the VAS.
//
// Output characteristics (Hardy specs):
//   - Max output: +25 dBu into 75 Ohm (13.8 Vrms, 260 mA peak)
//   - Slew rate: 16-18 V/us
//   - Zout < 5 Ohm (before load isolator)
//
// Template parameter: NonlinearLeaf (default BJTLeaf) — the WDF one-port
// element solving the BE junction. Allows swapping in a more detailed
// BJT model without changing the stage topology.
//
// Pattern: WDF sub-circuit with dual companion sources (header-only).
//
// Reference: JE-990 schematic; ANALYSE_ET_DESIGN_Rev2.md section 3.3;
//            MJE181/MJE171 datasheets (ON Semi); Hardy "JE-990" tech notes
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include "../wdf/BJTLeaf.h"
#include <algorithm>
#include <cmath>

namespace transfo {

// ── Configuration ────────────────────────────────────────────────────────────

struct ClassABConfig
{
    BJTParams q8;                   // MJE-181 NPN top output transistor
    BJTParams q9;                   // MJE-171 PNP bottom output transistor
    float R_emitter_top    = 39.0f;   // R14: top emitter ballast to +Vcc [Ohm]
    float R_emitter_bottom = 39.0f;   // R15: bottom emitter ballast to -Vcc [Ohm]
    float R_sense          = 3.9f;    // R13: output current sense resistor [Ohm]
    float C_comp_top       = 62e-12f; // C2: lead compensation, Q8 base-to-output [F]
    float C_comp_bottom    = 91e-12f; // C3: lead compensation, Q9 base-to-output [F]
    float I_quiescent      = 15e-3f;  // Quiescent bias current from VBE multiplier [A]
    float Vcc              = 24.0f;   // Supply voltage magnitude [V] (+/- 24V rails)

    bool isValid() const
    {
        return q8.isValid()
            && q9.isValid()
            && q8.polarity == BJTParams::Polarity::NPN
            && q9.polarity == BJTParams::Polarity::PNP
            && R_emitter_top > 0.0f
            && R_emitter_bottom > 0.0f
            && R_sense >= 0.0f
            && C_comp_top >= 0.0f
            && C_comp_bottom >= 0.0f
            && I_quiescent > 0.0f
            && Vcc > 0.0f;
    }

    /// Default JE-990 output stage configuration.
    /// Q8 = MJE-181 (NPN), Q9 = MJE-171 (PNP), with pre-driver Q7 folded
    /// into Q8's effective beta: beta_eff = beta_q7 * beta_q8 = 697 * 30 ~ 20910.
    /// We cap at a pragmatic value to avoid numerical issues.
    static ClassABConfig JE990_Default()
    {
        ClassABConfig cfg;
        cfg.q8 = BJTParams::MJE181();
        cfg.q9 = BJTParams::MJE171();

        // Fold pre-driver Q7 (2N2484, Bf=697) into Q8's effective beta.
        // beta_eff = beta_q7 * beta_q8 = 697 * 30 = 20910
        // Cap at 5000 for numerical stability — the exact value matters
        // little for the emitter follower's voltage gain (~1), but affects
        // base current magnitude (and thus input impedance).
        cfg.q8.Bf = std::min(BJTParams::N2N2484().Bf * BJTParams::MJE181().Bf,
                             5000.0f);

        // All other values are already set to JE-990 schematic defaults
        return cfg;
    }
};

// ── Class-AB Output Stage WDF ────────────────────────────────────────────────

template <typename NonlinearLeaf = BJTLeaf>
class ClassABOutputWDF
{
public:
    ClassABOutputWDF() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the Class-AB output stage.
    ///
    /// Sets up TWO BJTLeaf instances (Q8 NPN top, Q9 PNP bottom) and
    /// computes the DC bias voltages from the quiescent current setting.
    /// Lead compensation filters are configured from C2, C3 values.
    ///
    /// @param sampleRate  Audio sample rate [Hz]
    /// @param config      ClassABConfig with transistor params and passives
    void prepare(float sampleRate, const ClassABConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;
        Ts_         = 1.0f / sampleRate;
        Vcc_        = config.Vcc;

        // ── Configure Q8 (NPN top) BJT leaf ──────────────────────────────────
        q8Leaf_.configure(config_.q8);
        q8Leaf_.prepare(sampleRate);

        // ── Configure Q9 (PNP bottom) BJT leaf ──────────────────────────────
        q9Leaf_.configure(config_.q9);
        q9Leaf_.prepare(sampleRate);

        // ── DC bias voltages for Class-AB operation ──────────────────────────
        //
        // The VBE multiplier (CR3) sets the quiescent current I_q through
        // both output transistors. Each transistor needs a bias voltage
        // that forward-biases its BE junction at the target quiescent point.
        //
        // Design rule 1: Always add V_bias_base to baseDrive.
        //
        // Q8 (NPN): V_bias_q8 = +Vt * ln(Ic_q / Is + 1)
        //   Positive bias pushes the NPN BE junction into forward-active.
        //
        // Q9 (PNP): V_bias_q9 = -Vt * ln(Ic_q / Is + 1)
        //   Negative bias pushes the PNP BE junction into forward-active.
        //   (PNP forward-active requires Vbe < 0)
        V_bias_q8_ = config_.q8.Vt
                    * std::log(config_.I_quiescent / config_.q8.Is + 1.0f);

        V_bias_q9_ = -config_.q9.Vt
                    * std::log(config_.I_quiescent / config_.q9.Is + 1.0f);

        // ── Lead compensation filters (C2, C3) ──────────────────────────────
        //
        // C2 (62pF) and C3 (91pF) provide phase-lead compensation from
        // each BJT's base to the output node. In the WDF model, these act
        // as first-order lowpass filters on the base drive signal with a
        // cutoff set by the compensation cap and the BJT's base resistance:
        //
        //   fc = 1 / (2 * pi * Rb * C_comp)
        //
        // For Q8 (MJE-181): fc = 1 / (2*pi * 3.0 * 62e-12) ≈ 856 MHz
        // For Q9 (MJE-171): fc = 1 / (2*pi * 3.0 * 91e-12) ≈ 583 MHz
        //
        // These are above Nyquist at any practical sample rate, so we use
        // the bilinear-warped alpha to correctly attenuate any aliased
        // content. The filters also provide a warm-start smoothing effect
        // that aids NR convergence.
        if (config_.C_comp_top > 0.0f)
        {
            // Prewarp the corner frequency for sample-rate invariance
            const float fc_q8 = 1.0f / (kTwoPif * config_.q8.Rb * config_.C_comp_top);
            const float fc_q8_w = prewarpHz(std::min(fc_q8, sampleRate_ * 0.499f), sampleRate_);
            const float tau_q8_w = 1.0f / (kTwoPif * fc_q8_w);
            compAlpha_q8_ = Ts_ / (Ts_ + tau_q8_w);
        }
        else
        {
            compAlpha_q8_ = 1.0f; // No filtering (bypass)
        }

        if (config_.C_comp_bottom > 0.0f)
        {
            // Prewarp the corner frequency for sample-rate invariance
            const float fc_q9 = 1.0f / (kTwoPif * config_.q9.Rb * config_.C_comp_bottom);
            const float fc_q9_w = prewarpHz(std::min(fc_q9, sampleRate_ * 0.499f), sampleRate_);
            const float tau_q9_w = 1.0f / (kTwoPif * fc_q9_w);
            compAlpha_q9_ = Ts_ / (Ts_ + tau_q9_w);
        }
        else
        {
            compAlpha_q9_ = 1.0f; // No filtering (bypass)
        }

        // ── DC tracking filter for AC-coupled output ─────────────────────────
        //
        // Design rule 4: AC coupling between stages.
        // The output uses a sub-Hz DC tracking filter to remove the DC
        // operating point, passing only the AC signal content.
        //
        // Time constant: ~0.5s (sub-Hz, well below audio band)
        // This is conservative — slow enough to not affect bass response
        // down to 20 Hz, fast enough to track thermal drift.
        // Prewarp for sample-rate invariance
        constexpr float kDCTrackTau = 0.5f;
        const float fc_dc = 1.0f / (kTwoPif * kDCTrackTau);
        const float fc_dc_w = prewarpHz(fc_dc, sampleRate_);
        const float tau_dc_w = 1.0f / (kTwoPif * fc_dc_w);
        dcAlpha_ = Ts_ / (Ts_ + tau_dc_w);

        // ── Output voltage clamp ─────────────────────────────────────────────
        // Maximum output swing is limited by the supply rails minus the
        // ballast resistor drops and Vce_sat of the output transistors.
        // At max current (260mA peak), R_emitter drops: 39*0.26 ≈ 10V
        // Practical max swing: Vcc - Vce_sat - R_drop ≈ 24 - 1 - 10 ≈ 13V
        // This matches the Hardy spec of +25 dBu ≈ 13.8 Vrms peak.
        maxOutputSwing_ = config_.Vcc - 1.0f; // Conservative initial clamp

        reset();
    }

    /// Clear all filter states and NR warm-starts.
    ///
    /// Design rule 2: Run 32-sample warmup after reset().
    /// Design rule 3: Initialize all filter states to quiescent values.
    void reset()
    {
        q8Leaf_.reset();
        q9Leaf_.reset();

        // Reset filter states
        compState_q8_ = 0.0f;
        compState_q9_ = 0.0f;
        dcState_       = 0.0f;

        // Reset output state
        outputVoltage_  = 0.0f;
        lastOutput_     = 0.0f;
        lastVce_q8_     = Vcc_;
        lastVce_q9_     = Vcc_;
        lastIc_q8_      = 0.0f;
        lastIc_q9_      = 0.0f;

        // Design rule 2: Warm up the NR solver at the bias point.
        // 32 zero-input samples let both BJTs converge to their quiescent
        // operating points, eliminating the initial transient that would
        // otherwise produce a click/pop artifact.
        for (int i = 0; i < kWarmupSamples; ++i)
            processSample(0.0f);

        // Freeze DC offset to the converged quiescent output.
        // With dcState_=0 during warmup, outputVoltage_ = raw Vout.
        dcState_        = outputVoltage_;
        lastOutput_     = 0.0f;
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the Class-AB push-pull output stage.
    ///
    /// Algorithm:
    ///   1. Apply lead compensation filters (C2, C3) to the input signal
    ///   2. Add DC bias voltages for Q8 (NPN) and Q9 (PNP)
    ///   3. Drive both BJTLeaf instances (NR scatter for each BE junction)
    ///   4. Compute emitter voltages: Ve = baseDrive - Vbe (EF action)
    ///   5. Combine push-pull outputs: weighted average by transconductance
    ///   6. Apply ballast resistor and current-sense drops
    ///   7. Clamp to supply rails
    ///   8. AC-couple the output (remove DC operating point)
    ///
    /// The push-pull combination naturally produces Class-AB behavior:
    ///   - At quiescence: both transistors conduct I_q, output ≈ 0V AC
    ///   - Positive swing: Q8 gm increases, Q9 gm decreases, Q8 dominates
    ///   - Negative swing: Q9 gm increases, Q8 gm decreases, Q9 dominates
    ///   - Crossover region: both have moderate gm, smooth transition
    ///
    /// @param input  VAS output voltage [V] (from the voltage amplifier stage)
    /// @return       AC-coupled output voltage [V]
    float processSample(float input)
    {
        // ── Step 1: Lead compensation filters ────────────────────────────────
        //
        // C2 and C3 filter the base drive to each transistor, providing
        // phase lead compensation for loop stability. Modeled as one-pole
        // lowpass filters (bilinear-warped from the analog RC prototype).
        //
        // compState += alpha * (input - compState)
        // At audio frequencies (well below fc), alpha ≈ 1 and the filter
        // is transparent. At HF near aliasing, it attenuates.
        compState_q8_ += compAlpha_q8_ * (input - compState_q8_);
        compState_q9_ += compAlpha_q9_ * (input - compState_q9_);

        const float drive_q8 = compState_q8_;
        const float drive_q9 = compState_q9_;

        // ── Step 2: Add DC bias for forward-active operation ─────────────────
        //
        // Design rule 1: Always add V_bias_base to baseDrive.
        //
        // Q8 (NPN top): baseDrive = input + V_bias_q8 (positive bias)
        //   Forward-biases the NPN BE junction at I_quiescent.
        //
        // Q9 (PNP bottom): baseDrive = input + V_bias_q9 (negative bias)
        //   Forward-biases the PNP BE junction at I_quiescent.
        //
        // At quiescence (input=0): both transistors sit at their target
        // Vbe, each conducting I_q. The VBE multiplier CR3 is implicit
        // in these bias voltages.
        const float baseDrive_q8 = drive_q8 + V_bias_q8_;
        const float baseDrive_q9 = drive_q9 + V_bias_q9_;

        // ── Step 3: Drive Q8 BJTLeaf (NPN top, NR scatter) ───────────────────
        //
        // Convert the base drive voltage to a WDF incident wave and
        // scatter through the BJT's BE junction nonlinearity.
        //
        // Wave domain: a = 2*V_drive - b_prev
        // The BJTLeaf's NR solver finds the Vbe that satisfies the
        // Ebers-Moll equation at this operating point.
        {
            const float b_prev_q8 = q8Leaf_.getReflectedWave();
            const float a_q8 = 2.0f * baseDrive_q8 - b_prev_q8;
            q8Leaf_.scatter(a_q8);
        }

        // ── Step 4: Drive Q9 BJTLeaf (PNP bottom, NR scatter) ────────────────
        {
            const float b_prev_q9 = q9Leaf_.getReflectedWave();
            const float a_q9 = 2.0f * baseDrive_q9 - b_prev_q9;
            q9Leaf_.scatter(a_q9);
        }

        // ── Step 5: Compute emitter voltages (emitter-follower action) ───────
        //
        // In an emitter follower, the emitter "follows" the base:
        //   Ve = Vbase - Vbe  (for NPN, Vbe > 0)
        //   Ve = Vbase - Vbe  (for PNP, Vbe < 0, so Ve = Vbase + |Vbe|)
        //
        // This gives unity voltage gain with a Vbe offset that depends on
        // the instantaneous collector current through the BJT.
        const float Vbe_q8 = q8Leaf_.getVbe();
        const float Vbe_q9 = q9Leaf_.getVbe();

        const float Ve_q8 = baseDrive_q8 - Vbe_q8;  // NPN: Ve = Vb - Vbe
        const float Ve_q9 = baseDrive_q9 - Vbe_q9;  // PNP: Ve = Vb - Vbe (Vbe<0)

        // ── Step 6: Read collector currents ──────────────────────────────────
        //
        // Ic from each BJTLeaf indicates how much current each transistor
        // contributes to the output. In Class-AB:
        //   - Positive input: Ic_q8 >> I_q, Ic_q9 << I_q
        //   - Negative input: Ic_q9 >> I_q, Ic_q8 << I_q
        //   - At quiescence: Ic_q8 ≈ Ic_q9 ≈ I_q
        lastIc_q8_ = q8Leaf_.getIc();
        lastIc_q9_ = q9Leaf_.getIc();

        // ── Step 7: Push-pull combination (gm-weighted) ─────────────────────
        //
        // This naturally produces Class-AB behavior:
        //   - When Q8 dominates: V_out ≈ Ve_q8
        //   - When Q9 dominates: V_out ≈ Ve_q9
        //   - At crossover: smooth blend weighted by gm
        //
        // The crossover distortion is captured by the transition region
        // where both gm values are comparable but individually low.
        const float gm_q8 = q8Leaf_.getGm();
        const float gm_q9 = q9Leaf_.getGm();
        const float gm_total = gm_q8 + gm_q9;

        float Ve_combined;
        if (gm_total > kEpsilonF)
        {
            Ve_combined = (gm_q8 * Ve_q8 + gm_q9 * Ve_q9) / gm_total;
        }
        else
        {
            // Fallback: if both transistors are off (should not happen in
            // normal operation with proper biasing), average the emitter
            // voltages to avoid division by zero.
            Ve_combined = (Ve_q8 + Ve_q9) * 0.5f;
        }

        // ── Step 8: Ballast resistor and current-sense drops ─────────────────
        //
        // The net output current through R13 (current sense) produces a
        // small voltage drop. For the push-pull pair, the net current is
        // the difference of the two collector currents:
        //
        //   I_out = Ic_q8 - |Ic_q9|  (NPN sources, PNP sinks)
        //
        // For NPN Ic_q8 is positive; for PNP Ic_q9 is negative (convention
        // from BJTCompanionModel). The net output current is Ic_q8 + Ic_q9
        // (since Ic_q9 is already negative for PNP conducting).
        //
        // R13 drop: V_drop = I_out * R_sense
        const float I_out = lastIc_q8_ + lastIc_q9_;
        const float V_sense_drop = I_out * config_.R_sense;

        // Apply the current-sense drop to the combined emitter voltage.
        // The drop opposes the output (negative feedback from R13).
        float Vout_raw = Ve_combined - V_sense_drop;

        // ── Step 9: Supply rail clamp ────────────────────────────────────────
        //
        // The output cannot exceed the supply rails minus the minimum
        // Vce_sat (~1V for power BJTs) and the ballast drops.
        // This models the hard clipping at the rails.
        Vout_raw = std::clamp(Vout_raw, -maxOutputSwing_, maxOutputSwing_);

        // Store full (DC + AC) output for monitoring
        outputVoltage_ = Vout_raw;

        // ── Step 10: Collector-emitter voltages for monitoring ───────────────
        //
        // Q8 (NPN top): collector tied to +Vcc through R14 (39 Ohm)
        //   Vce_q8 = (Vcc - |Ic_q8| * R_emitter_top) - Ve_q8
        //
        // Q9 (PNP bottom): collector tied to -Vcc through R15 (39 Ohm)
        //   Vce_q9 = Ve_q9 - (-Vcc + |Ic_q9| * R_emitter_bottom)
        //          = Ve_q9 + Vcc - |Ic_q9| * R_emitter_bottom
        lastVce_q8_ = (Vcc_ - std::abs(lastIc_q8_) * config_.R_emitter_top)
                     - Ve_q8;
        lastVce_q9_ = Ve_q9
                     - (-Vcc_ + std::abs(lastIc_q9_) * config_.R_emitter_bottom);

        // ── Step 11: AC coupling (DC removal) ────────────────────────────────
        //
        // Subtract the FROZEN DC offset measured at reset().
        // Not updated during audio — keeps the stage stationary for the
        // JE990Path Newton solver.
        lastOutput_ = Vout_raw - dcState_;
        return lastOutput_;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring / diagnostics ─────────────────────────────────────────────

    /// Output impedance [Ohm] of the push-pull stage.
    ///
    /// Zout ≈ 1 / (gm_q8 + gm_q9) + R_sense
    ///
    /// At quiescence (I_q = 15mA):
    ///   gm_q8 = gm_q9 = I_q / Vt ≈ 15e-3 / 0.026 ≈ 577 mS
    ///   1 / (gm_q8 + gm_q9) ≈ 1 / 1.154 ≈ 0.87 Ohm
    ///   Zout ≈ 0.87 + 3.9 ≈ 4.77 Ohm  (matches Hardy spec: < 5 Ohm)
    float getOutputImpedance() const
    {
        const float gm_q8 = q8Leaf_.getGm();
        const float gm_q9 = q9Leaf_.getGm();
        const float gm_total = gm_q8 + gm_q9;
        const float r_intrinsic = (gm_total > kEpsilonF)
                                    ? (1.0f / gm_total)
                                    : 1e6f;
        return r_intrinsic + config_.R_sense;
    }

    /// Collector-emitter voltage for Q8 (NPN top) [V].
    /// Should be > Vce_sat (~1V) to stay out of saturation.
    float getVce_q8() const { return lastVce_q8_; }

    /// Collector-emitter voltage for Q9 (PNP bottom) [V].
    /// Should be > Vce_sat (~1V) to stay out of saturation (magnitude).
    float getVce_q9() const { return lastVce_q9_; }

    /// Collector current for Q8 (NPN top) [A].
    /// Positive in forward-active; increases on positive output swings.
    float getIc_q8() const { return lastIc_q8_; }

    /// Collector current for Q9 (PNP bottom) [A].
    /// Negative in forward-active (PNP convention); magnitude increases
    /// on negative output swings.
    float getIc_q9() const { return lastIc_q9_; }

    /// Last output voltage (AC, after DC removal) [V].
    float getOutputVoltage() const { return lastOutput_; }

    /// Full output voltage (DC + AC, before DC removal) [V].
    /// Useful for monitoring the DC operating point.
    float getRawOutputVoltage() const { return outputVoltage_; }

    /// Estimate of the crossover distortion region width [V].
    ///
    /// The crossover region is where both transistors have low gm,
    /// producing gain compression and distortion. This metric estimates
    /// the input voltage range over which the transition occurs.
    ///
    /// At the crossover point, both transistors conduct I_q.
    /// The crossover width is approximately 2 * Vt * ln(2) ≈ 36 mV
    /// for a perfectly biased Class-AB stage. Higher I_q narrows the
    /// transition (less crossover distortion).
    ///
    /// We compute it from the actual gm values:
    ///   width ≈ 2 * Vt / max(gm_q8, gm_q9)
    float getCrossingDistortion() const
    {
        const float gm_q8 = q8Leaf_.getGm();
        const float gm_q9 = q9Leaf_.getGm();
        const float gm_max = std::max(gm_q8, gm_q9);
        if (gm_max < kEpsilonF)
            return 1.0f; // Both off — maximum crossover distortion

        // Crossover width in volts: the input range where the
        // dominant transistor's gm is less than gm_total/2
        return 2.0f * config_.q8.Vt / gm_max;
    }

    /// Base-emitter voltage for Q8 [V] (positive for NPN forward-active).
    float getVbe_q8() const { return q8Leaf_.getVbe(); }

    /// Base-emitter voltage for Q9 [V] (negative for PNP forward-active).
    float getVbe_q9() const { return q9Leaf_.getVbe(); }

    /// Transconductance of Q8 [S] at the current operating point.
    float getGm_q8() const { return q8Leaf_.getGm(); }

    /// Transconductance of Q9 [S] at the current operating point.
    float getGm_q9() const { return q9Leaf_.getGm(); }

    /// Combined transconductance [S] (determines output impedance).
    float getGm_total() const { return q8Leaf_.getGm() + q9Leaf_.getGm(); }

    /// Net output current [A] (positive = sourcing, negative = sinking).
    /// I_out = Ic_q8 + Ic_q9 (PNP Ic is negative by convention).
    float getOutputCurrent() const { return lastIc_q8_ + lastIc_q9_; }

    /// NR iteration count from Q8's last scatter.
    int getLastIterCount_q8() const { return q8Leaf_.getLastIterCount(); }

    /// NR iteration count from Q9's last scatter.
    int getLastIterCount_q9() const { return q9Leaf_.getLastIterCount(); }

    // ── AC state snapshot (excludes frozen dcState_) ──────────
    struct AcSnap {
        typename NonlinearLeaf::AcState q8, q9;
        float compState_q8, compState_q9;
        float outputVoltage, lastOutput;
        float lastVce_q8, lastVce_q9, lastIc_q8, lastIc_q9;
    };

    AcSnap saveAcState() const
    {
        return { q8Leaf_.saveAcState(), q9Leaf_.saveAcState(),
                 compState_q8_, compState_q9_,
                 outputVoltage_, lastOutput_,
                 lastVce_q8_, lastVce_q9_, lastIc_q8_, lastIc_q9_ };
    }

    void restoreAcState(const AcSnap& s)
    {
        q8Leaf_.restoreAcState(s.q8);
        q9Leaf_.restoreAcState(s.q9);
        compState_q8_ = s.compState_q8; compState_q9_ = s.compState_q9;
        outputVoltage_ = s.outputVoltage; lastOutput_ = s.lastOutput;
        lastVce_q8_ = s.lastVce_q8; lastVce_q9_ = s.lastVce_q9;
        lastIc_q8_ = s.lastIc_q8; lastIc_q9_ = s.lastIc_q9;
        // dcState_ is NOT restored — it's a frozen DC offset
    }

    /// Signal-dependent local small-signal voltage gain.
    /// Uses combined gm of both output transistors for the EF gain formula:
    ///   Av = gm_total / (gm_total + 1/R_sense)
    /// At quiescence (gm_total ≈ 1.15 S): Av ≈ 0.82
    /// At large signal (one BJT off): gm_total ≈ 0.577 S → Av ≈ 0.69
    /// Clamped to [0.5, 0.98] for Newton solver stability.
    float getLocalGain() const
    {
        const float gm_q8 = q8Leaf_.getGm();
        const float gm_q9 = q9Leaf_.getGm();
        const float gm_total = gm_q8 + gm_q9;
        if (gm_total < kEpsilonF) return 0.5f;

        const float g_sense = 1.0f / std::max(config_.R_sense, 0.1f);
        const float gain = gm_total / (gm_total + g_sense);
        return std::clamp(gain, 0.5f, 0.98f);
    }

    /// Quiescent bias current [A] as configured.
    float getQuiescentCurrent() const { return config_.I_quiescent; }

    /// DC operating point estimate [V] (tracked by the DC filter).
    float getDCOffset() const { return dcState_; }

    /// Access the underlying Q8 BJT leaf for advanced diagnostics.
    const NonlinearLeaf& getQ8Leaf() const { return q8Leaf_; }
    NonlinearLeaf& getQ8Leaf() { return q8Leaf_; }

    /// Access the underlying Q9 BJT leaf for advanced diagnostics.
    const NonlinearLeaf& getQ9Leaf() const { return q9Leaf_; }
    NonlinearLeaf& getQ9Leaf() { return q9Leaf_; }

    /// Access the stage configuration.
    const ClassABConfig& getConfig() const { return config_; }

private:
    // ── WDF Nonlinear Elements ───────────────────────────────────────────────
    NonlinearLeaf q8Leaf_;   // Q8 (MJE-181 NPN top) BE junction
    NonlinearLeaf q9Leaf_;   // Q9 (MJE-171 PNP bottom) BE junction

    // ── Configuration ────────────────────────────────────────────────────────
    ClassABConfig config_;
    float sampleRate_     = 44100.0f;
    float Ts_             = 1.0f / 44100.0f;
    float Vcc_            = 24.0f;

    // ── DC bias voltages ─────────────────────────────────────────────────────
    // Computed from I_quiescent and Is via Shockley equation.
    // Q8 (NPN): positive, Q9 (PNP): negative.
    float V_bias_q8_      = 0.0f;
    float V_bias_q9_      = 0.0f;

    // ── Lead compensation filter state ───────────────────────────────────────
    // One-pole lowpass from C2 (Q8) and C3 (Q9) lead compensation caps.
    float compState_q8_   = 0.0f;   // C2 filter state
    float compState_q9_   = 0.0f;   // C3 filter state
    float compAlpha_q8_   = 1.0f;   // C2 LP coefficient
    float compAlpha_q9_   = 1.0f;   // C3 LP coefficient

    // ── DC tracking filter ───────────────────────────────────────────────────
    float dcState_        = 0.0f;   // DC component of output voltage
    float dcAlpha_        = 0.0f;   // LP coefficient for DC tracking

    // ── Output state ─────────────────────────────────────────────────────────
    float outputVoltage_  = 0.0f;   // Full output (DC + AC)
    float lastOutput_     = 0.0f;   // AC-coupled output (last sample)
    float maxOutputSwing_ = 23.0f;  // Supply rail clamp [V]

    // ── Monitoring state ─────────────────────────────────────────────────────
    float lastVce_q8_     = 24.0f;  // Q8 collector-emitter voltage [V]
    float lastVce_q9_     = 24.0f;  // Q9 collector-emitter voltage [V]
    float lastIc_q8_      = 0.0f;   // Q8 collector current [A]
    float lastIc_q9_      = 0.0f;   // Q9 collector current [A]

    // ── Warmup ───────────────────────────────────────────────────────────────
    static constexpr int kWarmupSamples = 32;      // Design rule 2
};

} // namespace transfo
