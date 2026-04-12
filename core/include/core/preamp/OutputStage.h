#pragma once

// =============================================================================
// OutputStage — WDF output stage for the dual-topology preamp.
//
// Combines the two amplifier paths (Chemin A: Neve, Chemin B: JE-990) via
// equal-power crossfade and drives the JT-11ELCF output transformer (T2).
//
// Signal flow:
//
//   Chemin A (Neve)  ──┐
//                      ├── ABCrossfade ──[ mixed ]── T2 (JT-11ELCF) ── XLR Out
//   Chemin B (JE-990) ─┘
//
// T2 is a 1:1 bifilar Jensen JT-11ELCF:
//   Rdc = 40 Ohm per winding, BW = 0.18 Hz – 15 MHz
//   Insertion loss ≈ -1.1 dB, drives 600 Ohm loads to +24 dBu @ 20 Hz
//   Output impedance: Zout ≈ 80–91 Ohm (dominated by winding resistance)
//
// Source impedance handling (S5-4, P1):
//   The effective source impedance seen by T2 depends on which path is active:
//     Chemin A (Neve):   Zout_A ≈ 11 Ohm (EF stage + 10 Ohm series)
//     Chemin B (JE-990): Zout_B ≈ 44 Ohm (ClassAB + 39 Ohm isolator)
//   During crossfade, Zout_eff = gA^2 * Zout_A + gB^2 * Zout_B (power-weighted).
//   When a crossfade completes, T2's WDF source impedance is dynamically
//   re-adapted via setSourceImpedance() if the effective Z changed by >10%.
//   This is lightweight (updates port resistance + root junction scattering).
//
// Template parameter: NonlinearLeaf type for T2 (CPWLLeaf for realtime,
// JilesAthertonLeaf for physical mode).
//
// Pattern: Facade over ABCrossfade + TransformerCircuitWDF.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md §5 (Output Stage);
//            Jensen JT-11ELCF datasheet; AES preamp design notes
// =============================================================================

#include "ABCrossfade.h"
#include "../model/TransformerModel.h"
#include "../magnetics/CPWLLeaf.h"
#include "../model/TransformerConfig.h"

#include <algorithm>
#include <cmath>

namespace transfo {

template <typename NonlinearLeaf = CPWLLeaf>
class OutputStage
{
public:
    OutputStage() = default;

    // ── Preparation ───────────────────────────────────────────────────────────

    /// Initialize the output stage with T2 config.
    /// @param sampleRate   Host sample rate [Hz]
    /// @param t2Config     JT-11ELCF transformer configuration
    /// @param fadeTimeMs   Crossfade duration [ms] (default 5ms)
    void prepare(float sampleRate, const TransformerConfig& t2Config,
                 float fadeTimeMs = 5.0f)
    {
        sampleRate_ = sampleRate;

        // Prepare crossfade engine
        crossfade_.prepare(sampleRate, fadeTimeMs);

        // Prepare T2 transformer (full cascade model: J-A + Bertotti + Lm + LC)
        t2_.setConfig(t2Config);
        t2_.prepareToPlay(sampleRate, 1);
        t2_.reset();

        // Cache T2 winding resistance for output impedance calculation
        Rdc_pri_ = t2Config.windings.Rdc_primary;
        Rdc_sec_ = t2Config.windings.Rdc_secondary;

        // T2 insertion gain: n * Rload / (Rs*n² + Rdc_pri*n² + Rdc_sec + Rload)
        {
            const float n = t2Config.windings.turnsRatio();
            const float Rsrc = t2Config.windings.sourceImpedance;
            const float Rload = t2Config.loadImpedance;
            const float Rtotal = Rsrc * n * n + Rdc_pri_ * n * n + Rdc_sec_ + Rload;
            t2InsertionGain_ = (Rtotal > 0.0f) ? n * Rload / Rtotal : n;
        }

        // Cache initial source impedance for dynamic update detection
        lastSourceZ_ = getEffectiveSourceZ();

        lastOutput_ = 0.0f;
    }

    /// Clear all reactive element states and the nonlinear leaf.
    void reset()
    {
        crossfade_.reset();
        t2_.reset();
        lastOutput_ = 0.0f;
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    /// Process both path outputs and return the final output voltage.
    /// @param sampleA  Output voltage from Neve path (Chemin A) [V]
    /// @param sampleB  Output voltage from JE-990 path (Chemin B) [V]
    /// @return         Output voltage after T2 [V]
    float processSample(float sampleA, float sampleB)
    {
        // 1. Crossfade between the two paths
        const bool wasTransitioning = crossfade_.isTransitioning();
        const float mixed = crossfade_.processSample(sampleA, sampleB);
        const bool justCompleted = wasTransitioning && !crossfade_.isTransitioning();

        // 2. If transition just completed, update T2 source impedance
        //    so the WDF tree reflects the new dominant path's output Z.
        if (justCompleted)
        {
            updateT2SourceImpedance();
        }

        // 3. Drive through T2 output transformer (full cascade model)
        // TransformerModel returns unity-gain-normalized; multiply by insertion gain.
        // T2 is 1:1: insertionGain = Rload / (Rsource + Rdc_pri + Rdc_sec + Rload)
        float out = t2_.processSample(mixed) * t2InsertionGain_;

        // 4. Store for monitoring
        lastOutput_ = out;
        return out;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* inputA, const float* inputB,
                      float* output, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            output[i] = processSample(inputA[i], inputB[i]);
    }

    // ── Path selection ────────────────────────────────────────────────────────

    /// Set target path position. 0 = Neve (Chemin A), 1 = JE-990 (Chemin B).
    /// Transition is smoothed over the configured fade time.
    void setPath(float position)
    {
        crossfade_.setPosition(position);
    }

    /// True while crossfading between paths.
    bool isTransitioning() const
    {
        return crossfade_.isTransitioning();
    }

    // ── Source impedance ──────────────────────────────────────────────────────

    /// Set the output impedances of each amplifier path [Ohm].
    /// Used for effective source impedance calculation during crossfade.
    /// Default: Zout_A = 11 Ohm (Neve EF), Zout_B = 44 Ohm (JE-990 isolator).
    void setPathImpedances(float ZoutA, float ZoutB)
    {
        ZoutA_ = ZoutA;
        ZoutB_ = ZoutB;
    }

    /// Effective source impedance seen by T2 [Ohm].
    /// Power-weighted interpolation: Zout_eff = gA^2 * ZoutA + gB^2 * ZoutB.
    /// At endpoints: pure path impedance. At midpoint: average (equal power).
    float getEffectiveSourceZ() const
    {
        const float gA = crossfade_.getGainA();
        const float gB = crossfade_.getGainB();
        return gA * gA * ZoutA_ + gB * gB * ZoutB_;
    }

    // ── Monitoring ────────────────────────────────────────────────────────────

    /// Magnetizing current [A]. In cascade mode, approximation from J-A state.
    float getT2MagnetizingCurrent() const
    {
        return t2_.getMagnetizingCurrent();
    }

    /// Last output sample absolute value [V]. Quick level check.
    float getOutputLevel() const
    {
        return std::fabs(lastOutput_);
    }

    /// Output impedance [Ohm].
    /// T2 dominates: Rdc_pri + Rdc_sec = 40 + 40 = 80 Ohm (1:1 bifilar).
    /// With reflected source impedance and coupling losses: ~80–91 Ohm.
    float getOutputImpedance() const
    {
        return Rdc_pri_ + Rdc_sec_ + getEffectiveSourceZ();
    }

    /// Current crossfade position (0 = Neve, 1 = JE-990).
    float getPathPosition() const { return crossfade_.getPosition(); }

    /// Access the underlying T2 TransformerModel for advanced diagnostics.
    const TransformerModel<NonlinearLeaf>& getTransformerModel() const { return t2_; }
    TransformerModel<NonlinearLeaf>& getTransformerModel() { return t2_; }

private:
    // ── Dynamic source impedance update ──────────────────────────────────────

    /// Reconfigure T2's WDF source impedance if the effective source Z
    /// has changed significantly (>10%) after a crossfade completes.
    /// This is a lightweight operation — it updates the AdaptedRSource
    /// port resistance and re-scatters the root junction, without touching
    /// any reactive element states.
    void updateT2SourceImpedance()
    {
        const float Zs_new = getEffectiveSourceZ();
        // Only reconfigure if impedance changed significantly (>10%)
        if (std::abs(Zs_new - lastSourceZ_) > lastSourceZ_ * 0.1f)
        {
            // Update the T2 transformer's source impedance (cascade + LC)
            t2_.setSourceImpedance(Zs_new);
            lastSourceZ_ = Zs_new;
        }
    }

    // ── Components ────────────────────────────────────────────────────────────
    ABCrossfade                  crossfade_;
    TransformerModel<NonlinearLeaf> t2_;

    // ── Source impedances per path ────────────────────────────────────────────
    float ZoutA_ = 11.0f;    // Neve EF stage + 10 Ohm series [Ohm]
    float ZoutB_ = 44.0f;    // JE-990 ClassAB + 39 Ohm isolator [Ohm]

    // ── T2 winding resistance (cached from config) ───────────────────────────
    float Rdc_pri_ = 40.0f;  // Primary DC resistance [Ohm]
    float Rdc_sec_ = 40.0f;  // Secondary DC resistance [Ohm]

    // ── Insertion gain: n * Rload / (Rsource + Rdc_pri + Rdc_sec + Rload) ────
    float t2InsertionGain_ = 0.85f;

    // ── State ─────────────────────────────────────────────────────────────────
    float sampleRate_ = 44100.0f;
    float lastOutput_ = 0.0f;
    float lastSourceZ_ = 11.0f;   // Cached effective source Z for change detection
};

} // namespace transfo
