#pragma once

// =============================================================================
// OversamplingEngine — Oversampling for Physical mode (OS 4x).
//
// Refactored from DSP/Oversampling.h. In v3 architecture:
//   - Realtime mode: NO oversampling (ADAA in CPWLLeaf replaces it)
//   - Physical mode: OS 4x for offline/render precision with J-A model
//
// Uses simple polyphase halfband FIR filters. No JUCE dependency in core/.
// The JUCE wrapper is in the plugin layer for convenience.
//
// [v3] Architecture: oversampling is ALWAYS external to the WDF graph,
// never inside the scattering chain. Confirmed by chowdsp_wdf study.
// =============================================================================

#include "../util/AlignedBuffer.h"
#include "../util/Constants.h"
#include <cmath>
#include <array>

namespace transfo {

// ─── Simple halfband FIR filter for up/downsampling ─────────────────────────
// 7-tap halfband filter: efficient, zero at odd samples.
// For production: replace with polyphase IIR (lower latency) or
// use the JUCE wrapper in the plugin layer.

class HalfbandFilter
{
public:
    HalfbandFilter() { reset(); }

    void reset()
    {
        for (auto& s : state_) s = 0.0f;
    }

    // Process one sample through the lowpass halfband filter
    float process(float input)
    {
        // Shift delay line
        for (int i = kNumTaps - 1; i > 0; --i)
            state_[i] = state_[i - 1];
        state_[0] = input;

        // Convolve with halfband FIR coefficients
        float out = 0.0f;
        for (int i = 0; i < kNumTaps; ++i)
            out += state_[i] * coeffs_[i];
        return out;
    }

private:
    static constexpr int kNumTaps = 7;
    // Halfband FIR coefficients (symmetric, every other coeff is zero except center)
    static constexpr float coeffs_[kNumTaps] = {
        -0.0625f, 0.0f, 0.5625f, 1.0f, 0.5625f, 0.0f, -0.0625f
    };
    float state_[kNumTaps]{};
};

// ─── Oversampling Engine ────────────────────────────────────────────────────

class OversamplingEngine
{
public:
    OversamplingEngine() = default;

    void prepare(float sampleRate, int maxBlockSize, int factor = kOversamplingPhysical)
    {
        factor_ = factor;
        originalRate_ = sampleRate;
        oversampledRate_ = sampleRate * static_cast<float>(factor);

        oversampledBuffer_.resize(maxBlockSize * factor);
        upsampleFilter_.reset();
        downsampleFilter_.reset();
    }

    void reset()
    {
        upsampleFilter_.reset();
        downsampleFilter_.reset();
        oversampledBuffer_.clear();
    }

    // ─── Upsample: insert zeros + lowpass filter ────────────────────────────
    void upsample(const float* input, int numSamples, float* output)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int j = 0; j < factor_; ++j)
            {
                float sample = (j == 0) ? input[i] * static_cast<float>(factor_) : 0.0f;
                output[i * factor_ + j] = upsampleFilter_.process(sample);
            }
        }
    }

    // ─── Downsample: lowpass filter + decimate ──────────────────────────────
    void downsample(const float* input, int numOversampledSamples, float* output)
    {
        int outIdx = 0;
        for (int i = 0; i < numOversampledSamples; ++i)
        {
            float filtered = downsampleFilter_.process(input[i]);
            if (i % factor_ == 0)
                output[outIdx++] = filtered;
        }
    }

    // ─── Convenience: get buffer for in-place oversampled processing ────────
    float* getOversampledBuffer() { return oversampledBuffer_.data(); }
    int    getOversampledSize(int originalSize) const { return originalSize * factor_; }

    float  getOversampledRate() const { return oversampledRate_; }
    int    getFactor() const { return factor_; }
    float  getLatency() const { return static_cast<float>(kFilterLatency) / static_cast<float>(factor_); }

private:
    int   factor_ = kOversamplingPhysical;
    float originalRate_ = kDefaultSampleRate;
    float oversampledRate_ = kDefaultSampleRate * kOversamplingPhysical;

    static constexpr int kFilterLatency = 3; // halfband filter group delay

    HalfbandFilter upsampleFilter_;
    HalfbandFilter downsampleFilter_;
    AlignedBuffer<float> oversampledBuffer_;
};

} // namespace transfo
