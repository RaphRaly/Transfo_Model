#pragma once

// =============================================================================
// SmoothedValue — Parameter smoothing for audio-thread-safe value changes.
//
// Prevents zipper noise when parameters change. Linear ramp over a
// configurable number of samples. No JUCE dependency.
//
// Usage:
//   SmoothedValue<float> gain;
//   gain.reset(sampleRate, 0.02);   // 20ms ramp
//   gain.setTargetValue(0.5f);
//   for (int i = 0; i < N; ++i)
//       output[i] = input[i] * gain.getNextValue();
// =============================================================================

#include <cmath>
#include <algorithm>

namespace transfo {

template <typename T>
class SmoothedValue
{
public:
    SmoothedValue() = default;

    explicit SmoothedValue(T initialValue)
        : currentValue_(initialValue), targetValue_(initialValue) {}

    // Set ramp time in seconds. Call once in prepareToPlay.
    void reset(double sampleRate, double rampTimeSeconds)
    {
        rampLength_ = static_cast<int>(sampleRate * rampTimeSeconds);
        if (rampLength_ < 1) rampLength_ = 1;
        countdown_ = 0;
    }

    // Set new target value. Ramp will start from current position.
    void setTargetValue(T newTarget)
    {
        if (std::abs(newTarget - targetValue_) < T(1e-9))
            return;

        targetValue_ = newTarget;
        countdown_ = rampLength_;
        step_ = (targetValue_ - currentValue_) / static_cast<T>(rampLength_);
    }

    // Set value immediately (no smoothing). Use at init or reset.
    void setCurrentAndTargetValue(T value)
    {
        currentValue_ = value;
        targetValue_ = value;
        countdown_ = 0;
        step_ = T(0);
    }

    // Get next smoothed value (call once per sample).
    T getNextValue()
    {
        if (countdown_ > 0)
        {
            --countdown_;
            currentValue_ += step_;

            if (countdown_ == 0)
                currentValue_ = targetValue_; // Snap to target
        }
        return currentValue_;
    }

    // Get current value without advancing.
    T getCurrentValue() const { return currentValue_; }
    T getTargetValue()  const { return targetValue_; }

    // Check if currently smoothing.
    bool isSmoothing() const { return countdown_ > 0; }

    // Skip smoothing, jump to target immediately.
    void skip()
    {
        currentValue_ = targetValue_;
        countdown_ = 0;
    }

private:
    T   currentValue_ = T(0);
    T   targetValue_  = T(0);
    T   step_         = T(0);
    int rampLength_   = 1;
    int countdown_    = 0;
};

} // namespace transfo
