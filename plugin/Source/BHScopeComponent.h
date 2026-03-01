#pragma once

#include "PluginProcessor.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>


class BHScopeComponent : public juce::Component, public juce::Timer {
public:
  explicit BHScopeComponent(PluginProcessor &processor);
  ~BHScopeComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void timerCallback() override;

private:
  PluginProcessor &processorRef_;

  // Circular buffer to store recent points for drawing
  static constexpr int kMaxPoints = 512;
  std::array<transfo::BHSample, kMaxPoints> points_{};
  int writeIdx_ = 0;

  // Smooth min/max for dynamic scaling
  float maxH_ = 1.0f;
  float maxB_ = 1.0f;

  void drawGrid(juce::Graphics &g, juce::Rectangle<int> bounds);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BHScopeComponent)
};
