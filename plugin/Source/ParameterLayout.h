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
    static const juce::String SVU            = "svu";         // Stereo Variation Units
    static const juce::String Circuit        = "circuit";     // Legacy Cascade / WDF Circuit
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
        juce::StringArray{"Jensen JT-115K-E", "Jensen JT-11ELCF"},
        0));

    // Processing Mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Mode, 1), "Mode",
        juce::StringArray{"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)"},
        0));

    // SVU — Stereo Variation Units
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::SVU, 1), "SVU",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.1f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Circuit topology: Legacy Cascade (HP→J-A→LC) or WDF Circuit (Test Circuit 1)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Circuit, 1), "Circuit",
        juce::StringArray{"Legacy (Cascade)", "WDF Circuit"},
        0));

    return { params.begin(), params.end() };
}

} // namespace transfo
