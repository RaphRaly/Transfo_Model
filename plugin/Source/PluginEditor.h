#pragma once

// =============================================================================
// PluginEditor -- SSL Channel Strip 2 inspired GUI for Transformer Model.
// =============================================================================

#include "BHScopeComponent.h"
#include "LevelMeterComponent.h"
#include "PluginProcessor.h"
#include "VUMeterComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// =============================================================================
// SSLLookAndFeel — Dark console-style LookAndFeel
// =============================================================================

class SSLLookAndFeel : public juce::LookAndFeel_V4 {
public:
  SSLLookAndFeel();

  juce::Typeface::Ptr getTypefaceForFont(const juce::Font &font) override;

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &slider) override;

  void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

  void drawComboBox(juce::Graphics &g, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox &box) override;

  void drawPopupMenuBackground(juce::Graphics &g, int width,
                                int height) override;

private:
  juce::Typeface::Ptr typefaceRegular_;
  juce::Typeface::Ptr typefaceBold_;
};

// =============================================================================
// PluginEditor
// =============================================================================

class PluginEditor : public juce::AudioProcessorEditor, public juce::Timer {
public:
  explicit PluginEditor(PluginProcessor &processor);
  ~PluginEditor() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void timerCallback() override;

private:
  PluginProcessor &processorRef_;
  SSLLookAndFeel sslLnf_;

  // ─── UI Components ──────────────────────────────────────────────────────
  struct RotarySlider {
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attachment;
  };

  // Main controls
  RotarySlider inputGain_, outputGain_, mix_, svuAmount_;

  // Preset, mode, and circuit topology
  juce::ComboBox presetCombo_, modeCombo_, circuitCombo_;
  juce::Label presetLabel_, modeLabel_, circuitLabel_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      presetAttach_, modeAttach_, circuitAttach_;

  // Monitor display
  juce::Label monitorLabel_;
  BHScopeComponent bhScope_;

  // Level meter (footer bar)
  LevelMeterComponent levelMeter_;

  void setupRotary(RotarySlider &rs, const juce::String &paramId,
                   const juce::String &text, juce::Colour pointerCol);

  // Section painting helpers
  void drawSectionHeader(juce::Graphics &g, juce::Rectangle<int> area,
                         const juce::String &title, juce::Colour accent);
  void drawHDivider(juce::Graphics &g, int x, int y, int w);
  void drawVDivider(juce::Graphics &g, int x, int y, int h);

  // ── Preamp controls ────────────────────────────────────────────────────
  RotarySlider preampGain_;

  juce::ToggleButton preampPad_, preampPhase_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      preampPadAttach_, preampPhaseAttach_;

  juce::ComboBox preampPathCombo_, preampRatioCombo_;
  juce::Label preampPathLabel_, preampRatioLabel_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      preampPathAttach_, preampRatioAttach_;

  // ── Harrison Console controls ─────────────────────────────────────────
  RotarySlider harrisonMicGain_, harrisonSourceZ_, harrisonDynLoss_;

  juce::ToggleButton harrisonPad_, harrisonPhase_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      harrisonPadAttach_, harrisonPhaseAttach_;

  // ── T2 Load combo (visible in Preamp mode) ──────────────────────────
  juce::ComboBox t2LoadCombo_;
  juce::Label t2LoadLabel_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      t2LoadAttach_;

  // ── Engine-dependent UI visibility ──────────────────────────────────
  int lastCircuitIndex_ = -1;  // tracks engine changes for show/hide
  void updateEngineVisibility(int circuitIndex);
  juce::String column2Title_ = "DOUBLE LEGACY";  // dynamic header for column 2

  // ── P1.2: Saturation Meter ──────────────────────────────────────────────
  class SaturationMeter : public juce::Component {
  public:
    void setSaturation(float pct) { saturation_ = pct; repaint(); }
    float getSaturation() const { return saturation_; }
    void paint(juce::Graphics& g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(juce::Colour(0xFF141418));
      g.fillRoundedRectangle(b, 3.0f);
      float fillH = b.getHeight() * std::min(saturation_ / 100.0f, 1.0f);
      auto fillRect = b.removeFromBottom(fillH);
      juce::Colour col = saturation_ < 30.f ? juce::Colour(0xFF4CAF50)
                        : saturation_ < 70.f ? juce::Colour(0xFFFFB300)
                        : juce::Colour(0xFFE53935);
      g.setColour(col);
      g.fillRoundedRectangle(fillRect, 2.0f);
      g.setColour(juce::Colour(0xFF3A3A3E));
      g.drawRoundedRectangle(getLocalBounds().toFloat(), 3.0f, 0.8f);
      g.setColour(juce::Colour(0xFFCCCCCC));
      g.setFont(juce::Font(juce::FontOptions(9.0f)));
      g.drawText(juce::String((int)saturation_) + "%",
                 getLocalBounds(), juce::Justification::centred);
    }
  private:
    float saturation_ = 0.0f;
  };

  // ── P1.3: Engineering Info Panel ───────────────────────────────────────
  class InfoPanel : public juce::Component {
  public:
    void setNRIter(int n) { nrIter_ = n; repaint(); }
    void setSatPct(float p) { satPct_ = p; }
    void setLmValue(float lm) { lmValue_ = lm; }
    void paint(juce::Graphics& g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(juce::Colour(0xFF1A1A1E));
      g.fillRoundedRectangle(b, 4.0f);
      g.setColour(juce::Colour(0xFF3A3A3E));
      g.drawRoundedRectangle(b, 4.0f, 0.8f);
      g.setFont(juce::Font(juce::FontOptions(10.0f)));
      g.setColour(juce::Colour(0xFF888888));
      auto tb = getLocalBounds().reduced(6, 2);
      // P1.3: Show saturation, NR iterations, and dynamic Lm
      juce::String lmStr = (lmValue_ >= 1.0f) ? juce::String(lmValue_, 1) + "H"
                         : (lmValue_ >= 0.001f) ? juce::String(lmValue_ * 1000.0f, 1) + "mH"
                         : juce::String(lmValue_ * 1e6f, 0) + "uH";
      g.drawText("Sat: " + juce::String(satPct_, 1) + "%  NR: " + juce::String(nrIter_)
                 + "  Lm: " + lmStr,
                 tb, juce::Justification::centredLeft);
    }
  private:
    int nrIter_ = 0;
    float satPct_ = 0.0f;
    float lmValue_ = 1.0f;
  };

  SaturationMeter satMeter_;
  InfoPanel infoPanel_;
  juce::Label satLabel_;

  // Resizable constrainer
  juce::ComponentBoundsConstrainer constrainer_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
