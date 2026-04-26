#include "DSP/Oversampling.h"

OversamplingWrapper::OversamplingWrapper() = default;

void OversamplingWrapper::prepare(double sampleRate, int samplesPerBlock,
                                   int numChannels, int order)
{
    order_ = order;
    factor = 1 << order;  // 2^order
    oversampledRate = sampleRate * factor;

    // Polyphase IIR halfband filters — good balance of quality vs CPU
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        numChannels,
        order,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true    // isMaxQuality
    );

    oversampler->initProcessing(static_cast<size_t>(samplesPerBlock));
}

void OversamplingWrapper::reset()
{
    if (oversampler)
        oversampler->reset();
}

float OversamplingWrapper::getLatency() const
{
    if (oversampler)
        return oversampler->getLatencyInSamples();
    return 0.0f;
}
