#pragma once

// =============================================================================
// LevelMeterComponent -- Compact stereo horizontal LED-bar meter for footer.
// Segmented bargraph: Green / Yellow / Red.  28 px tall, pure JUCE Graphics.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

class LevelMeterComponent : public juce::Component {
public:
    LevelMeterComponent() = default;

    /** Feed new L / R levels in dBu (call every timer tick). */
    void setLevels(float leftdBu, float rightdBu)
    {
        // Smooth with simple exponential filter (~100 ms at 30 fps)
        constexpr float coeff = 0.25f;
        leftLevel_  += coeff * (juce::jlimit(-60.0f, 6.0f, leftdBu)  - leftLevel_);
        rightLevel_ += coeff * (juce::jlimit(-60.0f, 6.0f, rightdBu) - rightLevel_);

        // Peak hold per channel
        updatePeak(leftdBu,  leftPeak_,  leftPeakHold_);
        updatePeak(rightdBu, rightPeak_, rightPeakHold_);

        if (std::abs(leftLevel_ - lastPaintL_) > 0.3f
            || std::abs(rightLevel_ - lastPaintR_) > 0.3f
            || leftPeakHold_ > 0 || rightPeakHold_ > 0)
        {
            lastPaintL_ = leftLevel_;
            lastPaintR_ = rightLevel_;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Background
        g.setColour(bgColour_);
        g.fillRoundedRectangle(bounds, 3.0f);

        // Channel label width
        const float labelW = 14.0f;
        auto labelArea = bounds.removeFromLeft(labelW);
        bounds.removeFromLeft(2.0f);

        // Split into two rows (L on top, R on bottom) with 1 px gap
        const float rowGap = 1.0f;
        const float rowH = (bounds.getHeight() - rowGap) * 0.5f;

        auto leftRow  = bounds.removeFromTop(rowH);
        bounds.removeFromTop(rowGap);
        auto rightRow = bounds.removeFromTop(rowH);

        // "L" / "R" labels
        g.setColour(labelColour_);
        g.setFont(juce::Font(juce::FontOptions(juce::jmax(9.0f, rowH * 0.85f)).withStyle("Bold")));
        auto lLabel = labelArea.removeFromTop(labelArea.getHeight() * 0.5f);
        auto rLabel = labelArea;
        g.drawText("L", lLabel, juce::Justification::centred, false);
        g.drawText("R", rLabel, juce::Justification::centred, false);

        // Draw bars
        drawBar(g, leftRow,  leftLevel_,  leftPeak_);
        drawBar(g, rightRow, rightLevel_, rightPeak_);
    }

private:
    // ── Helpers ─────────────────────────────────────────────────────────────

    void drawBar(juce::Graphics& g, juce::Rectangle<float> area, float level_dBu, float peak_dBu)
    {
        const int numSegs = 30;
        const float segGap = 1.0f;
        const float totalGaps = segGap * (float)(numSegs - 1);
        const float segW = (area.getWidth() - totalGaps) / (float)numSegs;

        // dBu range: -60 .. +6
        constexpr float rangeMin = -60.0f;
        constexpr float rangeMax = 6.0f;
        const float normLevel = (level_dBu - rangeMin) / (rangeMax - rangeMin);
        const int litSegs = juce::jlimit(0, numSegs,
                                          (int)std::round(normLevel * (float)numSegs));

        const float normPeak = (peak_dBu - rangeMin) / (rangeMax - rangeMin);
        const int peakSeg = juce::jlimit(0, numSegs - 1,
                                          (int)std::round(normPeak * (float)numSegs));

        for (int i = 0; i < numSegs; ++i)
        {
            float segX = area.getX() + (float)i * (segW + segGap);
            auto segRect = juce::Rectangle<float>(segX, area.getY(), segW, area.getHeight());

            // Determine colour based on segment position
            float segdBu = rangeMin + ((float)i / (float)numSegs) * (rangeMax - rangeMin);
            juce::Colour segCol;
            if (segdBu >= -3.0f)
                segCol = meterRed_;
            else if (segdBu >= -12.0f)
                segCol = meterYellow_;
            else
                segCol = meterGreen_;

            if (i < litSegs)
            {
                g.setColour(segCol);
                g.fillRect(segRect);
            }
            else if (i == peakSeg && peak_dBu > rangeMin + 1.0f)
            {
                // Peak hold indicator
                g.setColour(segCol.withAlpha(0.8f));
                g.fillRect(segRect);
            }
            else
            {
                // Unlit segment (dim)
                g.setColour(segCol.withAlpha(0.10f));
                g.fillRect(segRect);
            }
        }
    }

    void updatePeak(float dBu, float& peak, int& holdCounter)
    {
        if (dBu > peak)
        {
            peak = dBu;
            holdCounter = peakHoldFrames_;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }
        else
        {
            // Decay the peak ~30 dB/s at 30 fps
            peak -= 1.0f;
            if (peak < -60.0f)
                peak = -60.0f;
        }
    }

    // ── State ───────────────────────────────────────────────────────────────
    float leftLevel_  = -60.0f;
    float rightLevel_ = -60.0f;

    float leftPeak_   = -60.0f;
    float rightPeak_  = -60.0f;
    int   leftPeakHold_  = 0;
    int   rightPeakHold_ = 0;

    float lastPaintL_ = -100.0f;
    float lastPaintR_ = -100.0f;

    static constexpr int peakHoldFrames_ = 45; // ~1.5 s at 30 fps

    // ── Colours ─────────────────────────────────────────────────────────────
    juce::Colour bgColour_    { 0xFF0E0E1E };
    juce::Colour labelColour_ { 0xFFBBBBDD };
    juce::Colour meterGreen_  { 0xFF66BB6A };
    juce::Colour meterYellow_ { 0xFFFFB74D };
    juce::Colour meterRed_    { 0xFFEF5350 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeterComponent)
};
