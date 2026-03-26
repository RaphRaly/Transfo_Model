#pragma once

// =============================================================================
// HarrisonMicPre — Top-level Harrison Console Mic Preamp model.
//
// Signal chain:
//   1. Balanced input (termination + phase reverse + 20dB PAD)
//   2. Transformer (external dependency, injected via template)
//   3. U20 op-amp gain stage with MIC GAIN pot (IIR biquad)
//
// Template parameter TransformerModelType must satisfy:
//   - void processBlock(const float* in, float* out, int numSamples)
//   - float getTurnsRatio() const
//
// Click-free switching: PAD and phase reverse use 2ms crossfades
// to avoid audible discontinuities.
//
// Total gain:
//   G_total = p * A_term * beta * N * H(s, alpha)
//   where p = +/-1, A_term ~ 0.989, beta = 1 or 0.10526,
//   N = turns ratio, H(s,alpha) = gain stage transfer function.
//
// Reference: Harrison console schematic, 8 verified zoom photos.
// =============================================================================

#include "ComponentValues.h"
#include "OpAmpGainStage.h"
#include "../util/SmoothedValue.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace Harrison {
namespace MicPre {

template <typename TransformerModelType>
class HarrisonMicPre
{
public:
    HarrisonMicPre() = default;

    // ── Configuration ────────────────────────────────────────────────────

    /// Set transformer model (external dependency).
    /// Caller retains ownership. Must remain valid for the lifetime of this object.
    void setTransformer(TransformerModelType* transformer)
    {
        transformer_ = transformer;
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    /// Prepare for playback. Call once before processing.
    void prepareToPlay(float sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;

        // Gain stage IIR filter
        gainStage_.prepare(sampleRate);

        // Alpha smoothing: 5ms ramp (prevents zipper noise on pot)
        alphaSmooth_.reset(static_cast<double>(sampleRate), 0.005);
        alphaSmooth_.setCurrentAndTargetValue(0.5f);

        // Input scale smoothing: 2ms ramp (click-free PAD/phase switching)
        inputScaleSmooth_.reset(static_cast<double>(sampleRate), 0.002);
        inputScaleSmooth_.setCurrentAndTargetValue(computeInputScale());

        // Temp buffer for transformer processing
        tempBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0f);
    }

    /// Reset all internal state.
    void reset()
    {
        gainStage_.reset();
        alphaSmooth_.skip();
        inputScaleSmooth_.skip();
        std::fill(tempBuffer_.begin(), tempBuffer_.end(), 0.0f);
    }

    // ── Parameter Control ────────────────────────────────────────────────

    /// Set mic gain knob position. 0.0 = min gain, 1.0 = max gain.
    /// Internally mapped to alpha = 1 - micGain (pot convention: MIN at top).
    void setMicGain(float micGain01)
    {
        const float alpha = 1.0f - std::clamp(micGain01, 0.0f, 1.0f);
        alphaSmooth_.setTargetValue(alpha);
    }

    /// Enable/disable 20dB PAD. Click-free via smoothed input scale.
    void setPadEnabled(bool enabled)
    {
        padEnabled_ = enabled;
        inputScaleSmooth_.setTargetValue(computeInputScale());
    }

    /// Enable/disable phase reverse. Click-free via smoothed input scale.
    void setPhaseReverse(bool reversed)
    {
        phaseReversed_ = reversed;
        inputScaleSmooth_.setTargetValue(computeInputScale());
    }

    /// Set source impedance (Ohm) for termination attenuation calculation.
    /// Typical: 150 Ohm for dynamic mic, 50 Ohm for low-Z, 600 Ohm for line.
    void setSourceImpedance(float impedanceOhm)
    {
        sourceImpedance_ = std::max(0.0f, impedanceOhm);
        inputScaleSmooth_.setTargetValue(computeInputScale());
    }

    // ── Processing ───────────────────────────────────────────────────────

    /// Process a block of audio through the complete mic pre chain.
    /// In-place: data is both input and output.
    void processBlock(float* data, int numSamples)
    {
        // ---- Phase 1: Input scaling (balanced termination + phase + PAD) ----
        // Applied per-sample for click-free transitions
        for (int i = 0; i < numSamples; ++i)
            data[i] *= inputScaleSmooth_.getNextValue();

        // ---- Phase 2: Transformer (external model, block processing) ----
        if (transformer_ != nullptr)
            transformer_->processBlock(data, data, numSamples);

        // ---- Phase 3: U20 gain stage (per-sample, smoothed alpha) ----
        for (int i = 0; i < numSamples; ++i)
        {
            const float alpha = alphaSmooth_.getNextValue();

            // Recalculate IIR coefficients only when alpha changes
            if (gainStage_.needsUpdate(alpha))
                gainStage_.updateCoefficients(alpha);

            data[i] = gainStage_.processSample(data[i]);
        }
    }

    /// Process a block from JUCE AudioBuffer (convenience overload).
    /// Processes only the specified channel.
    void processChannel(float* channelData, int numSamples)
    {
        processBlock(channelData, numSamples);
    }

    // ── Monitoring ───────────────────────────────────────────────────────

    /// Get current mid-band gain in dB (for UI display).
    float getCurrentGainDb() const
    {
        const float alpha = alphaSmooth_.getCurrentValue();
        const float midGain = gainStage_.getMidBandGain(alpha);
        const float totalLinear = std::abs(inputScaleSmooth_.getCurrentValue())
                                * midGain;
        if (totalLinear < 1e-15f) return -300.0f;
        return 20.0f * std::log10(totalLinear);
    }

    /// Get current alpha (pot position, 0=max gain, 1=min gain).
    float getCurrentAlpha() const
    {
        return alphaSmooth_.getCurrentValue();
    }

    bool isPadEnabled() const { return padEnabled_; }
    bool isPhaseReversed() const { return phaseReversed_; }

private:

    /// Compute the combined input scaling factor: p * A_term * beta.
    float computeInputScale() const
    {
        const float p = phaseReversed_ ? -1.0f : 1.0f;
        const float beta = padEnabled_ ? PAD_ATTENUATION : 1.0f;
        // A_term = R100 / (R100 + Z_s/2) -- balanced termination
        const float A_term = R100 / (R100 + sourceImpedance_ * 0.5f);
        return p * A_term * beta;
    }

    // ── State ────────────────────────────────────────────────────────────

    float sampleRate_ = 44100.0f;

    // External transformer model (not owned)
    TransformerModelType* transformer_ = nullptr;

    // U20 gain stage filter
    OpAmpGainStage gainStage_;

    // Smoothed parameters
    transfo::SmoothedValue<float> alphaSmooth_{0.5f};
    transfo::SmoothedValue<float> inputScaleSmooth_{1.0f};

    // Switch states
    bool padEnabled_ = false;
    bool phaseReversed_ = false;
    float sourceImpedance_ = 150.0f;  // default: dynamic mic

    // Temp buffer for block processing
    std::vector<float> tempBuffer_;
};

} // namespace MicPre
} // namespace Harrison
