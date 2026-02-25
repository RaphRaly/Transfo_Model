#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"

// =============================================================================
// PluginEditor — Basic GUI for Phase 1 "Hysteresis Lab".
//
// Exposes all J-A parameters as sliders with labels showing physical units.
// This is a lab/development interface — a polished GUI can come later.
// =============================================================================

class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processorRef;

    // ─── Slider + Label helper ────────────────────────────────────────────────
    struct ParamSlider
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ParamSlider inputLevel;
    ParamSlider outputLevel;
    ParamSlider msSlider;
    ParamSlider aSlider;
    ParamSlider kSlider;
    ParamSlider cSlider;
    ParamSlider alphaSlider;

    // Oversampling combo
    juce::ComboBox osCombo;
    juce::Label    osLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAttachment;

    void setupSlider(ParamSlider& ps, const juce::String& paramId,
                     const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
