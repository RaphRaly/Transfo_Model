#pragma once
#include <juce_dsp/juce_dsp.h>
#include <memory>

// =============================================================================
// OversamplingWrapper — JUCE dsp::Oversampling wrapper.
//
// Oversampling is CRITICAL for the J-A model: the Newton-Raphson solver
// converges much faster when samples are close together (small dH between
// steps). 8x oversampling is recommended for precision.
//
// Using JUCE's built-in polyphase IIR halfband filters for the up/down
// conversion (good anti-aliasing with reasonable latency).
// =============================================================================

class OversamplingWrapper
{
public:
    OversamplingWrapper();

    // order: oversampling factor = 2^order (1=2x, 2=4x, 3=8x)
    void prepare(double sampleRate, int samplesPerBlock, int numChannels, int order = 3);
    void reset();

    // Returns the oversampled sample rate (e.g. 44100 * 8 = 352800)
    double getOversampledRate() const { return oversampledRate; }

    // Returns the oversampling factor (e.g. 8)
    int getFactor() const { return factor; }

    // Returns latency in samples (at original rate)
    float getLatency() const;

    // Access the JUCE oversampling object for processBlock integration
    juce::dsp::Oversampling<float>& getOversampler() { return *oversampler; }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double oversampledRate = 44100.0;
    int factor = 8;
    int order_ = 3;
};
