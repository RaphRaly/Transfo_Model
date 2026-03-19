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

    // Preamp controls (Sprint 7)
    static const juce::String PreampGain    = "preampGain";     // int 0-10
    static const juce::String PreampPath    = "preampPath";     // int 0-1 (Neve/Jensen)
    static const juce::String PreampPad     = "preampPad";      // bool
    static const juce::String PreampRatio   = "preampRatio";    // int 0-1 (1:5/1:10)
    static const juce::String PreampPhase   = "preampPhase";    // bool
    static const juce::String PreampEnabled = "preampEnabled";  // bool (bypass preamp)
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

    // ── Preamp parameters (Sprint 7) ──────────────────────────────────────

    // Preamp Gain (11-position Grayhill switch)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamID::PreampGain, 1), "Preamp Gain",
        0, 10, 5));  // default position 6 (+30dB)

    // Preamp Path (0=Neve, 1=Jensen)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::PreampPath, 1), "Preamp Path",
        juce::StringArray{"Neve Heritage", "Jensen Heritage"}, 0));

    // Preamp PAD (20dB attenuation)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::PreampPad, 1), "PAD", false));

    // Preamp Ratio (0=1:5, 1=1:10)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::PreampRatio, 1), "Ratio",
        juce::StringArray{"1:5", "1:10"}, 1));  // default 1:10

    // Phase Invert
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::PreampPhase, 1), "Phase", false));

    // Preamp Enable/Bypass
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::PreampEnabled, 1), "Preamp", false));  // off by default

    return { params.begin(), params.end() };
}

} // namespace transfo
