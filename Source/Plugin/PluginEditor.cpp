#include "Plugin/PluginEditor.h"

// =============================================================================
// PluginEditor — Phase 1 lab interface
// =============================================================================

static constexpr int sliderWidth  = 80;
static constexpr int sliderHeight = 120;
static constexpr int labelHeight  = 20;
static constexpr int margin       = 10;

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(p), processorRef(p)
{
    // Setup all parameter sliders
    setupSlider(inputLevel,  "inputLevel",  "Input dB");
    setupSlider(outputLevel, "outputLevel", "Output dB");
    setupSlider(msSlider,    "ms",          "Ms (A/m)");
    setupSlider(aSlider,     "a",           "Shape (a)");
    setupSlider(kSlider,     "k",           "Coercivity");
    setupSlider(cSlider,     "c",           "Revers. (c)");
    setupSlider(alphaSlider, "alpha",       "Coupling");

    // Oversampling combo box
    osLabel.setText("Oversampling", juce::dontSendNotification);
    osLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(osLabel);

    osCombo.addItemList({"2x", "4x", "8x"}, 1);
    addAndMakeVisible(osCombo);
    osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.getAPVTS(), "osOrder", osCombo);

    // Window size
    setSize(8 * (sliderWidth + margin) + margin, sliderHeight + 2 * labelHeight + 3 * margin);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::setupSlider(ParamSlider& ps, const juce::String& paramId,
                                const juce::String& labelText)
{
    ps.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    ps.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible(ps.slider);

    ps.label.setText(labelText, juce::dontSendNotification);
    ps.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ps.label);

    ps.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), paramId, ps.slider);
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A2E));

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("Transformer Model — Phase 1: Hysteresis Lab",
               getLocalBounds().removeFromTop(30),
               juce::Justification::centred);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(margin);
    area.removeFromTop(30); // space for title

    // Layout sliders in a row
    ParamSlider* sliders[] = {
        &inputLevel, &msSlider, &aSlider, &kSlider,
        &cSlider, &alphaSlider, &outputLevel
    };

    int x = margin;
    const int y = area.getY();

    for (auto* ps : sliders)
    {
        ps->label.setBounds(x, y, sliderWidth, labelHeight);
        ps->slider.setBounds(x, y + labelHeight, sliderWidth, sliderHeight);
        x += sliderWidth + margin;
    }

    // Oversampling combo in the last slot
    osLabel.setBounds(x, y, sliderWidth, labelHeight);
    osCombo.setBounds(x + 5, y + labelHeight + 40, sliderWidth - 10, 25);
}
