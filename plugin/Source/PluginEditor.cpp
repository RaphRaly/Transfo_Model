#include "PluginEditor.h"
#include "BinaryData.h"
#include "ParameterLayout.h"

using namespace transfo;

// =============================================================================
// SSL-inspired colour palette — dark charcoal console style
// =============================================================================
namespace SSL {
// Background tones
static const juce::Colour bgDark      {0xFF1A1A1E};  // main background
static const juce::Colour bgPanel     {0xFF222226};  // panel area
static const juce::Colour bgSection   {0xFF2A2A2E};  // section header strip
static const juce::Colour bgRecessed  {0xFF141418};  // recessed areas (scope, meters)

// Lines
static const juce::Colour divider     {0xFF3A3A3E};  // separator lines
static const juce::Colour dividerLight{0xFF4A4A4E};  // brighter divider (top edge)

// Text
static const juce::Colour textPrimary  {0xFFCCCCCC};  // labels
static const juce::Colour textSecondary{0xFF888888};  // secondary / dim
static const juce::Colour textValue    {0xFFEEEEEE};  // parameter values
static const juce::Colour textBrand   {0xFFDDDDDD};  // brand name

// Accents (per-section coloring)
static const juce::Colour accentAmber {0xFFE8A030};  // preamp / gain
static const juce::Colour accentGreen {0xFF4CAF50};  // active / on / pad
static const juce::Colour accentRed   {0xFFE53935};  // phase / clip
static const juce::Colour accentCyan  {0xFF4FC3F7};  // I/O
static const juce::Colour accentPurple{0xFFAB7CDB};  // mix / SVU

// Knob
static const juce::Colour knobBody    {0xFF2A2A30};
static const juce::Colour knobEdge    {0xFF3E3E44};
static const juce::Colour knobHigh    {0xFF4A4A50};  // top-light highlight
static const juce::Colour knobShadow  {0xFF111115};

// Buttons (illuminated)
static const juce::Colour btnOff      {0xFF2E2E34};  // dark off state
static const juce::Colour btnBorder   {0xFF3A3A40};
} // namespace SSL

// =============================================================================
// SSLLookAndFeel
// =============================================================================

SSLLookAndFeel::SSLLookAndFeel() {
  // Load embedded Space Grotesk font
  typefaceRegular_ = juce::Typeface::createSystemTypefaceFor(
      BinaryData::SpaceGroteskRegular_ttf, BinaryData::SpaceGroteskRegular_ttfSize);
  typefaceBold_ = juce::Typeface::createSystemTypefaceFor(
      BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize);

  // General dark theme
  setColour(juce::Slider::textBoxTextColourId,       SSL::textValue);
  setColour(juce::Slider::textBoxOutlineColourId,     juce::Colour(0x00000000));
  setColour(juce::Slider::textBoxBackgroundColourId,  SSL::bgRecessed);
  setColour(juce::Label::textColourId,                SSL::textPrimary);
  setColour(juce::ComboBox::backgroundColourId,       SSL::bgRecessed);
  setColour(juce::ComboBox::outlineColourId,          SSL::divider);
  setColour(juce::ComboBox::textColourId,             SSL::textValue);
  setColour(juce::ComboBox::arrowColourId,            SSL::textSecondary);
  setColour(juce::PopupMenu::backgroundColourId,      SSL::bgPanel);
  setColour(juce::PopupMenu::textColourId,            SSL::textValue);
  setColour(juce::PopupMenu::highlightedBackgroundColourId, SSL::bgSection);
  setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

juce::Typeface::Ptr SSLLookAndFeel::getTypefaceForFont(const juce::Font &font) {
  if (font.isBold() && typefaceBold_)
    return typefaceBold_;
  if (typefaceRegular_)
    return typefaceRegular_;
  return LookAndFeel_V4::getTypefaceForFont(font);
}

// ─────────────────────────────────────────────────────────────────────────────
// SSL-style rotary knob — dark body, thin pointer line, NO arc
// ─────────────────────────────────────────────────────────────────────────────
void SSLLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                       int width, int height,
                                       float sliderPos,
                                       float rotaryStartAngle,
                                       float rotaryEndAngle,
                                       juce::Slider &slider) {
  const float diameter = (float)juce::jmin(width, height) * 0.78f;
  const float radius   = diameter * 0.5f;
  const float centreX  = (float)x + (float)width  * 0.5f;
  const float centreY  = (float)y + (float)height * 0.5f;
  const float angle    = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  // Per-slider pointer colour
  juce::Colour ptrCol{0xFFCCCCCC};
  if (slider.getProperties().contains("arcColour"))
    ptrCol = juce::Colour((juce::uint32)(juce::int64)slider.getProperties()["arcColour"]);

  const float knobR = radius * 0.72f;

  // ── 1. Drop shadow ────────────────────────────────────────────────────
  g.setColour(juce::Colour(0x35000000));
  g.fillEllipse(centreX - knobR + 1.5f, centreY - knobR + 2.0f,
                knobR * 2.0f, knobR * 2.0f);

  // ── 2. Outer ring (subtle recessed groove) ────────────────────────────
  g.setColour(SSL::knobShadow);
  g.fillEllipse(centreX - knobR - 2.0f, centreY - knobR - 2.0f,
                knobR * 2.0f + 4.0f, knobR * 2.0f + 4.0f);
  g.setColour(SSL::knobEdge);
  g.drawEllipse(centreX - knobR - 2.0f, centreY - knobR - 2.0f,
                knobR * 2.0f + 4.0f, knobR * 2.0f + 4.0f, 0.8f);

  // ── 3. Knob body — radial gradient (top-light) ───────────────────────
  {
    juce::ColourGradient bodyGrad(
        SSL::knobHigh,
        centreX - knobR * 0.3f, centreY - knobR * 0.5f,
        SSL::knobShadow,
        centreX + knobR * 0.4f, centreY + knobR * 0.6f,
        true);
    bodyGrad.addColour(0.35, SSL::knobBody);
    bodyGrad.addColour(0.7,  juce::Colour(0xFF202024));
    g.setGradientFill(bodyGrad);
    g.fillEllipse(centreX - knobR, centreY - knobR,
                  knobR * 2.0f, knobR * 2.0f);
  }

  // ── 4. Edge bevel ─────────────────────────────────────────────────────
  g.setColour(SSL::knobEdge);
  g.drawEllipse(centreX - knobR, centreY - knobR,
                knobR * 2.0f, knobR * 2.0f, 1.0f);

  // ── 5. Subtle notch marks around the knob (scale) ────────────────────
  {
    const float notchR = knobR + 5.0f;
    const float totalAngle = rotaryEndAngle - rotaryStartAngle;
    const int numNotches = 11;
    for (int i = 0; i <= numNotches; ++i) {
      float t = (float)i / (float)numNotches;
      float a = rotaryStartAngle + t * totalAngle;
      float cs = std::cos(a - juce::MathConstants<float>::halfPi);
      float sn = std::sin(a - juce::MathConstants<float>::halfPi);
      float r1 = notchR;
      float r2 = notchR + 3.0f;
      g.setColour(SSL::divider);
      g.drawLine(centreX + r1 * cs, centreY + r1 * sn,
                 centreX + r2 * cs, centreY + r2 * sn, 0.8f);
    }
  }

  // ── 6. Pointer line — thin, clean, SSL style ─────────────────────────
  {
    float cs = std::cos(angle - juce::MathConstants<float>::halfPi);
    float sn = std::sin(angle - juce::MathConstants<float>::halfPi);

    float lineStart = knobR * 0.25f;
    float lineEnd   = knobR * 0.92f;

    float x1 = centreX + lineStart * cs;
    float y1 = centreY + lineStart * sn;
    float x2 = centreX + lineEnd * cs;
    float y2 = centreY + lineEnd * sn;

    // Pointer line
    float thick = juce::jmax(1.8f, diameter * 0.028f);
    g.setColour(ptrCol);
    g.drawLine(x1, y1, x2, y2, thick);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// SSL-style illuminated push button
// ─────────────────────────────────────────────────────────────────────────────
void SSLLookAndFeel::drawToggleButton(juce::Graphics &g,
                                       juce::ToggleButton &button,
                                       bool highlighted, bool down) {
  auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
  bool isOn = button.getToggleState();

  // Determine ON colour from the button's tick colour property
  auto onColour = button.findColour(juce::ToggleButton::tickColourId);
  if (onColour.isTransparent())
    onColour = SSL::accentGreen;

  // Background
  if (isOn) {
    g.setColour(onColour);
    g.fillRoundedRectangle(bounds, 3.0f);
    // Subtle inner glow
    g.setColour(onColour.brighter(0.3f).withAlpha(0.3f));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 2.0f);
  } else {
    g.setColour(down ? SSL::bgSection : SSL::btnOff);
    g.fillRoundedRectangle(bounds, 3.0f);
  }

  // Border
  g.setColour(highlighted ? SSL::dividerLight : SSL::btnBorder);
  g.drawRoundedRectangle(bounds, 3.0f, 0.8f);

  // Text
  g.setColour(isOn ? SSL::bgDark : SSL::textPrimary);
  g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
}

// ─────────────────────────────────────────────────────────────────────────────
// SSL-style ComboBox — dark with subtle border
// ─────────────────────────────────────────────────────────────────────────────
void SSLLookAndFeel::drawComboBox(juce::Graphics &g, int width, int height,
                                   bool, int, int, int, int,
                                   juce::ComboBox &box) {
  auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
  g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
  g.fillRoundedRectangle(bounds, 3.0f);
  g.setColour(box.findColour(juce::ComboBox::outlineColourId));
  g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 0.8f);

  // Small arrow
  auto arrowZone = bounds.removeFromRight((float)height).reduced((float)height * 0.35f);
  juce::Path arrow;
  arrow.addTriangle(arrowZone.getX(), arrowZone.getY(),
                    arrowZone.getRight(), arrowZone.getY(),
                    arrowZone.getCentreX(), arrowZone.getBottom());
  g.setColour(box.findColour(juce::ComboBox::arrowColourId));
  g.fillPath(arrow);
}

void SSLLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width,
                                               int height) {
  g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
  g.setColour(SSL::divider);
  g.drawRect(0, 0, width, height, 1);
}

// =============================================================================
// PluginEditor
// =============================================================================

static constexpr int kDefaultW = 860;
static constexpr int kDefaultH = 520;
static constexpr int kMinW     = 700;
static constexpr int kMinH     = 420;

PluginEditor::PluginEditor(PluginProcessor &p)
    : AudioProcessorEditor(p), processorRef_(p), bhScope_(p) {
  setLookAndFeel(&sslLnf_);
  juce::LookAndFeel::setDefaultLookAndFeel(&sslLnf_);

  // ── Main knobs (pointer colours per-section) ──
  setupRotary(inputGain_,  ParamID::InputGain,  "INPUT",  SSL::accentCyan);
  setupRotary(outputGain_, ParamID::OutputGain, "OUTPUT", SSL::accentCyan);
  setupRotary(mix_,        ParamID::Mix,        "MIX",    SSL::accentPurple);
  setupRotary(svuAmount_,  ParamID::SVU,        "SVU",    SSL::accentPurple);

  addAndMakeVisible(bhScope_);
  addAndMakeVisible(levelMeter_);

  // ── Preset combo (8 factory presets — synced with Presets.h) ──
  presetLabel_.setText("PRESET", juce::dontSendNotification);
  presetLabel_.setJustificationType(juce::Justification::centred);
  presetLabel_.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  presetLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(presetLabel_);
  presetCombo_.addItemList({
      "Jensen JT-115K-E", "Jensen JT-11ELCF",
      "Neve 10468 Input", "Neve LI1166 Output",
      "Clean DI", "Vocal Warmth",
      "Bass Thickener", "Master Glue"}, 1);
  addAndMakeVisible(presetCombo_);
  presetAttach_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processorRef_.getAPVTS(), ParamID::Preset, presetCombo_);

  // ── Mode combo ──
  modeLabel_.setText("MODE", juce::dontSendNotification);
  modeLabel_.setJustificationType(juce::Justification::centred);
  modeLabel_.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  modeLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(modeLabel_);
  modeCombo_.addItemList({"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)", "Physical (J-A+OS2x)"}, 1);
  addAndMakeVisible(modeCombo_);
  modeAttach_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processorRef_.getAPVTS(), ParamID::Mode, modeCombo_);

  // ── Engine combo ──
  circuitLabel_.setText("ENGINE", juce::dontSendNotification);
  circuitLabel_.setJustificationType(juce::Justification::centred);
  circuitLabel_.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  circuitLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(circuitLabel_);

  circuitCombo_.addItemList({"Double Legacy", "Legacy (Transformer)", "Harrison Console"}, 1);
  addAndMakeVisible(circuitCombo_);
  circuitAttach_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processorRef_.getAPVTS(), ParamID::Circuit, circuitCombo_);

  // ── Preamp section ──
  setupRotary(preampGain_, ParamID::PreampGain, "GAIN", SSL::accentAmber);

  // PAD button (illuminated green)
  preampPad_.setButtonText("PAD");
  preampPad_.setColour(juce::ToggleButton::tickColourId, SSL::accentGreen);
  addAndMakeVisible(preampPad_);
  preampPadAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef_.getAPVTS(), ParamID::PreampPad, preampPad_);

  // Phase button (illuminated red)
  preampPhase_.setButtonText("PHASE");
  preampPhase_.setColour(juce::ToggleButton::tickColourId, SSL::accentRed);
  addAndMakeVisible(preampPhase_);
  preampPhaseAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef_.getAPVTS(), ParamID::PreampPhase, preampPhase_);

  // Path combo
  preampPathLabel_.setText("PATH", juce::dontSendNotification);
  preampPathLabel_.setJustificationType(juce::Justification::centred);
  preampPathLabel_.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  preampPathLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(preampPathLabel_);
  preampPathCombo_.addItemList({"Heritage Mode", "Modern"}, 1);
  addAndMakeVisible(preampPathCombo_);
  preampPathAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processorRef_.getAPVTS(), ParamID::PreampPath, preampPathCombo_);

  // Ratio combo
  preampRatioLabel_.setText("RATIO", juce::dontSendNotification);
  preampRatioLabel_.setJustificationType(juce::Justification::centred);
  preampRatioLabel_.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  preampRatioLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(preampRatioLabel_);
  preampRatioCombo_.addItemList({"1:5", "1:10"}, 1);
  addAndMakeVisible(preampRatioCombo_);
  preampRatioAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processorRef_.getAPVTS(), ParamID::PreampRatio, preampRatioCombo_);

  // ── T2 Load combo (Preamp mode) ──
  t2LoadLabel_.setText("T2 LOAD", juce::dontSendNotification);
  t2LoadLabel_.setJustificationType(juce::Justification::centred);
  t2LoadLabel_.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  t2LoadLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(t2LoadLabel_);
  t2LoadCombo_.addItemList({"600 Ohm", "10k Ohm", "47k Ohm"}, 1);
  addAndMakeVisible(t2LoadCombo_);
  t2LoadAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processorRef_.getAPVTS(), ParamID::T2Load, t2LoadCombo_);

  // ── Harrison Console controls ──
  setupRotary(harrisonMicGain_, ParamID::HarrisonMicGain, "MIC GAIN", SSL::accentAmber);
  setupRotary(harrisonSourceZ_, ParamID::HarrisonSourceZ, "SOURCE Z", SSL::accentPurple);
  setupRotary(harrisonDynLoss_, ParamID::HarrisonDynLoss, "DYNAMICS", SSL::accentGreen);

  harrisonPad_.setButtonText("PAD");
  harrisonPad_.setColour(juce::ToggleButton::tickColourId, SSL::accentGreen);
  addAndMakeVisible(harrisonPad_);
  harrisonPadAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef_.getAPVTS(), ParamID::HarrisonPad, harrisonPad_);

  harrisonPhase_.setButtonText("PHASE");
  harrisonPhase_.setColour(juce::ToggleButton::tickColourId, SSL::accentRed);
  addAndMakeVisible(harrisonPhase_);
  harrisonPhaseAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef_.getAPVTS(), ParamID::HarrisonPhase, harrisonPhase_);

  // ── P1.2: Saturation meter ──
  addAndMakeVisible(satMeter_);
  satLabel_.setText("SAT", juce::dontSendNotification);
  satLabel_.setJustificationType(juce::Justification::centred);
  satLabel_.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
  satLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(satLabel_);

  // ── P1.3: Info panel ──
  addAndMakeVisible(infoPanel_);

  // ── Monitor label ──
  monitorLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
  monitorLabel_.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(monitorLabel_);

  // ── Resizable window ──
  constrainer_.setMinimumSize(kMinW, kMinH);
  setConstrainer(&constrainer_);
  setResizable(true, true);
  setSize(kDefaultW, kDefaultH);

  // ── Initial engine visibility ──
  updateEngineVisibility(static_cast<int>(
      processorRef_.getAPVTS().getRawParameterValue(ParamID::Circuit)->load()));

  startTimerHz(30);
}

PluginEditor::~PluginEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void PluginEditor::setupRotary(RotarySlider &rs, const juce::String &paramId,
                                const juce::String &text, juce::Colour ptrCol) {
  rs.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  rs.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 16);
  rs.slider.setColour(juce::Slider::textBoxTextColourId,      SSL::textValue);
  rs.slider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0x00000000));
  rs.slider.setColour(juce::Slider::textBoxBackgroundColourId, SSL::bgRecessed);
  rs.slider.getProperties().set("arcColour", (juce::int64)ptrCol.getARGB());
  rs.slider.setVelocityBasedMode(false);

  // Double-click reset
  auto *param = processorRef_.getAPVTS().getParameter(paramId);
  if (param != nullptr) {
    float dn = param->getDefaultValue();
    rs.slider.setDoubleClickReturnValue(true,
        param->getNormalisableRange().convertFrom0to1(dn));
  }
  addAndMakeVisible(rs.slider);

  rs.label.setText(text, juce::dontSendNotification);
  rs.label.setJustificationType(juce::Justification::centred);
  rs.label.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
  rs.label.setColour(juce::Label::textColourId, SSL::textSecondary);
  addAndMakeVisible(rs.label);

  rs.attachment =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processorRef_.getAPVTS(), paramId, rs.slider);
}

// ─── Section painting helpers ────────────────────────────────────────────────

void PluginEditor::drawSectionHeader(juce::Graphics &g,
                                      juce::Rectangle<int> area,
                                      const juce::String &title,
                                      juce::Colour accent) {
  // Thin accent bar at top
  g.setColour(accent);
  g.fillRect(area.getX(), area.getY(), area.getWidth(), 2);

  // Header background
  g.setColour(SSL::bgSection);
  g.fillRect(area.getX(), area.getY() + 2, area.getWidth(), area.getHeight() - 2);

  // Title text
  g.setColour(SSL::textPrimary);
  g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
  g.drawText(title, area.reduced(8, 0).withTrimmedTop(2),
             juce::Justification::centredLeft);
}

void PluginEditor::drawHDivider(juce::Graphics &g, int x, int y, int w) {
  g.setColour(SSL::divider);
  g.drawLine((float)x, (float)y, (float)(x + w), (float)y, 1.0f);
}

void PluginEditor::drawVDivider(juce::Graphics &g, int x, int y, int h) {
  g.setColour(SSL::divider);
  g.drawLine((float)x, (float)y, (float)x, (float)(y + h), 1.0f);
}

// =============================================================================
// Paint
// =============================================================================

void PluginEditor::paint(juce::Graphics &g) {
  const int W = getWidth();
  const int H = getHeight();

  // ── Flat dark background ──
  g.fillAll(SSL::bgDark);

  // ── Header bar (top 36px) ──
  const int headerH = 36;
  auto headerArea = juce::Rectangle<int>(0, 0, W, headerH);
  g.setColour(SSL::bgPanel);
  g.fillRect(headerArea);

  // Brand name — left
  g.setColour(SSL::textBrand);
  g.setFont(juce::Font(juce::FontOptions(16.0f).withStyle("Bold")));
  g.drawText("TWISTERION", headerArea.reduced(14, 0),
             juce::Justification::centredLeft);

  // Subtitle — right
  g.setColour(SSL::textSecondary);
  g.setFont(juce::Font(juce::FontOptions(10.0f)));
  g.drawText("Powered by HysteriCore", headerArea.reduced(14, 0),
             juce::Justification::centredRight);

  // Header bottom line
  drawHDivider(g, 0, headerH, W);

  // ── Footer bar (bottom 24px) ──
  const int footerH = 24;
  const int footerY = H - footerH;
  drawHDivider(g, 0, footerY, W);
  g.setColour(SSL::bgPanel);
  g.fillRect(0, footerY + 1, W, footerH - 1);

  // ── Main content area ──
  const int contentY = headerH + 1;
  const int contentH = footerY - contentY;

  // 4-column layout: INPUT | PREAMP | OUTPUT | ANALYSIS
  // Proportions: 15% | 28% | 20% | 37%
  const float colRatios[] = {0.13f, 0.22f, 0.22f, 0.43f};
  int colX[5];
  colX[0] = 0;
  for (int i = 0; i < 4; ++i)
    colX[i + 1] = colX[i] + (int)((float)W * colRatios[i]);
  colX[4] = W; // ensure last column goes to edge

  const int sectionHeaderH = 20;

  // ── Column backgrounds & headers ──

  // INPUT column
  g.setColour(SSL::bgDark);
  g.fillRect(colX[0], contentY, colX[1] - colX[0], contentH);
  drawSectionHeader(g, {colX[0], contentY, colX[1] - colX[0], sectionHeaderH},
                    "INPUT", SSL::accentCyan);

  // PREAMP / TRANSFORMER / HARRISON column (dynamic title)
  g.setColour(SSL::bgDark);
  g.fillRect(colX[1], contentY, colX[2] - colX[1], contentH);
  {
    juce::Colour col2Accent = (lastCircuitIndex_ == 1) ? SSL::accentCyan
                            : (lastCircuitIndex_ == 2) ? SSL::accentGreen
                            : SSL::accentAmber;
    drawSectionHeader(g, {colX[1], contentY, colX[2] - colX[1], sectionHeaderH},
                      column2Title_, col2Accent);
  }

  // OUTPUT column
  g.setColour(SSL::bgDark);
  g.fillRect(colX[2], contentY, colX[3] - colX[2], contentH);
  drawSectionHeader(g, {colX[2], contentY, colX[3] - colX[2], sectionHeaderH},
                    "OUTPUT", SSL::accentCyan);

  // ANALYSIS column
  g.setColour(SSL::bgRecessed);
  g.fillRect(colX[3], contentY, colX[4] - colX[3], contentH);
  drawSectionHeader(g, {colX[3], contentY, colX[4] - colX[3], sectionHeaderH},
                    "ANALYSIS", SSL::accentPurple);

  // Vertical dividers between columns
  for (int i = 1; i < 4; ++i)
    drawVDivider(g, colX[i], contentY, contentH);

  // ── Sub-section dividers within columns ──
  {
    const int pad = 6;
    const int innerY = contentY + sectionHeaderH + pad;
    const int innerH = contentH - sectionHeaderH - pad * 2;

    g.setColour(SSL::divider);

    // Column 1 (INPUT): divider between Input Gain and SVU
    {
      int divGap = 22;
      int knobH = (innerH - divGap) / 2;
      int lineY = innerY + knobH + divGap / 2;
      g.drawLine((float)(colX[0] + 12), (float)lineY,
                 (float)(colX[1] - 12), (float)lineY, 1.0f);
    }

    // Column 3 (OUTPUT): between Output/Mix and Mix/Engine
    {
      int divGap = 18;
      int comboH = 36;
      int knobH = (innerH - divGap * 2 - comboH) / 2;

      int line1Y = innerY + knobH + divGap / 2;
      int line2Y = innerY + knobH + divGap + knobH + divGap / 2;

      g.drawLine((float)(colX[2] + 12), (float)line1Y,
                 (float)(colX[3] - 12), (float)line1Y, 1.0f);
      g.drawLine((float)(colX[2] + 12), (float)line2Y,
                 (float)(colX[3] - 12), (float)line2Y, 1.0f);
    }
  }
}

// =============================================================================
// Resized
// =============================================================================

void PluginEditor::resized() {
  const int W = getWidth();
  const int H = getHeight();

  const int headerH = 36;
  const int footerH = 24;
  const int contentY = headerH + 1;
  const int contentH = H - footerH - contentY;
  const int sectionHeaderH = 20;

  // Column boundaries
  const float colRatios[] = {0.13f, 0.22f, 0.22f, 0.43f};
  int colX[5];
  colX[0] = 0;
  for (int i = 0; i < 4; ++i)
    colX[i + 1] = colX[i] + (int)((float)W * colRatios[i]);
  colX[4] = W;

  const int pad = 6;
  const int innerY = contentY + sectionHeaderH + pad;
  const int innerH = contentH - sectionHeaderH - pad * 2;

  // ═══════════════════════════════════════════════════════════════════════
  // COLUMN 1: INPUT  (Input Gain + SVU)
  // ═══════════════════════════════════════════════════════════════════════
  {
    auto col = juce::Rectangle<int>(colX[0] + pad, innerY,
                                     colX[1] - colX[0] - pad * 2, innerH);
    int divGap = 22;
    int knobH = (col.getHeight() - divGap) / 2;

    // Input Gain knob (top)
    auto inputArea = col.removeFromTop(knobH);
    inputGain_.label.setBounds(inputArea.removeFromTop(14));
    inputGain_.slider.setBounds(inputArea);

    col.removeFromTop(divGap);

    // SVU knob (bottom)
    auto svuArea = col.removeFromTop(knobH);
    svuAmount_.label.setBounds(svuArea.removeFromTop(14));
    svuAmount_.slider.setBounds(svuArea);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // COLUMN 2: PREAMP / TRANSFORMER / HARRISON (engine-dependent)
  // ═══════════════════════════════════════════════════════════════════════
  {
    auto col = juce::Rectangle<int>(colX[1] + pad, innerY,
                                     colX[2] - colX[1] - pad * 2, innerH);
    const int comboRowH = 36;

    if (lastCircuitIndex_ == 0)
    {
      // ── O.D.T Preamp: Gain knob + Path + Ratio + T2Load + PAD/Phase ──
      col.removeFromTop(8);
      {
        auto row = col.removeFromTop(comboRowH);
        modeLabel_.setBounds(row.removeFromTop(13));
        modeCombo_.setBounds(row.reduced(2, 1));
      }
      col.removeFromTop(8);
      {
        auto row = col.removeFromTop(comboRowH);
        t2LoadLabel_.setBounds(row.removeFromTop(13));
        t2LoadCombo_.setBounds(row.reduced(2, 1));
      }
    }
    else if (lastCircuitIndex_ == 1)
    {
      // ── Legacy Transformer: Preset + Mode combos ──
      col.removeFromTop(8);
      {
        auto row = col.removeFromTop(comboRowH);
        presetLabel_.setBounds(row.removeFromTop(13));
        presetCombo_.setBounds(row.reduced(2, 1));
      }
      col.removeFromTop(8);
      {
        auto row = col.removeFromTop(comboRowH);
        modeLabel_.setBounds(row.removeFromTop(13));
        modeCombo_.setBounds(row.reduced(2, 1));
      }
    }
    else
    {
      // ── Harrison Console: MicGain + SourceZ + Dynamics + PAD/Phase ──
      int gainH = (int)(col.getHeight() * 0.28f);
      auto gainArea = col.removeFromTop(gainH);
      harrisonMicGain_.label.setBounds(gainArea.removeFromTop(14));
      harrisonMicGain_.slider.setBounds(gainArea);

      col.removeFromTop(4);

      int szH = (int)(col.getHeight() * 0.32f);
      auto szArea = col.removeFromTop(szH);
      harrisonSourceZ_.label.setBounds(szArea.removeFromTop(14));
      harrisonSourceZ_.slider.setBounds(szArea);

      col.removeFromTop(4);

      int dynH = (int)(col.getHeight() * 0.40f);
      auto dynArea = col.removeFromTop(dynH);
      harrisonDynLoss_.label.setBounds(dynArea.removeFromTop(14));
      harrisonDynLoss_.slider.setBounds(dynArea);

      col.removeFromTop(8);
      {
        int btnH = juce::jmin(28, col.getHeight());
        auto btnRow = col.removeFromTop(btnH);
        int halfW = btnRow.getWidth() / 2 - 2;
        harrisonPad_.setBounds(btnRow.removeFromLeft(halfW));
        btnRow.removeFromLeft(4);
        harrisonPhase_.setBounds(btnRow.removeFromLeft(halfW));
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════════
  // COLUMN 3: OUTPUT  (Output Gain + Mix + Engine)
  // ═══════════════════════════════════════════════════════════════════════
  {
    auto col = juce::Rectangle<int>(colX[2] + pad, innerY,
                                     colX[3] - colX[2] - pad * 2, innerH);

    int divGap = 18;
    int comboH = 36;
    int knobH = (col.getHeight() - divGap * 2 - comboH) / 2;

    // Output Gain knob
    auto outArea = col.removeFromTop(knobH);
    outputGain_.label.setBounds(outArea.removeFromTop(14));
    outputGain_.slider.setBounds(outArea);

    col.removeFromTop(divGap);

    // Mix knob
    auto mixArea = col.removeFromTop(knobH);
    mix_.label.setBounds(mixArea.removeFromTop(14));
    mix_.slider.setBounds(mixArea);

    col.removeFromTop(divGap);

    // Engine selector (bottom)
    {
      auto engArea = col.removeFromTop(comboH);
      circuitLabel_.setBounds(engArea.removeFromTop(13));
      circuitCombo_.setBounds(engArea.reduced(2, 1));
    }
  }

  // ═══════════════════════════════════════════════════════════════════════
  // COLUMN 4: ANALYSIS  (B-H Scope + Level Meter)
  // ═══════════════════════════════════════════════════════════════════════
  {
    auto col = juce::Rectangle<int>(colX[3] + pad, innerY,
                                     colX[4] - colX[3] - pad * 2, innerH);

    // P1.2: Saturation meter (right strip, 24px wide)
    auto satCol = col.removeFromRight(24);
    satLabel_.setBounds(satCol.removeFromTop(12));
    satMeter_.setBounds(satCol.reduced(2, 0));

    col.removeFromRight(4);

    // B-H Scope (main area)
    auto scopeArea = col.removeFromTop(col.getHeight() - 50);
    bhScope_.setBounds(scopeArea.reduced(2));

    col.removeFromTop(4);

    // P1.3: Info panel (between scope and level meter)
    auto infoArea = col.removeFromTop(16);
    infoPanel_.setBounds(infoArea.reduced(2, 0));

    col.removeFromTop(2);

    // Level meter bar (bottom strip)
    levelMeter_.setBounds(col.reduced(2, 0));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // FOOTER
  // ═══════════════════════════════════════════════════════════════════════
  {
    auto footer = juce::Rectangle<int>(0, H - footerH, W, footerH).reduced(10, 2);
    monitorLabel_.setBounds(footer);
  }
}

// =============================================================================
// Engine-dependent UI visibility
// =============================================================================

void PluginEditor::updateEngineVisibility(int circuitIndex) {
  if (circuitIndex == lastCircuitIndex_)
    return;
  lastCircuitIndex_ = circuitIndex;

  // 0 = Double Legacy, 1 = Legacy Transformer, 2 = Harrison Console
  const bool isDoubleLegacy = (circuitIndex == 0);
  const bool isLegacy   = (circuitIndex == 1);
  const bool isHarrison = (circuitIndex == 2);

  // ── Preamp controls ──
  preampGain_.slider.setVisible(false);
  preampGain_.label.setVisible(false);
  preampPathLabel_.setVisible(false);
  preampPathCombo_.setVisible(false);
  preampRatioLabel_.setVisible(false);
  preampRatioCombo_.setVisible(false);
  preampPad_.setVisible(false);
  preampPhase_.setVisible(false);
  t2LoadLabel_.setVisible(isDoubleLegacy);
  t2LoadCombo_.setVisible(isDoubleLegacy);

  // ── Legacy controls ──
  presetLabel_.setVisible(isLegacy);
  presetCombo_.setVisible(isLegacy);
  modeLabel_.setVisible(isDoubleLegacy || isLegacy);
  modeCombo_.setVisible(isDoubleLegacy || isLegacy);

  // ── Harrison controls ──
  harrisonMicGain_.slider.setVisible(isHarrison);
  harrisonMicGain_.label.setVisible(isHarrison);
  harrisonSourceZ_.slider.setVisible(isHarrison);
  harrisonSourceZ_.label.setVisible(isHarrison);
  harrisonDynLoss_.slider.setVisible(isHarrison);
  harrisonDynLoss_.label.setVisible(isHarrison);
  harrisonPad_.setVisible(isHarrison);
  harrisonPhase_.setVisible(isHarrison);

  // Update column 2 header title
  column2Title_ = isDoubleLegacy ? "DOUBLE LEGACY"
                : isLegacy ? "TRANSFORMER"
                : "HARRISON";

  // Trigger relayout + repaint
  resized();
  repaint();
}

void PluginEditor::timerCallback() {
  // Check engine mode change
  updateEngineVisibility(static_cast<int>(
      processorRef_.getAPVTS().getRawParameterValue(ParamID::Circuit)->load()));

  // P1.2: Update saturation meter
  float sat = processorRef_.getPeakSaturation();
  satMeter_.setSaturation(sat);

  // P1.3: Update info panel
  auto monData = processorRef_.getMonitorData();
  infoPanel_.setNRIter(monData.lastIterCount);
  infoPanel_.setSatPct(sat);
  infoPanel_.setLmValue(processorRef_.getLmSmoothed());

  const auto levels = processorRef_.getEngineLevels();
  const int circuitIndex = static_cast<int>(
      processorRef_.getAPVTS().getRawParameterValue(ParamID::Circuit)->load());

  levelMeter_.setLevels(levels.inputLevel_dBu, levels.outputLevel_dBu);

  juce::String text;
  if (circuitIndex == 2)
  {
    text = juce::String(levels.inputLevel_dBu, 1) + " dBu  >  "
         + juce::String(levels.outputLevel_dBu, 1) + " dBu"
         + "   |   Harrison"
         + "   |   Mic " + juce::String(harrisonMicGain_.slider.getValue(), 2);
  }
  else if (circuitIndex == 0)
  {
    text = juce::String(levels.inputLevel_dBu, 1) + " dBu  >  "
         + juce::String(levels.outputLevel_dBu, 1) + " dBu"
         + "   |   JT-115K-E > JT-11ELCF"
         + "   |   " + modeCombo_.getText()
         + "   |   " + t2LoadCombo_.getText();
  }
  else
  {
    text = juce::String(levels.inputLevel_dBu, 1) + " dBu  >  "
         + juce::String(levels.outputLevel_dBu, 1) + " dBu"
         + "   |   " + presetCombo_.getText()
         + "   |   " + modeCombo_.getText();
  }
  if (levels.isClipping)
    text += "   |   CLIP";

  monitorLabel_.setText(text, juce::dontSendNotification);
}
