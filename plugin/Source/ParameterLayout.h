#pragma once

// =============================================================================
// ParameterLayout — Centralized APVTS parameter definitions.
//
// All parameter IDs and layout in one place. Referenced by both
// PluginProcessor and PluginEditor.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>

namespace transfo {

// ─── Parameter IDs ──────────────────────────────────────────────────────────
namespace ParamID
{
    static const juce::String InputGain     = "inputGain";
    static const juce::String OutputGain    = "outputGain";
    static const juce::String Mix           = "mix";
    static const juce::String Preset        = "preset";
    static const juce::String Mode          = "mode";        // Physical / Realtime
    static const juce::String TMTAmount     = "tmtAmount";   // Tolerance stereo spread

    // J-A parameters (exposed for lab use)
    static const juce::String Ms            = "ms";
    static const juce::String A             = "a";
    static const juce::String K             = "k";
    static const juce::String C             = "c";
    static const juce::String Alpha         = "alpha";
}

// ─── Create parameter layout ────────────────────────────────────────────────
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input Gain (dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::InputGain, 1), "Input Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Output Gain (dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::OutputGain, 1), "Output Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Dry/Wet Mix
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::Mix, 1), "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Transformer Preset
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Preset, 1), "Transformer",
        juce::StringArray{"Jensen JT-115K-E", "Neve Marinair LO1166", "API AP2503"},
        0));

    // Processing Mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Mode, 1), "Mode",
        juce::StringArray{"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)"},
        0));

    // TMT Stereo Amount
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::TMTAmount, 1), "TMT Stereo",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.1f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // J-A Parameters (lab/advanced)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::Ms, 1), "Saturation (Ms)",
        juce::NormalisableRange<float>(1e4f, 2e6f, 100.0f, 0.3f), 5.5e5f,
        juce::AudioParameterFloatAttributes().withLabel("A/m")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::A, 1), "Shape (a)",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.4f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel("A/m")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::K, 1), "Coercivity (k)",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 0.1f, 0.4f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel("A/m")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::C, 1), "Reversibility (c)",
        juce::NormalisableRange<float>(0.01f, 0.99f, 0.001f), 0.85f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::Alpha, 1), "Coupling (alpha)",
        juce::NormalisableRange<float>(1e-7f, 1e-1f, 1e-7f, 0.2f), 1e-4f));

    return { params.begin(), params.end() };
}

} // namespace transfo
