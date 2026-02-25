#include "PluginEditor.h"
#include "ParameterLayout.h"

using namespace transfo;

static constexpr int kKnobW = 70;
static constexpr int kKnobH = 90;
static constexpr int kLabelH = 18;
static constexpr int kMargin = 8;
static constexpr int kSectionGap = 12;
static constexpr int kHeaderH = 32;

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(p), processorRef_(p)
{
    // ── Main knobs ──
    setupRotary(inputGain_,  ParamID::InputGain,  "Input");
    setupRotary(outputGain_, ParamID::OutputGain, "Output");
    setupRotary(mix_,        ParamID::Mix,        "Mix");
    setupRotary(tmtAmount_,  ParamID::TMTAmount,  "TMT");

    // ── Preset combo ──
    presetLabel_.setText("Transformer", juce::dontSendNotification);
    presetLabel_.setJustificationType(juce::Justification::centred);
    presetLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(presetLabel_);

    presetCombo_.addItemList({"Jensen JT-115K-E", "Neve Marinair LO1166", "API AP2503"}, 1);
    addAndMakeVisible(presetCombo_);
    presetAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef_.getAPVTS(), ParamID::Preset, presetCombo_);

    // ── Mode combo ──
    modeLabel_.setText("Mode", juce::dontSendNotification);
    modeLabel_.setJustificationType(juce::Justification::centred);
    modeLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(modeLabel_);

    modeCombo_.addItemList({"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)"}, 1);
    addAndMakeVisible(modeCombo_);
    modeAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef_.getAPVTS(), ParamID::Mode, modeCombo_);

    // ── J-A knobs (advanced) ──
    setupRotary(msSlider_,    ParamID::Ms,    "Ms");
    setupRotary(aSlider_,     ParamID::A,     "Shape");
    setupRotary(kSlider_,     ParamID::K,     "Coercivity");
    setupRotary(cSlider_,     ParamID::C,     "Revers.");
    setupRotary(alphaSlider_, ParamID::Alpha, "Coupling");

    // ── Monitor label ──
    monitorLabel_.setFont(juce::Font(12.0f));
    monitorLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    addAndMakeVisible(monitorLabel_);

    // Window size
    setSize(680, 340);

    // Start monitoring timer (10 Hz)
    startTimerHz(10);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
}

void PluginEditor::setupRotary(RotarySlider& rs, const juce::String& paramId,
                                const juce::String& text)
{
    rs.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    rs.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    addAndMakeVisible(rs.slider);

    rs.label.setText(text, juce::dontSendNotification);
    rs.label.setJustificationType(juce::Justification::centred);
    rs.label.setFont(juce::Font(12.0f));
    addAndMakeVisible(rs.label);

    rs.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef_.getAPVTS(), paramId, rs.slider);
}

void PluginEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xFF0F0F1E));

    // Title bar
    g.setColour(juce::Colour(0xFF1A1A3E));
    g.fillRect(0, 0, getWidth(), kHeaderH);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("Transformer Model v3", 0, 0, getWidth(), kHeaderH,
               juce::Justification::centred);

    // Section separators
    g.setColour(juce::Colour(0xFF2A2A4E));
    int y1 = kHeaderH + kLabelH + kKnobH + kMargin * 2 + 30;
    g.drawHorizontalLine(y1, kMargin, getWidth() - kMargin);

    // Section labels
    g.setColour(juce::Colour(0xFF8888AA));
    g.setFont(juce::Font(11.0f));
    g.drawText("MAIN", kMargin, kHeaderH + 2, 100, kLabelH, juce::Justification::left);
    g.drawText("J-A PARAMETERS", kMargin, y1 + 2, 200, kLabelH, juce::Justification::left);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(kHeaderH + kMargin);

    // ── Row 1: Main controls ──
    int x = kMargin;
    int y = area.getY();

    // Knobs: Input, Output, Mix, TMT
    RotarySlider* mainKnobs[] = { &inputGain_, &outputGain_, &mix_, &tmtAmount_ };
    for (auto* rs : mainKnobs)
    {
        rs->label.setBounds(x, y, kKnobW, kLabelH);
        rs->slider.setBounds(x, y + kLabelH, kKnobW, kKnobH);
        x += kKnobW + kMargin;
    }

    // Combos: Preset, Mode
    x += kSectionGap;
    presetLabel_.setBounds(x, y, 170, kLabelH);
    presetCombo_.setBounds(x, y + kLabelH + 2, 170, 24);
    modeLabel_.setBounds(x, y + kLabelH + 30, 170, kLabelH);
    modeCombo_.setBounds(x, y + kLabelH + 48, 170, 24);

    // ── Row 2: J-A Parameters ──
    y += kLabelH + kKnobH + kMargin + 20;
    x = kMargin;

    RotarySlider* jaKnobs[] = { &msSlider_, &aSlider_, &kSlider_, &cSlider_, &alphaSlider_ };
    for (auto* rs : jaKnobs)
    {
        rs->label.setBounds(x, y, kKnobW, kLabelH);
        rs->slider.setBounds(x, y + kLabelH, kKnobW, kKnobH);
        x += kKnobW + kMargin;
    }

    // Monitor label at bottom
    monitorLabel_.setBounds(kMargin, getHeight() - 22, getWidth() - 2 * kMargin, 20);
}

void PluginEditor::timerCallback()
{
    auto data = processorRef_.getMonitorData();

    juce::String text = "HSIM: " + juce::String(data.lastIterCount) + " iter"
                      + (data.lastConverged ? " OK" : " FAIL")
                      + " | Failures: " + juce::String(data.convergenceFailures);

#ifndef NDEBUG
    text += " | rho: " + juce::String(data.spectralRadius, 3);
#endif

    monitorLabel_.setText(text, juce::dontSendNotification);
}
