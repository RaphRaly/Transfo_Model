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

    // Harrison Console Mic Pre controls
    static const juce::String HarrisonMicGain = "harrisonMicGain";   // float 0-1 (continuous pot)
    static const juce::String HarrisonPad     = "harrisonPad";       // bool (20dB PAD)
    static const juce::String HarrisonPhase   = "harrisonPhase";     // bool (phase reverse)
    static const juce::String HarrisonSourceZ = "harrisonSourceZ";   // float 50-600 Ohm
    static const juce::String HarrisonDynLoss = "harrisonDynLoss";   // float 0-1 (Bertotti mix)

    // T2 Output Transformer Load (Sprint C.3)
    static const juce::String T2Load       = "t2Load";
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

    // Transformer Preset (8 factory presets — synced with Presets.h)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Preset, 1), "Transformer",
        juce::StringArray{
            "Jensen JT-115K-E", "Jensen JT-11ELCF",
            "Neve 10468 Input", "Neve LI1166 Output",
            "Clean DI", "Vocal Warmth",
            "Bass Thickener", "Master Glue"},
        0));

    // Processing Mode (P1.1: added 2x OS option)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Mode, 1), "Mode",
        juce::StringArray{"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)", "Physical (J-A+OS2x)"},
        0));

    // SVU — Stereo Variation Units
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::SVU, 1), "SVU",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.1f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Engine: Preamp (default), Legacy transformer-only, or Harrison Console
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::Circuit, 1), "Engine",
        juce::StringArray{"O.D.T Balanced Preamp", "Legacy (Transformer)", "Harrison Console"},
        0));

    // ── Preamp parameters (Sprint 7) ──────────────────────────────────────

    // Preamp Gain (11-position Grayhill switch)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamID::PreampGain, 1), "Preamp Gain",
        0, 10, 5));  // default position 6 (+30dB)

    // Preamp Path (0=Neve, 1=Jensen)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::PreampPath, 1), "Preamp Path",
        juce::StringArray{"Heritage Mode", "Modern"}, 0));

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

    // ── Harrison Console Mic Pre parameters ─────────────────────────────

    // Mic Gain (continuous pot, 0=min, 1=max)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::HarrisonMicGain, 1), "Mic Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    // 20dB PAD
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::HarrisonPad, 1), "Harrison PAD", false));

    // Phase Reverse
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::HarrisonPhase, 1), "Harrison Phase", false));

    // Source Impedance (Ohm)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::HarrisonSourceZ, 1), "Source Z",
        juce::NormalisableRange<float>(50.0f, 600.0f, 1.0f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel("Ohm")));

    // Dynamic Losses (Bertotti K1/K2 mix: 0 = quasi-static, 1 = full preset)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::HarrisonDynLoss, 1), "Dynamics",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // ── T2 Output Transformer Load (Sprint C.3) ─────────────────────────
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::T2Load, 1), "T2 Load",
        juce::StringArray{"600 Ohm (Broadcast)", "10k Ohm (Bridging)", "47k Ohm (Hi-Z)"},
        1));  // Default: Bridging (10k)

    return { params.begin(), params.end() };
}

} // namespace transfo
