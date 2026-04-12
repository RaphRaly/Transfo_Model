#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"

// =============================================================================
// Custom LookAndFeel for modern rotary knobs with colored arcs
// =============================================================================

class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    juce::Colour arcColour { 0xFFF0A500 };  // default amber
};

// =============================================================================
// PluginEditor -- Polished resizable GUI
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
    ModernLookAndFeel modernLnf;

    // Slider + Label helper
    struct ParamSlider
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ParamSlider inputLevel;
    ParamSlider outputLevel;

    // Oversampling combo
    juce::ComboBox osCombo;
    juce::Label    osLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAttachment;

    void setupSlider(ParamSlider& ps, const juce::String& paramId,
                     const juce::String& labelText, juce::Colour arcCol);

    // Section painting helper
    void drawSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                     const juce::String& title);

    // Resizable constrainer
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
