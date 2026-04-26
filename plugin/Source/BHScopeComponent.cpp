#include "BHScopeComponent.h"

BHScopeComponent::BHScopeComponent(PluginProcessor &p) : processorRef_(p) {
  // 30 Hz refresh rate for smooth animation
  startTimerHz(30);
}

BHScopeComponent::~BHScopeComponent() { stopTimer(); }

void BHScopeComponent::timerCallback() {
  transfo::BHSample samples[128];
  size_t numRead = processorRef_.readBHSamples(samples, 128);

  if (numRead > 0) {
    for (size_t i = 0; i < numRead; ++i) {
      points_[writeIdx_] = samples[i];
      writeIdx_ = (writeIdx_ + 1) % kMaxPoints;
    }
    repaint();
  }
}

void BHScopeComponent::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds().reduced(4);

  // Background
  g.setColour(juce::Colour(0xFF0A0A14));
  g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

  drawGrid(g, bounds);

  // Find current max extents for dynamic scaling
  float currentMaxH = 0.1f;
  float currentMaxB = 0.1f;
  for (int i = 0; i < kMaxPoints; ++i) {
    currentMaxH = juce::jmax(currentMaxH, std::abs(points_[i].h));
    currentMaxB = juce::jmax(currentMaxB, std::abs(points_[i].b));
  }

  // Smooth the scaling to prevent jitter
  maxH_ = maxH_ * 0.9f + currentMaxH * 0.1f;
  maxB_ = maxB_ * 0.9f + currentMaxB * 0.1f;

  // Draw the B-H curve
  if constexpr (kMaxPoints < 2)
    return;

  juce::Path curve;
  bool first = true;

  // Iterate from oldest to newest point
  int numPointsToDraw = kMaxPoints;
  for (int i = 0; i < numPointsToDraw; ++i) {
    int readIdx = (writeIdx_ + i) % kMaxPoints;
    const auto &pt = points_[readIdx];

    // Map to UI coordinates
    float x = juce::jmap(pt.h, -maxH_, maxH_, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
    float y =
        juce::jmap(pt.b, -maxB_, maxB_, static_cast<float>(bounds.getBottom()),
                   static_cast<float>(bounds.getY()));

    // Clamp to bounds to prevent drawing outside
    x = juce::jlimit(static_cast<float>(bounds.getX()),
                     static_cast<float>(bounds.getRight()), x);
    y = juce::jlimit(static_cast<float>(bounds.getY()),
                     static_cast<float>(bounds.getBottom()), y);

    if (first) {
      curve.startNewSubPath(x, y);
      first = false;
    } else {
      curve.lineTo(x, y);
    }
  }

  // Glow effect
  g.setColour(juce::Colours::cyan.withAlpha(0.3f));
  g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved));

  // Main trace
  g.setColour(juce::Colours::cyan);
  g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

void BHScopeComponent::drawGrid(juce::Graphics &g,
                                juce::Rectangle<int> bounds) {
  g.setColour(juce::Colour(0xFF1E1E2E));

  // Center axes
  int cx = bounds.getCentreX();
  int cy = bounds.getCentreY();
  g.drawVerticalLine(cx, static_cast<float>(bounds.getY()),
                     static_cast<float>(bounds.getBottom()));
  g.drawHorizontalLine(cy, static_cast<float>(bounds.getX()),
                       static_cast<float>(bounds.getRight()));

  // Minor grid
  g.setColour(juce::Colour(0xFF151522));
  for (int i = 1; i <= 4; ++i) {
    float frac = i / 5.0f;
    int dx = static_cast<int>(bounds.getWidth() * 0.5f * frac);
    int dy = static_cast<int>(bounds.getHeight() * 0.5f * frac);

    g.drawVerticalLine(cx - dx, static_cast<float>(bounds.getY()),
                       static_cast<float>(bounds.getBottom()));
    g.drawVerticalLine(cx + dx, static_cast<float>(bounds.getY()),
                       static_cast<float>(bounds.getBottom()));
    g.drawHorizontalLine(cy - dy, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
    g.drawHorizontalLine(cy + dy, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
  }

  // Labels
  g.setColour(juce::Colours::grey);
  g.setFont(10.0f);
  g.drawText("H", bounds.getRight() - 15, cy + 2, 15, 10,
             juce::Justification::centred);
  g.drawText("B", cx + 5, bounds.getY() + 2, 15, 10,
             juce::Justification::centred);
}

void BHScopeComponent::resized() {}
