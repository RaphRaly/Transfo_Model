#pragma once

// =============================================================================
// PluginEditor — v3 GUI for the Transformer Model plugin.
//
// Features:
//   - Transformer preset selector (Jensen/Neve/API)
//   - Processing mode switch (Realtime/Physical)
//   - Input/Output gain + Mix knobs
//   - TMT stereo spread control
//   - J-A parameters (advanced section)
//   - B-H scope visualization (future: BHScopeComponent)
//   - Convergence monitoring display
// =============================================================================

#include "BHScopeComponent.h"
#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PluginEditor : public juce::AudioProcessorEditor, public juce::Timer {
public:
  explicit PluginEditor(PluginProcessor &processor);
  ~PluginEditor() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void timerCallback() override;

private:
  PluginProcessor &processorRef_;

  // ─── UI Components ──────────────────────────────────────────────────────
  struct RotarySlider {
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attachment;
  };

  // Main controls
  RotarySlider inputGain_, outputGain_, mix_, tmtAmount_;

  // Preset and mode
  juce::ComboBox presetCombo_, modeCombo_;
  juce::Label presetLabel_, modeLabel_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      presetAttach_, modeAttach_;

  // J-A parameters (advanced)
  RotarySlider msSlider_, aSlider_, kSlider_, cSlider_, alphaSlider_;

  // Monitor display
  juce::Label monitorLabel_;
  BHScopeComponent bhScope_;

  void setupRotary(RotarySlider &rs, const juce::String &paramId,
                   const juce::String &text);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
