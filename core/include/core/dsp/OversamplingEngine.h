#pragma once

// =============================================================================
// OversamplingEngine — Oversampling for Artistic mode (OS 4x).
//
// [v3.2] Corrected: Cascaded 2-stage halfband architecture.
//
// Architecture:
//   - 2 stages of 2x up/downsample (cascade = 4x total)
//   - Each stage: 53-tap Kaiser halfband FIR (β=7.86, ≥80 dB stopband)
//   - Halfband optimization: only 14 MACs per sample (13 odd-index + center)
//   - Correct gain: ×2 per upsample stage (compensates zero-stuffing)
//
// Design method: Kaiser window, A=80 dB, transition band 0.4π–0.6π
// Coefficients computed via scipy.signal.firwin, verified ≥80 dB stopband.
//
// Bugs fixed [v3.2]:
//   1. Cutoff was π/2 for 4x OS (needed π/4) → cascaded halfband solves this
//   2. Only 7 taps = ~25 dB stopband → now 53 taps = 80+ dB per stage
//   3. Gain was ×8 (factor × filter_DC_gain_2) → now ×1 (amplitude-preserving)
//   4. Latency was hardcoded 3 → now reflects true group delay (39 samples)
//
// Why cascaded halfband and not a single lowpass at π/4?
//   Each halfband stage has cutoff at π/2 of its rate — the natural Nyquist/2
//   boundary. The cascade gives effective cutoff at π/4 of the original rate,
//   exactly what 4x oversampling requires. Each stage can independently achieve
//   80+ dB stopband with only 53 taps (vs ~200+ for a single-stage π/4 FIR).
//   Also: the halfband zero structure halves the multiply-accumulate count.
//
// Reference: Oppenheim & Schafer ch.7 (FIR design); Kaiser 1974;
//            MIT RES.6-008 Lecture 17; Smith "Spectral Audio Signal Processing"
// =============================================================================

#include "../util/AlignedBuffer.h"
#include "../util/Constants.h"
#include <array>
#include <cmath>

namespace transfo {

// ─── Halfband FIR Filter — 53-tap Kaiser (β=7.86, ≥80 dB stopband) ──────────
//
// Halfband property: h[n]=0 for even n (except center h[26]≈0.5).
// Exploited: only 13 symmetric odd-index coefficients + center tap computed.
// Total cost: 14 multiplications + 26 additions per sample.
//
// Frequency response:
//   Passband:  0 to 0.4π — ripple < 0.001 dB
//   Transition: 0.4π to 0.6π
//   Stopband:  0.6π to π — attenuation ≥ 80 dB

class HalfbandFilter
{
public:
    HalfbandFilter() { reset(); }

    void reset()
    {
        state_.fill(0.0f);
        pos_ = 0;
    }

    float process(float input)
    {
        // Write input into circular delay line
        state_[pos_] = input;

        // Center tap: h[26] * x[n-26]
        float out = kCenterTap * readDelay(kCenterDelay);

        // Symmetric odd-index pairs: h[k] * (x[n-k] + x[n-(N-1-k)])
        // where k = 1,3,5,...,25 and N-1-k = 51,49,...,27
        for (int i = 0; i < kNumOddCoeffs; ++i)
        {
            const int k = 2 * i + 1;
            const int k_mirror = kNumTaps - 1 - k;
            out += kOddCoeffs[i] * (readDelay(k) + readDelay(k_mirror));
        }

        // Advance circular write pointer
        pos_ = (pos_ + 1) % kNumTaps;
        return out;
    }

private:
    static constexpr int kNumTaps = 53;
    static constexpr int kCenterDelay = 26;  // Group delay = (N-1)/2

    // Center tap (even index, the only non-zero even coefficient)
    static constexpr float kCenterTap = 4.9999478972e-01f;

    // 13 unique odd-index coefficients: h[1],h[3],...,h[25]
    // Symmetric pairs: h[2i+1] = h[N-1-(2i+1)]
    static constexpr int kNumOddCoeffs = 13;
    static constexpr float kOddCoeffs[kNumOddCoeffs] = {
        +8.6790341e-05f, -3.1347383e-04f, +7.9608460e-04f,
        -1.6901093e-03f, +3.1999384e-03f, -5.5895971e-03f,
        +9.2091572e-03f, -1.4563182e-02f, +2.2491810e-02f,
        -3.4691124e-02f, +5.5516009e-02f, -1.0102597e-01f,
        +3.1657627e-01f
    };

    std::array<float, kNumTaps> state_{};
    int pos_ = 0;

    // Read from circular delay line: delay=0 is current sample
    float readDelay(int delay) const
    {
        int idx = pos_ - delay;
        if (idx < 0) idx += kNumTaps;
        return state_[idx];
    }
};

// ─── P1.1: Oversampling factor selection ─────────────────────────────────
enum class OversamplingFactor { OS_2x = 2, OS_4x = 4 };

// ─── Oversampling Engine — Cascaded 2×2x = 4x (or single 2x) ───────────

class OversamplingEngine
{
public:
    OversamplingEngine() = default;

    void prepare(float sampleRate, int maxBlockSize, int factor = kOversamplingArtistic)
    {
        factor_ = factor;
        originalRate_ = sampleRate;
        oversampledRate_ = sampleRate * static_cast<float>(factor);

        oversampledBuffer_.resize(maxBlockSize * factor);
        intermediateBuffer_.resize(maxBlockSize * 2);

        reset();
    }

    void reset()
    {
        for (auto& f : upFilters_)   f.reset();
        for (auto& f : downFilters_) f.reset();
        oversampledBuffer_.clear();
        intermediateBuffer_.clear();
    }

    // ─── Upsample: cascade fs → 2fs (→ 4fs) ────────────────────────────────
    void upsample(const float* input, int numSamples, float* output)
    {
        if (factor_ == 2) {
            // P1.1: Single stage 2x
            upsample2x(input, numSamples, output, upFilters_[0]);
        } else {
            float* mid = intermediateBuffer_.data();
            upsample2x(input, numSamples, mid, upFilters_[0]);
            upsample2x(mid, numSamples * 2, output, upFilters_[1]);
        }
    }

    // ─── Downsample: cascade (4fs →) 2fs → fs ──────────────────────────────
    void downsample(const float* input, int numOversampledSamples, float* output)
    {
        if (factor_ == 2) {
            // P1.1: Single stage 2x
            downsample2x(input, numOversampledSamples, output, downFilters_[0]);
        } else {
            float* mid = intermediateBuffer_.data();
            downsample2x(input, numOversampledSamples, mid, downFilters_[0]);
            downsample2x(mid, numOversampledSamples / 2, output, downFilters_[1]);
        }
    }

    // ─── Accessors ──────────────────────────────────────────────────────────
    float* getOversampledBuffer()                     { return oversampledBuffer_.data(); }
    int    getOversampledSize(int originalSize) const { return originalSize * factor_; }
    float  getOversampledRate() const { return oversampledRate_; }
    int    getFactor() const { return factor_; }

    // Round-trip latency in original-rate samples.
    // 53-tap FIR group delay = 26 samples at its operating rate.
    // Upsample path:  stage1 (26 @ 2fs = 13) + stage2 (26 @ 4fs = 6.5) = 19.5
    // Downsample path: stage1 (26 @ 4fs = 6.5) + stage2 (26 @ 2fs = 13) = 19.5
    // Total: 39 original-rate samples.
    float getLatency() const {
        return (factor_ == 2) ? kLatency2x : kRoundTripLatency;
    }

private:
    int   factor_ = kOversamplingArtistic;
    float originalRate_ = kDefaultSampleRate;
    float oversampledRate_ = kDefaultSampleRate * kOversamplingArtistic;

    // Two cascade stages: [0]=first, [1]=second
    HalfbandFilter upFilters_[2];
    HalfbandFilter downFilters_[2];

    AlignedBuffer<float> oversampledBuffer_;
    AlignedBuffer<float> intermediateBuffer_;  // Holds 2fs intermediate data

    static constexpr float kRoundTripLatency = 39.0f;  // 4x OS
    static constexpr float kLatency2x = 13.0f;         // 2x OS: single halfband stage

    // ─── Single 2x upsample: zero-stuff + halfband lowpass + gain ────────
    // Gain ×2 compensates the energy loss from zero-stuffing:
    //   Zero-stuffed DC = original_DC / 2 → ×2 restores unity.
    static void upsample2x(const float* input, int numSamples,
                            float* output, HalfbandFilter& filter)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            output[2 * i]     = filter.process(input[i]) * 2.0f;
            output[2 * i + 1] = filter.process(0.0f)     * 2.0f;
        }
    }

    // ─── Single 2x downsample: halfband anti-alias + decimate ────────────
    static void downsample2x(const float* input, int numSamples,
                              float* output, HalfbandFilter& filter)
    {
        int outIdx = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            float filtered = filter.process(input[i]);
            if (i % 2 == 0)
                output[outIdx++] = filtered;
        }
    }
};

} // namespace transfo
