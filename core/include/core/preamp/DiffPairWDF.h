#pragma once

// =============================================================================
// DiffPairWDF — WDF Differential Pair stage for the JE-990 discrete op-amp.
//
// Models the matched LM-394 (MAT02) differential input pair with Jensen
// inductive emitter degeneration, as used in the JE-990 topology
// (ANALYSE_ET_DESIGN_Rev2.md section 3.3).
//
// Circuit topology:
//
//   (+) in --[CR1 bypass]-- Q1 Base (LM-394 #1)
//                            |C --> to cascode Q3
//                            |E
//                            R1[30R] + L1[20uH]
//                            |
//                            +--- tail node
//                            |
//                            R2[30R] + L2[20uH]
//                            |E
//   (-) in --[CR2 bypass]-- Q2 Base (LM-394 #2)
//                            |C --> to cascode Q5
//
//   Tail current source: Q4 (2N2484) + R3[160R]
//     --> simplified as ideal I_tail = 3 mA
//
// WDF implementation strategy:
//
//   Each half of the pair uses a BJTLeaf (companion-source NR solver)
//   for the BE junction. The differential output is the current difference
//   (Ic1 - Ic2), converted to voltage through a nominal load resistance
//   (R_load ~ 300 Ohm, matching the cascode load R4/R5).
//
//   Emitter degeneration (R1+L1, R2+L2) is modeled as a frequency-dependent
//   gain reduction factor:
//     - At DC:  Z_degen = R_emitter  -->  gain = gm * R_load / (1 + gm*R)
//     - At HF:  Z_degen = R + jwL     -->  more degeneration at HF
//   This is Jensen's patent technique: the inductors increase degeneration
//   at high frequencies, reducing THD at HF where ears are most sensitive
//   to odd harmonics.
//
//   The frequency-dependent inductor effect is modeled as a first-order
//   filter on the effective degeneration impedance, updated per sample.
//
// Design rules (from Sprint 3 lessons):
//   1. Always add V_bias_base to baseDrive -- without it, BJTs stay off
//   2. Emitter degeneration in processSample() -- R1/R2=30R + L1/L2=20uH
//   3. Run 32-sample warmup after reset() -- WDF b_reflected=0 transient
//   4. Initialize all filter states to quiescent values -- not 0
//   5. One scatter per reactive WDF element per sample -- no double-scatter
//   6. AC coupling between stages, not DC -- independent bias per stage
//
// Template parameter: NonlinearLeaf (default BJTLeaf) -- the WDOnePort
// element solving the BE junction via Newton-Raphson. Allows substituting
// a more elaborate BJT model without altering the diff pair topology.
//
// Pattern: CRTP-composing WDF sub-circuit (header-only).
//
// Reference: Ebers-Moll 1954; Jensen US4,612,517 (inductive degeneration);
//            JE-990 schematic (John Hardy); ANALYSE_ET_DESIGN_Rev2.md sec 3.3
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include "../wdf/AdaptedElements.h"
#include "../wdf/BJTLeaf.h"
#include <algorithm>
#include <cmath>

namespace transfo {

// -- Configuration ------------------------------------------------------------

struct DiffPairConfig
{
    BJTParams q1q2;             // LM-394 matched pair (used for both Q1, Q2)
    float R_emitter   = 30.0f;  // R1, R2 emitter degeneration [Ohm]
    float L_emitter   = 20e-6f; // L1, L2 Jensen inductors [H]
    float I_tail      = 3e-3f;  // Tail current [A] (Q4 simplified as ideal)
    float R_tail      = 160.0f; // R3 tail resistor [Ohm]
    float R_load      = 300.0f; // Cascode load resistance R4/R5 [Ohm]
    float Vcc         = 24.0f;  // Supply voltage [V]

    bool isValid() const
    {
        return q1q2.isValid()
            && R_emitter >= 0.0f
            && L_emitter >= 0.0f
            && I_tail > 0.0f
            && R_tail > 0.0f
            && R_load > 0.0f
            && Vcc > 0.0f;
    }

    /// Default JE-990 differential pair configuration.
    static DiffPairConfig JE990_Default()
    {
        DiffPairConfig cfg;
        cfg.q1q2 = BJTParams::LM394();
        // All other values are already set to JE-990 defaults
        return cfg;
    }
};

// -- Differential Pair WDF ----------------------------------------------------

template <typename NonlinearLeaf = BJTLeaf>
class DiffPairWDF
{
public:
    DiffPairWDF() = default;

    // -- Preparation ----------------------------------------------------------

    /// Initialize the differential pair from config and sample rate.
    ///
    /// Sets up:
    ///   - Two BJTLeaf instances for Q1 and Q2 (matched LM-394)
    ///   - Emitter degeneration inductors L1, L2 (AdaptedInductor, 20uH)
    ///   - DC bias voltage for forward-active operation at I_tail/2 per side
    ///   - Frequency-dependent degeneration filter coefficients
    void prepare(float sampleRate, const DiffPairConfig& config)
    {
        sampleRate_ = sampleRate;
        config_     = config;
        Ts_         = 1.0f / sampleRate;

        // Both transistors are NPN (LM-394 matched pair)
        sign_ = config.q1q2.polaritySign();

        // -- Configure BJT leaves (identical matched pair) --------------------
        q1_.configure(config.q1q2);
        q1_.prepare(sampleRate);
        q2_.configure(config.q1q2);
        q2_.prepare(sampleRate);

        // -- Emitter degeneration inductors -----------------------------------
        // L1, L2 = 20 uH Jensen inductors.
        // Used as adapted WDF inductors to model the frequency-dependent
        // impedance: Z_L = 2L/Ts in the wave domain (trapezoidal rule).
        hasInductors_ = (config.L_emitter > 0.0f);
        if (hasInductors_)
        {
            l1_.setInductance(config.L_emitter, Ts_);
            l2_.setInductance(config.L_emitter, Ts_);
        }

        // -- DC bias for forward-active operation -----------------------------
        // Quiescent: each transistor carries I_tail/2.
        //   Ic_q = I_tail / 2 = 1.5 mA (for I_tail = 3 mA)
        //   V_bias = Vt * ln(Ic_q / Is + 1)  [NPN: sign = +1]
        //
        // This bias voltage is added to baseDrive in processSample() so that
        // the BJT NR solver starts in the forward-active region. Without it,
        // both transistors remain off (Rule 1).
        Ic_quiescent_ = config.I_tail * 0.5f;
        V_bias_base_  = sign_ * config.q1q2.Vt
                       * std::log(Ic_quiescent_ / config.q1q2.Is + 1.0f);

        // -- Frequency-dependent degeneration filter --------------------------
        // The emitter degeneration impedance is Z = R + jwL.
        // At DC, Z = R_emitter. At HF, the inductive component dominates.
        //
        // Model as a first-order filter on the effective degeneration:
        //   R_eff[n] = R_emitter + L_effect[n]
        // where L_effect is derived from the inductor's reflected wave.
        //
        // Corner frequency of R-L series: f_c = R / (2*pi*L)
        //   For R=30, L=20uH: f_c = 30 / (2*pi*20e-6) ~ 239 kHz
        // This is well above audio, so the inductor's effect is subtle --
        // a gentle roll-up of degeneration in the top octave. This matches
        // Jensen's intent: reduce THD at HF without audible gain change.
        if (hasInductors_ && config.L_emitter > 0.0f)
        {
            const float fc = config.R_emitter / (kTwoPif * config.L_emitter);
            // Discrete first-order LP coefficient for tracking the inductive
            // component: alpha = 1 / (1 + fc * Ts * 2*pi) ... but we want
            // HPF behavior (inductor passes more at HF), so:
            //   inductorAlpha_ = exp(-Ts * 2*pi*fc) -- time-domain decay
            inductorAlpha_ = std::exp(-Ts_ * kTwoPif * fc);
        }

        // -- Output scaling ---------------------------------------------------
        // The differential current DIc = Ic1 - Ic2 is converted to voltage
        // by the cascode load resistance: Vout = DIc * R_load.
        // This matches the physical circuit where the cascode mirrors the
        // differential current into R4/R5 = 300 Ohm.

        reset();
    }

    /// Clear all reactive element states and NR warm-starts.
    ///
    /// After clearing, runs a 32-sample silent warmup so the WDF companion
    /// sources converge at the quiescent operating point (Rule 3).
    /// All states are then re-initialized to the converged values (Rule 4).
    void reset()
    {
        q1_.reset();
        q2_.reset();

        if (hasInductors_)
        {
            l1_.reset();
            l2_.reset();
        }

        // Initialize degeneration filter states to quiescent (Rule 4)
        inductorState1_ = 0.0f;
        inductorState2_ = 0.0f;

        // Initialize output and monitoring state
        Ic1_         = Ic_quiescent_;
        Ic2_         = Ic_quiescent_;
        Vbe1_        = V_bias_base_;
        Vbe2_        = V_bias_base_;
        gm_eff_      = 0.0f;
        outputVoltage_ = 0.0f;
        dcSettleCount_ = 0;
        outputDC_      = 0.0f;

        // -- Warmup: run 32 zero-input samples to settle bias (Rule 3) --------
        // The BJTLeaf starts with b_reflected = 0, which is far from the
        // quiescent operating point. Without warmup, the first real audio
        // samples would see a large transient as the NR solver converges.
        for (int i = 0; i < kWarmupSamples; ++i)
            processSample(0.0f, 0.0f);

        // Re-initialize DC tracker and output to the converged state
        outputDC_      = outputVoltage_;
        outputVoltage_ = 0.0f;
        dcSettleCount_ = 0;
    }

    // -- Audio processing -----------------------------------------------------

    /// Process a single sample through the differential pair.
    ///
    /// Algorithm:
    ///   1. Add DC bias to each input for forward-active operation (Rule 1)
    ///   2. Drive Q1 BJTLeaf with (vPlus + V_bias) -- NR scatter
    ///   3. Drive Q2 BJTLeaf with (vMinus + V_bias) -- NR scatter
    ///   4. Read Ic1, Ic2 from companion models
    ///   5. Apply tail current constraint: Ic1 + Ic2 ~ I_tail
    ///   6. Compute emitter degeneration with inductor effect (Rule 2)
    ///   7. Compute differential output: Vout = (Ic1 - Ic2) * R_load
    ///   8. Remove DC offset for AC-coupled output (Rule 6)
    ///
    /// @param vPlus   Positive input voltage [V] (from T1 or feedback sum)
    /// @param vMinus  Negative input voltage [V] (feedback, 0 for open-loop)
    /// @return        Differential output voltage [V] (single-ended, for cascode)
    float processSample(float vPlus, float vMinus)
    {
        // -- Step 1: Compute base drive with DC bias (Rule 1) -----------------
        // V_bias_base_ forward-biases each BE junction at I_tail/2.
        // The input signal modulates around this quiescent point.
        const float baseDrive1 = vPlus  + V_bias_base_;
        const float baseDrive2 = vMinus + V_bias_base_;

        // -- Step 2: Drive Q1 BJTLeaf (positive half) -------------------------
        // Scatter the base drive through the nonlinear BE junction.
        // The companion model internally runs NR to solve for Vbe1.
        // One scatter per reactive element per sample (Rule 5).
        {
            const float b_prev1 = q1_.getReflectedWave();
            const float a_q1 = 2.0f * baseDrive1 - b_prev1;
            q1_.scatter(a_q1);
        }

        // -- Step 3: Drive Q2 BJTLeaf (negative half) -------------------------
        {
            const float b_prev2 = q2_.getReflectedWave();
            const float a_q2 = 2.0f * baseDrive2 - b_prev2;
            q2_.scatter(a_q2);
        }

        // -- Step 4: Read collector currents ----------------------------------
        // Ic from the companion model represents the forward-active current.
        // For an NPN pair: Ic > 0 in forward-active.
        float Ic1_raw = q1_.getCollectorCurrent();
        float Ic2_raw = q2_.getCollectorCurrent();

        // -- Step 5: Tail current constraint ----------------------------------
        // In the real circuit, the total emitter current is fixed by the
        // tail current source Q4: Ie1 + Ie2 = I_tail (constant).
        // For a matched pair, Ie ~ Ic (alpha ~ 1), so Ic1 + Ic2 ~ I_tail.
        //
        // Enforce this constraint by normalizing:
        //   Ic1_constrained = I_tail * Ic1 / (Ic1 + Ic2)
        //   Ic2_constrained = I_tail * Ic2 / (Ic1 + Ic2)
        //
        // This prevents the total current from drifting during transients
        // and models the current-steering behavior of a true diff pair:
        // when one side gets more base drive, it "steals" current from the
        // other side through the shared tail node.
        const float Ic_sum = std::abs(Ic1_raw) + std::abs(Ic2_raw);
        if (Ic_sum > kEpsilonF)
        {
            const float scale = config_.I_tail / Ic_sum;
            Ic1_ = Ic1_raw * scale;
            Ic2_ = Ic2_raw * scale;
        }
        else
        {
            // Both transistors are off (startup transient) -- split equally
            Ic1_ = Ic_quiescent_;
            Ic2_ = Ic_quiescent_;
        }

        // -- Step 6: Emitter degeneration with inductor effect (Rule 2) -------
        // The emitter degeneration reduces differential transconductance:
        //   gm_eff = gm / (1 + gm * Z_degen)
        // where Z_degen = R_emitter at DC, increasing at HF due to L.
        //
        // For each half-circuit:
        //   gm_half = |Ic| / Vt  (individual transconductance)
        //   Differential gm_diff = gm_half (for a balanced pair, each half
        //     contributes gm_half to the differential transconductance)
        //
        // The inductor L creates frequency-dependent degeneration:
        //   Z_degen(f) = R + j*2*pi*f*L
        //   |Z_degen| grows at HF --> more degeneration at HF
        //
        // Model: use the WDF inductor to track the AC component of emitter
        // current, deriving an effective R_eff that increases at HF.

        const float gm1 = q1_.getGm();
        const float gm2 = q2_.getGm();

        // Average transconductance for the pair
        const float gm_avg = (gm1 + gm2) * 0.5f;

        // Effective degeneration impedance
        float R_eff = config_.R_emitter;

        if (hasInductors_ && gm_avg > kEpsilonF)
        {
            // Scatter each inductor with the corresponding emitter AC current.
            // The inductor's reflected wave magnitude indicates the reactive
            // voltage drop, which increases with frequency.
            //
            // Inductor wave variable: a = V + Z*I, b = V - Z*I = -a[n-1]
            // where Z_L = 2*L/Ts. The voltage across L at each instant is
            // proportional to the rate of change of Ie.
            //
            // We use the AC component of Ic as a proxy for emitter current
            // (Ie ~ Ic for high-beta BJTs).

            const float Ie1_ac = Ic1_ - Ic_quiescent_;
            const float Ie2_ac = Ic2_ - Ic_quiescent_;

            // Scatter through L1 and L2 (one scatter per element per sample,
            // Rule 5). The inductor's port impedance Z_L = 2*L/Ts determines
            // how much voltage the inductor develops for a given current change.
            const float Z_L = l1_.getPortResistance();

            const float a_l1 = Ie1_ac * Z_L;   // incident wave to L1
            const float b_l1 = l1_.scatter(a_l1);
            const float V_L1 = (a_l1 + b_l1) * 0.5f;  // voltage across L1

            const float a_l2 = Ie2_ac * Z_L;   // incident wave to L2
            const float b_l2 = l2_.scatter(a_l2);
            const float V_L2 = (a_l2 + b_l2) * 0.5f;  // voltage across L2

            // The effective additional impedance from the inductor:
            // R_L_eff = |V_L| / |Ie_ac| (averaged over both halves)
            const float V_L_avg = (std::abs(V_L1) + std::abs(V_L2)) * 0.5f;
            const float Ie_ac_avg = (std::abs(Ie1_ac) + std::abs(Ie2_ac)) * 0.5f;

            float R_L_extra = 0.0f;
            if (Ie_ac_avg > kEpsilonF)
            {
                R_L_extra = V_L_avg / Ie_ac_avg;
            }

            // Smooth the inductive impedance estimate to avoid sample-rate
            // artifacts. The inductor effect should be a gentle HF roll-up,
            // not a noisy per-sample modulation.
            inductorState1_ = inductorAlpha_ * inductorState1_
                            + (1.0f - inductorAlpha_) * R_L_extra;

            R_eff = config_.R_emitter + inductorState1_;
        }

        // Compute effective differential transconductance with degeneration:
        //   gm_diff_eff = gm_avg / (1 + gm_avg * R_eff)
        // This is the standard degenerated-pair transconductance formula.
        float degenFactor = 1.0f;
        if (gm_avg > kEpsilonF)
        {
            degenFactor = 1.0f / (1.0f + gm_avg * R_eff);
        }
        gm_eff_ = gm_avg * degenFactor;

        // -- Step 7: Differential output voltage ------------------------------
        // The differential current DIc = Ic1 - Ic2 is converted to voltage
        // by the cascode load: Vout = DIc * R_load.
        //
        // Apply the degeneration factor to the differential current:
        //   DIc_eff = (Ic1 - Ic2) * degenFactor
        // This correctly models the gain reduction from emitter degeneration.
        const float dIc = (Ic1_ - Ic2_) * degenFactor;
        float Vout = dIc * config_.R_load;

        // Clamp output to supply rail limits.
        // The cascode output cannot exceed the supply rails.
        Vout = std::clamp(Vout, -config_.Vcc, config_.Vcc);

        // -- Step 8: DC offset removal (Rule 6) -------------------------------
        // For a perfectly matched pair with equal inputs, DIc = 0 and
        // Vout = 0. In practice, small numerical asymmetries can create
        // a DC offset. Track and remove it with a slow DC filter.
        if (dcSettleCount_ < kDCSettleSamples)
        {
            // During initial settling, track DC rapidly
            outputDC_ += 0.01f * (Vout - outputDC_);
            ++dcSettleCount_;
        }
        else
        {
            // Steady state: very slow DC tracking (sub-Hz)
            constexpr float kDCTrackAlpha = 0.0001f;
            outputDC_ += kDCTrackAlpha * (Vout - outputDC_);
        }

        outputVoltage_ = Vout - outputDC_;

        // -- Update monitoring state ------------------------------------------
        Vbe1_ = q1_.getVbe();
        Vbe2_ = q2_.getVbe();

        return outputVoltage_;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* inPlus, const float* inMinus,
                      float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(inPlus[i], inMinus[i]);
    }

    /// Convenience overload for single-ended input (vMinus = 0).
    /// Used when no feedback is connected (open-loop testing).
    float processSample(float vPlus)
    {
        return processSample(vPlus, 0.0f);
    }

    // -- Monitoring / diagnostics ---------------------------------------------

    /// Effective differential transconductance [S] at the current
    /// operating point, including emitter degeneration.
    ///   gm_eff = gm / (1 + gm * R_eff)
    /// For the JE-990 at quiescence: gm ~ 58 mS, R_eff ~ 30 Ohm,
    ///   gm_eff ~ 58e-3 / (1 + 58e-3*30) ~ 21 mS.
    float getGm() const { return gm_eff_; }

    /// Raw (undegenerated) transconductance of Q1 [S].
    float getGmRaw1() const { return q1_.getGm(); }

    /// Raw (undegenerated) transconductance of Q2 [S].
    float getGmRaw2() const { return q2_.getGm(); }

    /// Base-emitter voltage of Q1 [V].
    float getVbe1() const { return Vbe1_; }

    /// Base-emitter voltage of Q2 [V].
    float getVbe2() const { return Vbe2_; }

    /// Collector current of Q1 [A] (constrained by tail current).
    float getIc1() const { return Ic1_; }

    /// Collector current of Q2 [A] (constrained by tail current).
    float getIc2() const { return Ic2_; }

    /// Last computed differential output voltage [V].
    float getOutputVoltage() const { return outputVoltage_; }

    /// Quiescent collector current per transistor [A].
    /// Ic_q = I_tail / 2 = 1.5 mA for default config.
    float getQuiescentIc() const { return Ic_quiescent_; }

    /// DC bias voltage applied to each base [V].
    /// V_bias = Vt * ln(Ic_q / Is + 1).
    float getBiasVoltage() const { return V_bias_base_; }

    /// Tail current [A] (fixed by current source Q4).
    float getTailCurrent() const { return config_.I_tail; }

    /// Differential input offset voltage [V].
    /// The LM-394 specifies Vos < 50 uV; in the model this is 0
    /// since both BJTs use identical parameters. Provided for
    /// future tolerance/mismatch modeling.
    float getInputOffset() const { return 0.0f; }

    /// NR iteration count from Q1's last scatter.
    int getLastIterCount1() const { return q1_.getLastIterCount(); }

    /// NR iteration count from Q2's last scatter.
    int getLastIterCount2() const { return q2_.getLastIterCount(); }

    /// Access the underlying BJT leaves for advanced diagnostics.
    const NonlinearLeaf& getQ1Leaf() const { return q1_; }
    NonlinearLeaf& getQ1Leaf() { return q1_; }
    const NonlinearLeaf& getQ2Leaf() const { return q2_; }
    NonlinearLeaf& getQ2Leaf() { return q2_; }

    /// Access the current configuration.
    const DiffPairConfig& getConfig() const { return config_; }

private:
    // -- WDF elements ---------------------------------------------------------
    NonlinearLeaf   q1_;        // Q1 BE junction (LM-394 #1, NR-solved)
    NonlinearLeaf   q2_;        // Q2 BE junction (LM-394 #2, NR-solved)
    AdaptedInductor l1_;        // L1 Jensen emitter inductor (20 uH)
    AdaptedInductor l2_;        // L2 Jensen emitter inductor (20 uH)

    // -- Configuration --------------------------------------------------------
    DiffPairConfig config_;
    float sampleRate_ = 44100.0f;
    float Ts_         = 1.0f / 44100.0f;
    float sign_       = 1.0f;          // +1 NPN, -1 PNP (always +1 for LM-394)

    // -- Flags ----------------------------------------------------------------
    bool hasInductors_ = false;

    // -- DC bias --------------------------------------------------------------
    float V_bias_base_    = 0.0f;      // DC bias for forward-active [V]
    float Ic_quiescent_   = 1.5e-3f;   // Quiescent Ic per transistor [A]

    // -- Emitter degeneration filter state -------------------------------------
    float inductorAlpha_  = 0.0f;      // LP coefficient for inductor smoothing
    float inductorState1_ = 0.0f;      // Smoothed inductive impedance [Ohm]
    float inductorState2_ = 0.0f;      // (reserved for asymmetric extension)

    // -- Output state ---------------------------------------------------------
    float Ic1_            = 1.5e-3f;   // Q1 collector current (constrained) [A]
    float Ic2_            = 1.5e-3f;   // Q2 collector current (constrained) [A]
    float Vbe1_           = 0.0f;      // Q1 base-emitter voltage [V]
    float Vbe2_           = 0.0f;      // Q2 base-emitter voltage [V]
    float gm_eff_         = 0.0f;      // Effective differential gm [S]
    float outputVoltage_  = 0.0f;      // Last differential output [V]

    // -- DC settling ----------------------------------------------------------
    static constexpr int kDCSettleSamples = 4096;  // ~93 ms at 44.1 kHz
    static constexpr int kWarmupSamples   = 32;    // Warmup iterations (Rule 3)
    int   dcSettleCount_ = 0;
    float outputDC_      = 0.0f;       // DC offset estimate [V]
};

} // namespace transfo
