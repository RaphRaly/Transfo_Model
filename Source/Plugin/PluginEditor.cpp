#include "Plugin/PluginEditor.h"

// =============================================================================
// Colour palette
// =============================================================================
namespace Colours
{
    static const juce::Colour bgTop        { 0xFF0D0D1A };
    static const juce::Colour bgBottom     { 0xFF1A1A2E };
    static const juce::Colour panelBg      { 0xFF16213E };
    static const juce::Colour panelBorder  { 0xFF2A2A4A };
    static const juce::Colour titleColour  { 0xFFE0E0FF };
    static const juce::Colour sectionTitle { 0xFF8888CC };
    static const juce::Colour labelColour  { 0xFFBBBBDD };

    // Arc colours per section
    static const juce::Colour arcInput     { 0xFF4FC3F7 };   // cyan
    static const juce::Colour arcMaterial  { 0xFFF0A500 };   // amber
    static const juce::Colour arcOutput    { 0xFF66BB6A };   // green
    static const juce::Colour arcDynamic   { 0xFFE57373 };   // red-orange for dynamic losses
}

// =============================================================================
// ModernLookAndFeel
// =============================================================================

ModernLookAndFeel::ModernLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFDDDDFF));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF0E0E1E));
    setColour(juce::Label::textColourId, Colours::labelColour);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF0E0E1E));
    setColour(juce::ComboBox::outlineColourId, Colours::panelBorder);
    setColour(juce::ComboBox::textColourId, juce::Colour(0xFFDDDDFF));
    setColour(juce::ComboBox::arrowColourId, Colours::sectionTitle);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF16213E));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFDDDDFF));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xFF2A2A4A));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xFFFFFFFF));
}

void ModernLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider& slider)
{
    const float diameter = (float)juce::jmin(width, height) * 0.82f;
    const float radius   = diameter * 0.5f;
    const float centreX  = (float)x + (float)width  * 0.5f;
    const float centreY  = (float)y + (float)height * 0.5f;
    const float angle    = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Per-slider arc colour from properties (fallback to member arcColour)
    juce::Colour currentArcColour = arcColour;
    if (slider.getProperties().contains("arcColour"))
        currentArcColour = juce::Colour((juce::uint32)(juce::int64)slider.getProperties()["arcColour"]);

    // Track (background arc)
    const float trackWidth = juce::jmax(2.5f, diameter * 0.06f);
    juce::Path bgArc;
    bgArc.addCentredArc(centreX, centreY, radius, radius,
                        0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xFF2A2A4A));
    g.strokePath(bgArc, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Value arc (coloured)
    if (sliderPos > 0.0f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius, radius,
                               0.0f, rotaryStartAngle, angle, true);
        g.setColour(currentArcColour);
        g.strokePath(valueArc, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // Knob body
    const float knobRadius = radius * 0.65f;
    juce::ColourGradient knobGrad(juce::Colour(0xFF3A3A5E), centreX, centreY - knobRadius,
                                   juce::Colour(0xFF1A1A30), centreX, centreY + knobRadius, false);
    g.setGradientFill(knobGrad);
    g.fillEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

    // Knob border
    g.setColour(juce::Colour(0xFF4A4A6E));
    g.drawEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.2f);

    // Pointer line
    const float pointerLength = knobRadius * 0.75f;
    const float pointerThickness = juce::jmax(2.0f, diameter * 0.04f);
    juce::Path pointer;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -knobRadius + 3.0f,
                                 pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(currentArcColour);
    g.fillPath(pointer);
}

void ModernLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                      juce::ComboBox& box)
{
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

void ModernLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(Colours::panelBorder);
    g.drawRect(0, 0, width, height, 1);
}

void ModernLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        g.setColour(label.findColour(juce::Label::textColourId).withAlpha(alpha));

        auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());
        auto font = label.getFont();
        g.setFont(font);
        g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                         juce::jmax(1, (int)((float)textArea.getHeight() / font.getHeight())),
                         label.getMinimumHorizontalScale());
    }
}

// =============================================================================
// PluginEditor
// =============================================================================

static constexpr int defaultW = 900;
static constexpr int defaultH = 520;
static constexpr int minW = 650;
static constexpr int minH = 400;

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(p), processorRef(p)
{
    setLookAndFeel(&modernLnf);

    // Setup sliders with per-section colours
    setupSlider(inputLevel,  "inputLevel",  "Input dB",      Colours::arcInput);
    setupSlider(outputLevel, "outputLevel", "Output dB",     Colours::arcOutput);
    setupSlider(msSlider,    "ms",          "Ms (A/m)",      Colours::arcMaterial);
    setupSlider(aSlider,     "a",           "Shape (a)",     Colours::arcMaterial);
    setupSlider(kSlider,     "k",           "Coercivity",    Colours::arcMaterial);
    setupSlider(cSlider,     "c",           "Revers. (c)",   Colours::arcMaterial);
    setupSlider(alphaSlider, "alpha",       "Coupling",      Colours::arcMaterial);
    setupSlider(kEddySlider,   "kEddy",    "K eddy",        Colours::arcDynamic);
    setupSlider(kExcessSlider,  "kExcess",  "K excess",      Colours::arcDynamic);

    // Oversampling combo box
    osLabel.setText("Oversampling", juce::dontSendNotification);
    osLabel.setJustificationType(juce::Justification::centred);
    osLabel.setFont(juce::Font(13.0f));
    addAndMakeVisible(osLabel);

    osCombo.addItemList({"2x", "4x", "8x"}, 1);
    addAndMakeVisible(osCombo);
    osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.getAPVTS(), "osOrder", osCombo);

    // Resizable window
    constrainer.setMinimumSize(minW, minH);
    constrainer.setFixedAspectRatio(0.0); // free aspect ratio
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(defaultW, defaultH);
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel(nullptr);
}

void PluginEditor::setupSlider(ParamSlider& ps, const juce::String& paramId,
                                const juce::String& labelText, juce::Colour arcCol)
{
    ps.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    ps.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    ps.slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFDDDDFF));
    ps.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    ps.slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF0E0E1E));

    // Store the arc colour in the slider's properties so the LookAndFeel can use it
    ps.slider.getProperties().set("arcColour", (juce::int64)arcCol.getARGB());

    addAndMakeVisible(ps.slider);

    ps.label.setText(labelText, juce::dontSendNotification);
    ps.label.setJustificationType(juce::Justification::centred);
    ps.label.setFont(juce::Font(13.0f));
    addAndMakeVisible(ps.label);

    ps.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), paramId, ps.slider);
}

void PluginEditor::drawSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                                const juce::String& title)
{
    // Panel background
    g.setColour(Colours::panelBg);
    g.fillRoundedRectangle(bounds.toFloat(), 10.0f);

    // Panel border
    g.setColour(Colours::panelBorder);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);

    // Section title
    g.setColour(Colours::sectionTitle);
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText(title, bounds.removeFromTop(28).reduced(12, 0),
               juce::Justification::centredLeft);
}

// =============================================================================
// Paint -- gradient background, title, section panels
// =============================================================================

void PluginEditor::paint(juce::Graphics& g)
{
    // Gradient background
    juce::ColourGradient bgGrad(Colours::bgTop, 0.0f, 0.0f,
                                 Colours::bgBottom, 0.0f, (float)getHeight(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    // Subtle top highlight line
    g.setColour(juce::Colour(0x15FFFFFF));
    g.fillRect(0, 0, getWidth(), 1);

    // Title bar area
    auto titleArea = getLocalBounds().removeFromTop(56);
    g.setColour(Colours::titleColour);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText("Transformer Model", titleArea.reduced(20, 0), juce::Justification::centredLeft);

    // Subtitle
    g.setColour(Colours::sectionTitle.withAlpha(0.6f));
    g.setFont(juce::Font(13.0f));
    g.drawText("Hysteresis Lab", titleArea.reduced(20, 0), juce::Justification::centredRight);

    // Draw section panels
    auto area = getLocalBounds().reduced(14);
    area.removeFromTop(56); // title space

    const int sectionGap = 10;
    const float totalWidth = (float)area.getWidth();

    // Input section (1 knob) ~ 15%
    auto inputArea = area.removeFromLeft((int)(totalWidth * 0.15f));
    drawSection(g, inputArea, "INPUT");

    area.removeFromLeft(sectionGap);

    // Output section (1 knob) ~ 15%
    auto outputArea = area.removeFromRight((int)(totalWidth * 0.15f));
    drawSection(g, outputArea, "OUTPUT");

    area.removeFromRight(sectionGap);

    // Material section (5 knobs + combo) ~ remaining
    drawSection(g, area, "MATERIAL PARAMETERS");
}

// =============================================================================
// Resized -- proportional layout
// =============================================================================

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(14);
    area.removeFromTop(56); // title

    const int sectionGap   = 10;
    const int sectionPadH  = 12;
    const int sectionPadV  = 10;
    const int titleBarH    = 28;
    const float totalWidth = (float)area.getWidth();

    // ----- INPUT section (1 knob) -----
    auto inputArea = area.removeFromLeft((int)(totalWidth * 0.15f));
    inputArea = inputArea.reduced(sectionPadH, sectionPadV);
    inputArea.removeFromTop(titleBarH);

    {
        auto knobArea = inputArea;
        int labelH = juce::jmax(18, knobArea.getHeight() / 8);
        inputLevel.label.setBounds(knobArea.removeFromTop(labelH));
        inputLevel.slider.setBounds(knobArea);
    }

    area.removeFromLeft(sectionGap);

    // ----- OUTPUT section (1 knob) -----
    auto outputArea = area.removeFromRight((int)(totalWidth * 0.15f));
    outputArea = outputArea.reduced(sectionPadH, sectionPadV);
    outputArea.removeFromTop(titleBarH);

    {
        auto knobArea = outputArea;
        int labelH = juce::jmax(18, knobArea.getHeight() / 8);
        outputLevel.label.setBounds(knobArea.removeFromTop(labelH));
        outputLevel.slider.setBounds(knobArea);
    }

    area.removeFromRight(sectionGap);

    // ----- MATERIAL section (5 static + 2 dynamic knobs + combo) -----
    auto matArea = area.reduced(sectionPadH, sectionPadV);
    matArea.removeFromTop(titleBarH);

    // Top row: 5 static J-A knobs
    auto topRow = matArea.removeFromTop(matArea.getHeight() * 2 / 3);
    ParamSlider* staticSliders[] = { &msSlider, &aSlider, &kSlider, &cSlider, &alphaSlider };
    const int numStaticKnobs = 5;
    const int staticKnobW = topRow.getWidth() / numStaticKnobs;

    for (int i = 0; i < numStaticKnobs; ++i)
    {
        auto col = topRow.removeFromLeft(staticKnobW);
        int labelH = juce::jmax(18, col.getHeight() / 8);
        staticSliders[i]->label.setBounds(col.removeFromTop(labelH));
        staticSliders[i]->slider.setBounds(col);
    }

    // Bottom row: 2 dynamic loss knobs + oversampling combo
    auto dynRow = matArea;
    const int dynKnobW = dynRow.getWidth() / 3;

    {
        auto col = dynRow.removeFromLeft(dynKnobW);
        int labelH = juce::jmax(18, col.getHeight() / 8);
        kEddySlider.label.setBounds(col.removeFromTop(labelH));
        kEddySlider.slider.setBounds(col);
    }

    {
        auto col = dynRow.removeFromLeft(dynKnobW);
        int labelH = juce::jmax(18, col.getHeight() / 8);
        kExcessSlider.label.setBounds(col.removeFromTop(labelH));
        kExcessSlider.slider.setBounds(col);
    }

    // Oversampling combo in remaining space
    {
        auto comboArea = dynRow.reduced(4, 4);
        int labelH = juce::jmin(22, comboArea.getHeight() / 2);
        osLabel.setBounds(comboArea.removeFromTop(labelH));
        osCombo.setBounds(comboArea.reduced(comboArea.getWidth() / 6, 2));
    }
}
