#pragma once

// =============================================================================
// VUMeterComponent -- Analog-style VU needle meter (UAD / Klanghelm quality).
// Pure JUCE Graphics, no image assets. Designed for 30 Hz repaint.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

class VUMeterComponent : public juce::Component {
public:
    explicit VUMeterComponent(const juce::String& label = "")
        : label_(label)
    {
        smoothedLevel_.reset(32);
        smoothedLevel_.setCurrentAndTargetValue(-40.0f);
    }

    // ── Public API ──────────────────────────────────────────────────────────

    /** Feed a new dBu reading (call every timer tick). */
    void setLevel(float dBu)
    {
        // Clamp to useful range
        dBu = juce::jlimit(-40.0f, 6.0f, dBu);

        // Manual VU ballistics (~300 ms integration at 30 Hz = 9 frames)
        constexpr float attackCoeff  = 0.12f;
        constexpr float releaseCoeff = 0.12f;

        if (dBu > currentSmoothed_)
            currentSmoothed_ += attackCoeff * (dBu - currentSmoothed_);
        else
            currentSmoothed_ += releaseCoeff * (dBu - currentSmoothed_);

        // Peak hold logic
        if (dBu > 0.0f)
        {
            peakHeld_ = true;
            peakHoldCounter_ = peakHoldFrames_;
            peakBrightness_ = 1.0f;
        }
        else if (peakHoldCounter_ > 0)
        {
            --peakHoldCounter_;
        }
        else if (peakBrightness_ > 0.0f)
        {
            peakBrightness_ -= peakFadeRate_;
            if (peakBrightness_ < 0.0f)
            {
                peakBrightness_ = 0.0f;
                peakHeld_ = false;
            }
        }

        // Only repaint when the needle actually moves (> 0.15 dB change)
        if (std::abs(currentSmoothed_ - lastPaintedLevel_) > 0.15f
            || std::abs(peakBrightness_ - lastPaintedPeak_) > 0.01f)
        {
            lastPaintedLevel_ = currentSmoothed_;
            lastPaintedPeak_  = peakBrightness_;
            repaint();
        }
    }

    void setPeakHold(bool enabled)  { peakHoldEnabled_ = enabled; }

    void setNeedleColour(juce::Colour c) { needleColour_ = c; }

    // ── Painting ────────────────────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // --- Background panel ---
        g.setColour(panelBg_);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(panelBorder_);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        // Reserve bottom area for label
        const float labelH = juce::jmax(14.0f, bounds.getHeight() * 0.10f);
        auto labelArea = bounds.removeFromBottom(labelH);

        // Reserve top strip for peak LED
        const float ledStripH = juce::jmax(10.0f, bounds.getHeight() * 0.08f);
        auto ledStrip = bounds.removeFromTop(ledStripH);

        // --- Peak LED ---
        {
            const float ledDiam = juce::jmin(ledStripH * 0.70f, 8.0f);
            float ledX = ledStrip.getCentreX() - ledDiam * 0.5f;
            float ledY = ledStrip.getCentreY() - ledDiam * 0.5f;
            auto ledRect = juce::Rectangle<float>(ledX, ledY, ledDiam, ledDiam);

            if (peakHoldEnabled_ && peakBrightness_ > 0.0f)
            {
                auto ledCol = peakLedOff_.interpolatedWith(peakLedOn_, peakBrightness_);
                g.setColour(ledCol);
                g.fillEllipse(ledRect);
                // Glow
                if (peakBrightness_ > 0.5f)
                {
                    g.setColour(peakLedOn_.withAlpha(peakBrightness_ * 0.25f));
                    g.fillEllipse(ledRect.expanded(2.0f));
                }
            }
            else
            {
                g.setColour(peakLedOff_);
                g.fillEllipse(ledRect);
            }
        }

        // --- Meter face (recessed area) ---
        auto meterFace = bounds.reduced(4.0f, 2.0f);
        g.setColour(meterFaceBg_);
        g.fillRoundedRectangle(meterFace, 4.0f);

        // --- Scale and needle area ---
        const float faceW = meterFace.getWidth();
        const float faceH = meterFace.getHeight();

        // Pivot point: bottom-center of meter face
        const float pivotX = meterFace.getCentreX();
        const float pivotY = meterFace.getBottom() + faceH * 0.15f;

        // Needle length: from pivot to near top of meter face
        const float needleLen = faceH * 0.92f;

        // Sweep: -45 deg to +45 deg from vertical
        constexpr float sweepMin = -juce::MathConstants<float>::pi * 0.25f;
        constexpr float sweepMax =  juce::MathConstants<float>::pi * 0.25f;

        // ── Draw scale markings ──
        struct ScaleMark { float dBu; const char* text; bool major; };
        static const ScaleMark marks[] = {
            { -20.0f, "-20", true  },
            { -15.0f, "",    false },
            { -10.0f, "-10", true  },
            {  -7.0f, "-7",  true  },
            {  -5.0f, "-5",  true  },
            {  -3.0f, "-3",  true  },
            {  -1.0f, "-1",  false },
            {   0.0f, "0",   true  },
            {   1.0f, "+1",  true  },
            {   2.0f, "+2",  true  },
            {   3.0f, "+3",  true  },
        };

        const float scaleRadius = needleLen * 0.82f;
        const float tickMajorLen = juce::jmax(5.0f, faceH * 0.06f);
        const float tickMinorLen = tickMajorLen * 0.55f;
        const float fontSize = juce::jmax(8.0f, juce::jmin(11.0f, faceW * 0.09f));

        g.setFont(juce::Font(juce::FontOptions(fontSize)));

        for (auto& m : marks)
        {
            float norm = dBuToNorm(m.dBu);
            float markAngle = sweepMin + norm * (sweepMax - sweepMin);

            float sinA = std::sin(markAngle);
            float cosA = std::cos(markAngle);

            float tickLen = m.major ? tickMajorLen : tickMinorLen;

            float tx1 = pivotX + sinA * scaleRadius;
            float ty1 = pivotY - cosA * scaleRadius;
            float tx2 = pivotX + sinA * (scaleRadius - tickLen);
            float ty2 = pivotY - cosA * (scaleRadius - tickLen);

            // Colour: white for <= 0, red for > 0
            bool overload = m.dBu > 0.0f;
            g.setColour(overload ? meterRed_ : scaleColour_);
            g.drawLine(tx1, ty1, tx2, ty2, m.major ? 1.5f : 1.0f);

            // Label
            if (m.major && m.text[0] != '\0')
            {
                float labelRadius = scaleRadius - tickLen - fontSize * 0.6f;
                float lx = pivotX + sinA * labelRadius;
                float ly = pivotY - cosA * labelRadius;
                auto textArea = juce::Rectangle<float>(lx - 16.0f, ly - fontSize * 0.5f,
                                                        32.0f, fontSize + 2.0f);
                g.setFont(juce::Font(juce::FontOptions(fontSize)));
                g.setColour(overload ? meterRed_ : scaleColour_);
                g.drawText(m.text, textArea, juce::Justification::centred, false);
            }
        }

        // Draw the red arc segment (0 to +3 range)
        {
            float normZero = dBuToNorm(0.0f);
            float normThree = dBuToNorm(3.0f);
            float angleStart = sweepMin + normZero * (sweepMax - sweepMin);
            float angleEnd   = sweepMin + normThree * (sweepMax - sweepMin);

            juce::Path redArc;
            float juceAngleStart = angleStart - juce::MathConstants<float>::halfPi;
            float juceAngleEnd   = angleEnd   - juce::MathConstants<float>::halfPi;

            redArc.addCentredArc(pivotX, pivotY,
                                  scaleRadius + 2.0f, scaleRadius + 2.0f,
                                  0.0f,
                                  juceAngleStart, juceAngleEnd,
                                  true);
            g.setColour(meterRed_.withAlpha(0.7f));
            g.strokePath(redArc, juce::PathStrokeType(2.0f));
        }

        // ── Draw needle ──
        {
            float norm  = dBuToNorm(currentSmoothed_);
            float needleAngle = sweepMin + norm * (sweepMax - sweepMin);

            float sinA = std::sin(needleAngle);
            float cosA = std::cos(needleAngle);

            float tipX = pivotX + sinA * needleLen;
            float tipY = pivotY - cosA * needleLen;

            // Needle shadow
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.drawLine(pivotX + 1.0f, pivotY + 1.0f, tipX + 1.0f, tipY + 1.0f, 1.8f);

            // Needle body
            g.setColour(needleColour_.withAlpha(0.75f));
            g.drawLine(pivotX, pivotY, tipX, tipY, 1.5f);

            // Bright tip (last 15%)
            float tipStartFrac = 0.85f;
            float midX = pivotX + sinA * (needleLen * tipStartFrac);
            float midY = pivotY - cosA * (needleLen * tipStartFrac);
            g.setColour(needleTip_);
            g.drawLine(midX, midY, tipX, tipY, 2.0f);

            // Pivot cap
            const float capR = juce::jmax(3.0f, faceW * 0.035f);
            g.setColour(juce::Colour(0xFF2A2A4A));
            g.fillEllipse(pivotX - capR, pivotY - capR, capR * 2.0f, capR * 2.0f);
            g.setColour(juce::Colour(0xFF4A4A6E));
            g.drawEllipse(pivotX - capR, pivotY - capR, capR * 2.0f, capR * 2.0f, 1.0f);
        }

        // --- Label ---
        g.setColour(labelColour_);
        g.setFont(juce::Font(juce::FontOptions(juce::jmax(10.0f, labelH * 0.80f)).withStyle("Bold")));
        g.drawText(label_, labelArea, juce::Justification::centred, false);
    }

    void resized() override {}

private:
    // ── Helpers ─────────────────────────────────────────────────────────────

    /** Map dBu to 0..1 normalised range for needle position.
     *  -20 dBu -> 0.0,  +3 dBu -> 1.0
     *  Piecewise mapping for authentic VU scale. */
    static float dBuToNorm(float dBu)
    {
        if (dBu <= -20.0f) return 0.0f;
        if (dBu >= 3.0f)   return 1.0f;

        if (dBu <= 0.0f)
        {
            // -20 to 0 -> 0.0 to 0.75
            float t = (dBu + 20.0f) / 20.0f;
            t = std::pow(t, 0.7f);
            return t * 0.75f;
        }
        else
        {
            // 0 to +3 -> 0.75 to 1.0
            float t = dBu / 3.0f;
            return 0.75f + t * 0.25f;
        }
    }

    // ── State ───────────────────────────────────────────────────────────────
    juce::String label_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel_;

    float currentSmoothed_ = -40.0f;
    float lastPaintedLevel_ = -100.0f;
    float lastPaintedPeak_  = 0.0f;

    // Peak hold
    bool  peakHoldEnabled_ = true;
    bool  peakHeld_        = false;
    int   peakHoldCounter_ = 0;
    float peakBrightness_  = 0.0f;

    static constexpr int   peakHoldFrames_ = 45;   // ~1.5 s at 30 fps
    static constexpr float peakFadeRate_   = 0.05f; // fade over ~20 frames

    // ── Colours ─────────────────────────────────────────────────────────────
    juce::Colour panelBg_      { 0xFF16213E };
    juce::Colour panelBorder_  { 0xFF2A2A4A };
    juce::Colour meterFaceBg_  { 0xFF111A30 };
    juce::Colour scaleColour_  { 0xFFCCCCEE };
    juce::Colour meterRed_     { 0xFFEF5350 };
    juce::Colour needleColour_ { 0xFFE0E0FF };
    juce::Colour needleTip_    { 0xFFFFFFFF };
    juce::Colour labelColour_  { 0xFFBBBBDD };
    juce::Colour peakLedOff_   { 0xFF3A1A1A };
    juce::Colour peakLedOn_    { 0xFFFF1744 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterComponent)
};
