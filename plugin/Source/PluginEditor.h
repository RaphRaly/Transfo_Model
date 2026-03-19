#pragma once

// =============================================================================
// PluginEditor -- v3 polished, resizable GUI for the Transformer Model plugin.
// =============================================================================

#include "BHScopeComponent.h"
#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// =============================================================================
// Custom LookAndFeel for modern rotary knobs with colored arcs
// =============================================================================

class ModernLookAndFeel : public juce::LookAndFeel_V4 {
public:
  ModernLookAndFeel();

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &slider) override;

  void drawComboBox(juce::Graphics &g, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox &box) override;

  void drawPopupMenuBackground(juce::Graphics &g, int width,
                                int height) override;
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
  ModernLookAndFeel modernLnf_;

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

  void setupRotary(RotarySlider &rs, const juce::String &paramId,
                   const juce::String &text, juce::Colour arcCol);

  // Section painting helper
  void drawSection(juce::Graphics &g, juce::Rectangle<int> bounds,
                   const juce::String &title);

  // ── Preamp controls (Sprint 7) ──────────────────────────────────────────
  RotarySlider preampGain_;

  juce::ToggleButton preampPad_, preampPhase_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      preampPadAttach_, preampPhaseAttach_;

  juce::ComboBox preampPathCombo_, preampRatioCombo_;
  juce::Label preampPathLabel_, preampRatioLabel_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      preampPathAttach_, preampRatioAttach_;

  // Resizable constrainer
  juce::ComponentBoundsConstrainer constrainer_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
