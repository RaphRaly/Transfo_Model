#pragma once

// =============================================================================
// IAmplifierPath — Strategy interface for switchable amplifier topologies.
//
// Defines the contract that both NeveClassAPath (Chemin A) and JE990Path
// (Chemin B) must implement. Enables runtime A/B switching in PreampModel
// without modifying the signal pipeline.
//
// Pattern: Strategy (GoF) — runtime selection of algorithm family.
//
// Lifecycle:
//   1. prepare(sampleRate, maxBlockSize) — allocate, precompute WDF trees
//   2. setGain(Rfb) — set feedback resistor from GainTable
//   3. processSample() / processBlock() — audio hot path
//   4. reset() — clear state (preset change, transport stop)
//
// Thread safety: NOT thread-safe. The host must ensure prepare/setGain
// are not called concurrently with processBlock.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md Option B (Neve) & Option C (JE-990)
// =============================================================================

namespace transfo {

class IAmplifierPath
{
public:
    virtual ~IAmplifierPath() = default;

    // ── Lifecycle ───────────────────────────────────────────────────────────

    /// Initialize internal WDF trees and allocate buffers.
    /// Called once before processing begins, and again if sample rate changes.
    virtual void prepare(float sampleRate, int maxBlockSize) = 0;

    /// Clear all internal state (capacitor memories, NR warm-starts).
    virtual void reset() = 0;

    // ── Audio processing ────────────────────────────────────────────────────

    /// Process a single sample through the amplifier topology.
    /// Input: voltage from T1 secondary (post-termination).
    /// Output: voltage ready for output stage coupling.
    virtual float processSample(float input) = 0;

    /// Block-based processing for efficiency.
    /// Default implementation calls processSample in a loop.
    virtual void processBlock(const float* input, float* output, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            output[i] = processSample(input[i]);
    }

    // ── Gain control ────────────────────────────────────────────────────────

    /// Set the feedback resistor value [Ohm] from GainTable.
    /// Gain = 1 + Rfb / Rg where Rg = 47 Ohm.
    virtual void setGain(float Rfb) = 0;

    // ── Monitoring / output stage coupling ──────────────────────────────────

    /// Output impedance [Ohm] — needed by T2 for impedance matching.
    /// Neve: ~11 Ohm (EF stage). JE-990: < 5 Ohm (feedback).
    virtual float getOutputImpedance() const = 0;

    /// Human-readable path name for UI / diagnostics.
    virtual const char* getName() const = 0;
};

} // namespace transfo
