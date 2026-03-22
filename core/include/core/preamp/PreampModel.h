#pragma once

// =============================================================================
// PreampModel — Top-level orchestrator for the dual-topology preamp.
//
// Assembles the complete signal chain:
//   InputStageWDF (T1) → [NeveClassAPath | JE990Path] → OutputStageWDF (T2)
//
// Features:
//   - 11-position stepped gain control (GainTable)
//   - Runtime controls: pad, ratio, path, gain, phase, mix
//   - Thread-safe parameter updates (atomic + smoothed)
//   - Monitoring: input/output levels, magnetizing currents, clipping
//
// Both amplifier paths are ALWAYS processed, even when one path has zero
// crossfade gain. This keeps WDF reactive element states continuous, avoiding
// transients when switching paths.
//
// Template parameter: NonlinearLeaf type for T1 and T2 transformers
// (JilesAthertonLeaf<LangevinPade> for Physical mode, CPWLLeaf for Realtime).
//
// Pattern: Facade / Mediator over the four pipeline stages.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md Annexe B (Dual Topology B+C);
//            SPRINT_PLAN_PREAMP.md Sprint 6
// =============================================================================

#include "InputStageWDF.h"
#include "NeveClassAPath.h"
#include "JE990Path.h"
#include "OutputStageWDF.h"
#include "GainTable.h"
#include "../model/PreampConfig.h"
#include "../util/SmoothedValue.h"
#include "../util/Constants.h"
#include "../dsp/OversamplingEngine.h"
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

namespace transfo {

template <typename NonlinearLeaf>
class PreampModel
{
public:
    PreampModel() = default;

    // ── Configuration ─────────────────────────────────────────────────────────

    /// Set the full preamp configuration. Must be called before prepareToPlay().
    void setConfig(const PreampConfig& config)
    {
        config_ = config;
        configured_ = true;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Prepare all stages for playback.
    /// @param sampleRate    Host sample rate [Hz]
    /// @param maxBlockSize  Maximum expected block size [samples]
    void prepareToPlay(float sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // ── Input stage (T1) ──────────────────────────────────────────────
        inputStage_.prepare(sampleRate, config_.input);

        // ── Amplifier paths ───────────────────────────────────────────────
        nevePath_.configure(config_.neveConfig);
        nevePath_.prepare(sampleRate, maxBlockSize);

        je990Path_.configure(config_.je990Config);
        je990Path_.prepare(sampleRate, maxBlockSize);

        // ── Output stage (T2) ─────────────────────────────────────────────
        outputStage_.prepare(sampleRate, config_.t2Config, 5.0f);
        outputStage_.setPathImpedances(
            nevePath_.getOutputImpedance(),   // ~11 Ohm
            44.0f                              // JE-990 post-isolator ~44 Ohm
        );

        // ── Gain: apply default position to both paths ────────────────────
        applyGainPosition(gainPosition_);

        // ── Smoothed parameters ───────────────────────────────────────────
        constexpr double kRampTimeSec = 0.02;  // 20 ms
        inputGain_.reset(static_cast<double>(sampleRate), kRampTimeSec);
        outputGain_.reset(static_cast<double>(sampleRate), kRampTimeSec);
        mix_.reset(static_cast<double>(sampleRate), kRampTimeSec);

        inputGain_.setCurrentAndTargetValue(1.0f);
        outputGain_.setCurrentAndTargetValue(1.0f);
        mix_.setCurrentAndTargetValue(1.0f);

        // ── Allocate scratch buffers ──────────────────────────────────────
        neveBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0f);
        jensenBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0f);

        // ── Initialize cached control state ───────────────────────────────
        cachedPath_ = targetPath_.load();
        cachedPad_ = padEnabled_.load();
        cachedRatio_ = ratio_.load();

        // ── Reset levels ──────────────────────────────────────────────────
        lastInputLevel_ = 0.0f;
        lastOutputLevel_ = 0.0f;
    }

    /// Clear all reactive element states across the entire chain.
    void reset()
    {
        inputStage_.reset();
        nevePath_.reset();
        je990Path_.reset();
        outputStage_.reset();

        lastInputLevel_ = 0.0f;
        lastOutputLevel_ = 0.0f;
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    /// Process a block of samples through the full preamp chain.
    /// @param input       Input buffer (mic signal) [numSamples]
    /// @param output      Output buffer [numSamples]
    /// @param numSamples  Number of samples in this block
    void processBlock(const float* input, float* output, int numSamples)
    {
        // ── 1. Read atomic controls at block start ────────────────────────
        const int newPath = targetPath_.load(std::memory_order_relaxed);
        const bool newPad = padEnabled_.load(std::memory_order_relaxed);
        const int newRatio = ratio_.load(std::memory_order_relaxed);
        const bool phaseInv = phaseInvert_.load(std::memory_order_relaxed);

        // ── 2. Apply pad/ratio changes if needed ──────────────────────────
        if (newPad != cachedPad_)
        {
            inputStage_.setPadEnabled(newPad);
            cachedPad_ = newPad;
        }

        if (newRatio != cachedRatio_)
        {
            inputStage_.setRatio(newRatio == 0
                ? InputStageConfig::Ratio::X5
                : InputStageConfig::Ratio::X10);
            cachedRatio_ = newRatio;
        }

        // ── 3. Apply path selection to output stage crossfade ─────────────
        if (newPath != cachedPath_)
        {
            outputStage_.setPath(static_cast<float>(newPath));
            cachedPath_ = newPath;
        }

        // ── 4. Per-sample processing ──────────────────────────────────────
        // Reset peak meters per-block so they track current block, not all-time peak
        lastInputLevel_ = 0.0f;
        lastOutputLevel_ = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // a. Apply input gain (smoothed)
            const float inGain = inputGain_.getNextValue();
            const float x = input[i] * inGain;

            // Track input level (peak)
            const float absIn = std::fabs(x);
            if (absIn > lastInputLevel_)
                lastInputLevel_ = absIn;

            // b. T1 input transformer
            const float t1Out = inputStage_.processSample(x);

            // c. Process BOTH paths (keep WDF states continuous)
            const float neveOut = nevePath_.processSample(t1Out);
            const float jensenOut = je990Path_.processSample(t1Out);

            // d. Output stage: crossfade + T2
            float y = outputStage_.processSample(neveOut, jensenOut);

            // e. Phase invert
            if (phaseInv)
                y = -y;

            // f. Dry/wet mix (smoothed)
            const float wet = mix_.getNextValue();
            y = (1.0f - wet) * input[i] + wet * y;

            // g. Output gain (smoothed)
            const float outGain = outputGain_.getNextValue();
            y *= outGain;

            // h. Store output and track level
            output[i] = y;

            const float absOut = std::fabs(y);
            if (absOut > lastOutputLevel_)
                lastOutputLevel_ = absOut;
        }
    }

    // ── Controls (thread-safe) ────────────────────────────────────────────────

    /// Set the gain position (0-10, 11 steps).
    /// Applies GainTable::getRfb(position) to both amplifier paths.
    void setGainPosition(int position)
    {
        position = std::clamp(position, 0, GainTable::kNumPositions - 1);
        gainPosition_ = position;
        applyGainPosition(position);
    }

    /// Set the active path. 0 = Neve (Chemin A), 1 = Jensen (Chemin B).
    void setPath(int path)
    {
        targetPath_.store(std::clamp(path, 0, 1), std::memory_order_relaxed);
    }

    /// Enable/disable the PAD attenuator on T1.
    void setPadEnabled(bool enabled)
    {
        padEnabled_.store(enabled, std::memory_order_relaxed);
    }

    /// Set the transformer ratio. 0 = 1:5, 1 = 1:10.
    void setRatio(int ratio)
    {
        ratio_.store(std::clamp(ratio, 0, 1), std::memory_order_relaxed);
    }

    /// Enable/disable phase inversion on the output.
    void setPhaseInvert(bool invert)
    {
        phaseInvert_.store(invert, std::memory_order_relaxed);
    }

    /// Set the input gain in dB (applied before T1).
    void setInputGain(float dB)
    {
        inputGain_.setTargetValue(dBtoLinear(dB));
    }

    /// Set the output gain in dB (applied after T2 + mix).
    void setOutputGain(float dB)
    {
        outputGain_.setTargetValue(dBtoLinear(dB));
    }

    /// Set the dry/wet mix. 0.0 = fully dry, 1.0 = fully wet.
    void setMix(float wetDry)
    {
        mix_.setTargetValue(std::clamp(wetDry, 0.0f, 1.0f));
    }

    // ── Monitoring ────────────────────────────────────────────────────────────

    struct MonitorData
    {
        float inputLevel_dBu       = -120.0f;
        float outputLevel_dBu      = -120.0f;
        float t1_magnetizing_current = 0.0f;
        float t2_magnetizing_current = 0.0f;
        int   currentPath          = 0;
        int   gainPosition         = 5;
        bool  isClipping           = false;
    };

    /// Get current monitoring data (call from UI thread).
    MonitorData getMonitorData() const
    {
        MonitorData data;

        // Convert peak levels to dBu (0 dBu = 0.7746 Vrms)
        // For peak: dBu = 20*log10(Vpeak / (sqrt(2) * 0.7746))
        constexpr float kVrefPeak = static_cast<float>(kDBuRefVrms) * 1.4142135f;

        if (lastInputLevel_ > kEpsilonF)
            data.inputLevel_dBu = 20.0f * std::log10(lastInputLevel_ / kVrefPeak);

        if (lastOutputLevel_ > kEpsilonF)
            data.outputLevel_dBu = 20.0f * std::log10(lastOutputLevel_ / kVrefPeak);

        data.t1_magnetizing_current = inputStage_.getMagnetizingCurrent();
        data.t2_magnetizing_current = outputStage_.getT2MagnetizingCurrent();
        data.currentPath = targetPath_.load(std::memory_order_relaxed);
        data.gainPosition = gainPosition_;
        data.isClipping = (lastOutputLevel_ > 1.0f);   // 0 dBFS digital clip

        return data;
    }

    // ── Access to sub-stages ──────────────────────────────────────────────────

    InputStageWDF<NonlinearLeaf>&  getInputStage()  { return inputStage_; }
    OutputStageWDF<NonlinearLeaf>& getOutputStage() { return outputStage_; }
    NeveClassAPath&                getNevePath()    { return nevePath_; }
    JE990Path&                     getJE990Path()   { return je990Path_; }

    const InputStageWDF<NonlinearLeaf>&  getInputStage()  const { return inputStage_; }
    const OutputStageWDF<NonlinearLeaf>& getOutputStage() const { return outputStage_; }
    const NeveClassAPath&                getNevePath()    const { return nevePath_; }
    const JE990Path&                     getJE990Path()   const { return je990Path_; }

private:
    // ── Pipeline stages ───────────────────────────────────────────────────────
    InputStageWDF<NonlinearLeaf>  inputStage_;
    NeveClassAPath                nevePath_;
    JE990Path                     je990Path_;
    OutputStageWDF<NonlinearLeaf> outputStage_;

    // ── Configuration ─────────────────────────────────────────────────────────
    PreampConfig config_;
    bool configured_ = false;

    // ── Gain control ──────────────────────────────────────────────────────────
    GainTable gainTable_;
    int gainPosition_ = 5;  // Default: position 5 = +26 dB amplifier

    // ── Smoothed parameters (audio-thread-safe) ───────────────────────────────
    SmoothedValue<float> inputGain_{1.0f};
    SmoothedValue<float> outputGain_{1.0f};
    SmoothedValue<float> mix_{1.0f};

    // ── Atomic controls (set from UI thread, read on audio thread) ─────────
    std::atomic<int>  targetPath_{0};     // 0=Neve, 1=Jensen
    std::atomic<bool> padEnabled_{false};
    std::atomic<int>  ratio_{1};          // 0=1:5, 1=1:10
    std::atomic<bool> phaseInvert_{false};

    // ── Cached control state (audio thread only) ──────────────────────────────
    int  cachedPath_  = 0;
    bool cachedPad_   = false;
    int  cachedRatio_ = 1;

    // ── Audio state ───────────────────────────────────────────────────────────
    float sampleRate_     = kDefaultSampleRate;
    int   maxBlockSize_   = kMaxBlockSize;
    float lastInputLevel_ = 0.0f;
    float lastOutputLevel_ = 0.0f;

    // ── Scratch buffers ───────────────────────────────────────────────────────
    std::vector<float> neveBuffer_;
    std::vector<float> jensenBuffer_;

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Apply a gain position to both amplifier paths.
    void applyGainPosition(int position)
    {
        const float Rfb = GainTable::getRfb(position);
        nevePath_.setGain(Rfb);
        je990Path_.setGain(Rfb);
    }
};

} // namespace transfo
