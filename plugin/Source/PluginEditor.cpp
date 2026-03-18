#include "PluginEditor.h"
#include "ParameterLayout.h"

using namespace transfo;

// =============================================================================
// Colour palette
// =============================================================================
namespace UiColours {
static const juce::Colour bgTop{0xFF0D0D1A};
static const juce::Colour bgBottom{0xFF1A1A2E};
static const juce::Colour panelBg{0xFF16213E};
static const juce::Colour panelBorder{0xFF2A2A4A};
static const juce::Colour titleColour{0xFFE0E0FF};
static const juce::Colour sectionTitle{0xFF8888CC};
static const juce::Colour labelColour{0xFFBBBBDD};
static const juce::Colour textBoxBg{0xFF0E0E1E};
static const juce::Colour textBoxText{0xFFDDDDFF};

// Arc colours per section
static const juce::Colour arcInput{0xFF4FC3F7};   // cyan
static const juce::Colour arcOutput{0xFF66BB6A};   // green
static const juce::Colour arcMix{0xFFCE93D8};      // purple
static const juce::Colour arcSVU{0xFFF0A500};      // amber
} // namespace UiColours

// =============================================================================
// ModernLookAndFeel
// =============================================================================

ModernLookAndFeel::ModernLookAndFeel() {
  setColour(juce::Slider::textBoxTextColourId, UiColours::textBoxText);
  setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
  setColour(juce::Slider::textBoxBackgroundColourId, UiColours::textBoxBg);
  setColour(juce::Label::textColourId, UiColours::labelColour);
  setColour(juce::ComboBox::backgroundColourId, UiColours::textBoxBg);
  setColour(juce::ComboBox::outlineColourId, UiColours::panelBorder);
  setColour(juce::ComboBox::textColourId, UiColours::textBoxText);
  setColour(juce::ComboBox::arrowColourId, UiColours::sectionTitle);
  setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF16213E));
  setColour(juce::PopupMenu::textColourId, UiColours::textBoxText);
  setColour(juce::PopupMenu::highlightedBackgroundColourId,
            juce::Colour(0xFF2A2A4A));
  setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void ModernLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                          int width, int height,
                                          float sliderPos,
                                          float rotaryStartAngle,
                                          float rotaryEndAngle,
                                          juce::Slider &slider) {
  const float diameter = (float)juce::jmin(width, height) * 0.82f;
  const float radius = diameter * 0.5f;
  const float centreX = (float)x + (float)width * 0.5f;
  const float centreY = (float)y + (float)height * 0.5f;
  const float angle =
      rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  // Per-slider arc colour from properties
  juce::Colour arcCol{0xFFF0A500};
  if (slider.getProperties().contains("arcColour"))
    arcCol = juce::Colour(
        (juce::uint32)(juce::int64)slider.getProperties()["arcColour"]);

  // Track (background arc)
  const float trackWidth = juce::jmax(2.5f, diameter * 0.06f);
  juce::Path bgArc;
  bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                      rotaryStartAngle, rotaryEndAngle, true);
  g.setColour(juce::Colour(0xFF2A2A4A));
  g.strokePath(bgArc, juce::PathStrokeType(trackWidth,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

  // Value arc (coloured)
  if (sliderPos > 0.0f) {
    juce::Path valueArc;
    valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                           rotaryStartAngle, angle, true);
    g.setColour(arcCol);
    g.strokePath(valueArc, juce::PathStrokeType(trackWidth,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
  }

  // Knob body with gradient
  const float knobRadius = radius * 0.65f;
  juce::ColourGradient knobGrad(juce::Colour(0xFF3A3A5E), centreX,
                                 centreY - knobRadius,
                                 juce::Colour(0xFF1A1A30), centreX,
                                 centreY + knobRadius, false);
  g.setGradientFill(knobGrad);
  g.fillEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f,
                knobRadius * 2.0f);

  // Knob border
  g.setColour(juce::Colour(0xFF4A4A6E));
  g.drawEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f,
                knobRadius * 2.0f, 1.2f);

  // Pointer
  const float pointerLength = knobRadius * 0.75f;
  const float pointerThickness = juce::jmax(2.0f, diameter * 0.04f);
  juce::Path pointer;
  pointer.addRoundedRectangle(-pointerThickness * 0.5f, -knobRadius + 3.0f,
                               pointerThickness, pointerLength,
                               pointerThickness * 0.5f);
  pointer.applyTransform(
      juce::AffineTransform::rotation(angle).translated(centreX, centreY));
  g.setColour(arcCol);
  g.fillPath(pointer);
}

void ModernLookAndFeel::drawComboBox(juce::Graphics &g, int width, int height,
                                      bool, int, int, int, int,
                                      juce::ComboBox &box) {
  auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
  g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(box.findColour(juce::ComboBox::outlineColourId));
  g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

  // Arrow
  auto arrowZone = bounds.removeFromRight(height).reduced(height * 0.3f);
  juce::Path arrow;
  arrow.addTriangle(arrowZone.getX(), arrowZone.getY(),
                    arrowZone.getRight(), arrowZone.getY(),
                    arrowZone.getCentreX(), arrowZone.getBottom());
  g.setColour(box.findColour(juce::ComboBox::arrowColourId));
  g.fillPath(arrow);
}

void ModernLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width,
                                                  int height) {
  g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
  g.setColour(UiColours::panelBorder);
  g.drawRect(0, 0, width, height, 1);
}

// =============================================================================
// PluginEditor
// =============================================================================

static constexpr int kDefaultW = 920;
static constexpr int kDefaultH = 500;
static constexpr int kMinW = 680;
static constexpr int kMinH = 380;

PluginEditor::PluginEditor(PluginProcessor &p)
    : AudioProcessorEditor(p), processorRef_(p), bhScope_(p) {
  setLookAndFeel(&modernLnf_);

  // ── Main knobs ──
  setupRotary(inputGain_, ParamID::InputGain, "Input", UiColours::arcInput);
  setupRotary(outputGain_, ParamID::OutputGain, "Output", UiColours::arcOutput);
  setupRotary(mix_, ParamID::Mix, "Mix", UiColours::arcMix);
  setupRotary(svuAmount_, ParamID::SVU, "SVU", UiColours::arcSVU);

  addAndMakeVisible(bhScope_);

  // ── Preset combo ──
  presetLabel_.setText("Transformer", juce::dontSendNotification);
  presetLabel_.setJustificationType(juce::Justification::centred);
  presetLabel_.setFont(juce::Font(13.0f));
  addAndMakeVisible(presetLabel_);

  presetCombo_.addItemList(
      {"Jensen JT-115K-E"},
      1);
  addAndMakeVisible(presetCombo_);
  presetAttach_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processorRef_.getAPVTS(), ParamID::Preset, presetCombo_);

  // ── Mode combo ──
  modeLabel_.setText("Mode", juce::dontSendNotification);
  modeLabel_.setJustificationType(juce::Justification::centred);
  modeLabel_.setFont(juce::Font(13.0f));
  addAndMakeVisible(modeLabel_);

  modeCombo_.addItemList({"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)"}, 1);
  addAndMakeVisible(modeCombo_);
  modeAttach_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processorRef_.getAPVTS(), ParamID::Mode, modeCombo_);

  // ── Circuit combo ──
  circuitLabel_.setText("Circuit", juce::dontSendNotification);
  circuitLabel_.setJustificationType(juce::Justification::centred);
  circuitLabel_.setFont(juce::Font(13.0f));
  addAndMakeVisible(circuitLabel_);

  circuitCombo_.addItemList({"Legacy (Cascade)", "WDF Circuit"}, 1);
  addAndMakeVisible(circuitCombo_);
  circuitAttach_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processorRef_.getAPVTS(), ParamID::Circuit, circuitCombo_);

  // ── Monitor label ──
  monitorLabel_.setFont(juce::Font(12.0f));
  monitorLabel_.setColour(juce::Label::textColourId,
                          juce::Colour(0xFF66BB6A));
  addAndMakeVisible(monitorLabel_);

  // ── Resizable window ──
  constrainer_.setMinimumSize(kMinW, kMinH);
  setConstrainer(&constrainer_);
  setResizable(true, true);
  setSize(kDefaultW, kDefaultH);

  startTimerHz(10);
}

PluginEditor::~PluginEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void PluginEditor::setupRotary(RotarySlider &rs, const juce::String &paramId,
                                const juce::String &text, juce::Colour arcCol) {
  rs.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  rs.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 20);
  rs.slider.setColour(juce::Slider::textBoxTextColourId,
                      UiColours::textBoxText);
  rs.slider.setColour(juce::Slider::textBoxOutlineColourId,
                      juce::Colour(0x00000000));
  rs.slider.setColour(juce::Slider::textBoxBackgroundColourId,
                      UiColours::textBoxBg);
  rs.slider.getProperties().set("arcColour", (juce::int64)arcCol.getARGB());
  addAndMakeVisible(rs.slider);

  rs.label.setText(text, juce::dontSendNotification);
  rs.label.setJustificationType(juce::Justification::centred);
  rs.label.setFont(juce::Font(13.0f, juce::Font::bold));
  addAndMakeVisible(rs.label);

  rs.attachment =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processorRef_.getAPVTS(), paramId, rs.slider);
}

void PluginEditor::drawSection(juce::Graphics &g, juce::Rectangle<int> bounds,
                                const juce::String &title) {
  // Panel background
  g.setColour(UiColours::panelBg);
  g.fillRoundedRectangle(bounds.toFloat(), 10.0f);

  // Panel border
  g.setColour(UiColours::panelBorder);
  g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);

  // Section title
  g.setColour(UiColours::sectionTitle);
  g.setFont(juce::Font(13.0f, juce::Font::bold));
  g.drawText(title, bounds.removeFromTop(26).reduced(12, 0),
             juce::Justification::centredLeft);
}

// =============================================================================
// Paint
// =============================================================================

void PluginEditor::paint(juce::Graphics &g) {
  // Gradient background
  juce::ColourGradient bgGrad(UiColours::bgTop, 0.0f, 0.0f,
                               UiColours::bgBottom, 0.0f, (float)getHeight(),
                               false);
  g.setGradientFill(bgGrad);
  g.fillRect(getLocalBounds());

  // Subtle top highlight
  g.setColour(juce::Colour(0x15FFFFFF));
  g.fillRect(0, 0, getWidth(), 1);

  // Title bar
  auto titleArea = getLocalBounds().removeFromTop(50);
  g.setColour(UiColours::titleColour);
  g.setFont(juce::Font(22.0f, juce::Font::bold));
  g.drawText("Transformer Model v3", titleArea.reduced(20, 0),
             juce::Justification::centredLeft);

  g.setColour(UiColours::sectionTitle.withAlpha(0.5f));
  g.setFont(juce::Font(12.0f));
  g.drawText("Physical Saturation", titleArea.reduced(20, 0),
             juce::Justification::centredRight);

  // Section panels
  auto area = getLocalBounds().reduced(12);
  area.removeFromTop(50);
  auto bottomBar = area.removeFromBottom(28);

  const int gap = 10;
  const int topRowH = (int)(area.getHeight() * 0.65f);

  auto topRow = area.removeFromTop(topRowH);
  area.removeFromTop(gap);
  auto botRow = area;

  // Top-left: CONTROLS panel
  int controlsW = juce::jmax(280, (int)(topRow.getWidth() * 0.35f));
  auto controlsArea = topRow.removeFromLeft(controlsW);
  drawSection(g, controlsArea, "CONTROLS");

  topRow.removeFromLeft(gap);

  // Top-right: B-H SCOPE panel
  drawSection(g, topRow, "B-H SCOPE");

  // Bottom: SETTINGS panel
  drawSection(g, botRow, "SETTINGS");
}

// =============================================================================
// Resized
// =============================================================================

void PluginEditor::resized() {
  auto area = getLocalBounds().reduced(12);
  area.removeFromTop(50); // title
  auto bottomBar = area.removeFromBottom(28);

  const int gap = 10;
  const int pad = 10;
  const int sectionTitleH = 26;
  const int topRowH = (int)(area.getHeight() * 0.65f);

  auto topRow = area.removeFromTop(topRowH);
  area.removeFromTop(gap);
  auto botRow = area;

  // ── CONTROLS panel (top-left) ──
  int controlsW = juce::jmax(280, (int)(topRow.getWidth() * 0.35f));
  auto controlsArea = topRow.removeFromLeft(controlsW);
  controlsArea = controlsArea.reduced(pad);
  controlsArea.removeFromTop(sectionTitleH);

  // 4 knobs in a row
  RotarySlider *knobs[] = {&inputGain_, &outputGain_, &mix_, &svuAmount_};
  const int numKnobs = 4;
  const int knobW = controlsArea.getWidth() / numKnobs;

  for (int i = 0; i < numKnobs; ++i) {
    auto col = controlsArea.removeFromLeft(knobW);
    int labelH = juce::jmax(18, col.getHeight() / 7);
    knobs[i]->label.setBounds(col.removeFromTop(labelH));
    knobs[i]->slider.setBounds(col);
  }

  topRow.removeFromLeft(gap);

  // ── B-H SCOPE panel (top-right) ──
  auto scopeArea = topRow.reduced(pad);
  scopeArea.removeFromTop(sectionTitleH);
  bhScope_.setBounds(scopeArea);

  // ── SETTINGS panel (bottom) ──
  auto settingsInner = botRow.reduced(pad);
  settingsInner.removeFromTop(sectionTitleH);

  const int colW = settingsInner.getWidth() / 3;

  // Preset combo (left third)
  {
    auto presetArea = settingsInner.removeFromLeft(colW).reduced(4, 0);
    int labelH = juce::jmin(20, presetArea.getHeight() / 2);
    presetLabel_.setBounds(presetArea.removeFromTop(labelH));
    presetCombo_.setBounds(
        presetArea.removeFromTop(juce::jmin(28, presetArea.getHeight()))
            .reduced(0, 2));
  }

  // Mode combo (center third)
  {
    auto modeArea = settingsInner.removeFromLeft(colW).reduced(4, 0);
    int labelH = juce::jmin(20, modeArea.getHeight() / 2);
    modeLabel_.setBounds(modeArea.removeFromTop(labelH));
    modeCombo_.setBounds(
        modeArea.removeFromTop(juce::jmin(28, modeArea.getHeight()))
            .reduced(0, 2));
  }

  // Circuit combo (right third)
  {
    auto circuitArea = settingsInner.reduced(4, 0);
    int labelH = juce::jmin(20, circuitArea.getHeight() / 2);
    circuitLabel_.setBounds(circuitArea.removeFromTop(labelH));
    circuitCombo_.setBounds(
        circuitArea.removeFromTop(juce::jmin(28, circuitArea.getHeight()))
            .reduced(0, 2));
  }

  // Monitor label at the very bottom
  monitorLabel_.setBounds(bottomBar.reduced(12, 0));
}

void PluginEditor::timerCallback() {
  auto data = processorRef_.getMonitorData();

  juce::String text = "HSIM: " + juce::String(data.lastIterCount) + " iter" +
                      (data.lastConverged ? " OK" : " FAIL") +
                      " | Failures: " + juce::String(data.convergenceFailures);

#ifndef NDEBUG
  text += " | rho: " + juce::String(data.spectralRadius, 3);
#endif

  monitorLabel_.setText(text, juce::dontSendNotification);
}
