#pragma once

// =============================================================================
// ABCrossfade — Click-free A/B switching between two amplifier paths.
//
// Provides smooth crossfading between the Neve Class-A path (Chemin A) and
// the JE-990 path (Chemin B) using an equal-power cos^2/sin^2 law.
//
// Signal flow:
//   sampleA (Neve)  ──┬──[ gA = cos(pos * pi/2) ]──┐
//                     │                              ├── output = gA*A + gB*B
//   sampleB (JE-990) ─┴──[ gB = sin(pos * pi/2) ]──┘
//
// The internal position parameter (0 = pure A, 1 = pure B) is exponentially
// smoothed toward the target to eliminate discontinuities / clicks when
// switching paths during playback.
//
// Equal-power guarantee:
//   gA^2 + gB^2 = cos^2(theta) + sin^2(theta) = 1
// This preserves perceived loudness throughout the crossfade, unlike a
// linear crossfade which dips by ~3 dB at the midpoint.
//
// Smoothing model:
//   position += coeff * (target - position)   [exponential one-pole]
//   coeff = 1 - exp(-1 / (fs * fadeTime))     [~63% convergence in fadeTime]
//
// Pattern: Utility / mix operator for Strategy (GoF) path switching in
//          PreampModel.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md §5 (A/B Topology Switching);
//            S. Siltanen, "Crossfading Techniques", DAFx-06.
// =============================================================================

#include <cmath>
#include "../util/Constants.h"

namespace transfo {

class ABCrossfade
{
public:
    ABCrossfade() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Configure smoothing. Call once at init.
    /// @param sampleRate  Host sample rate [Hz]
    /// @param fadeTimeMs  Crossfade duration [ms] (default 5ms)
    void prepare(float sampleRate, float fadeTimeMs = 5.0f)
    {
        sampleRate_ = sampleRate;
        fadeTimeMs_ = fadeTimeMs;
        coeff_ = 1.0f - std::exp(-1.0f / (sampleRate * fadeTimeMs * 0.001f));
        reset();
    }

    /// Reset to path A immediately (no fade).
    void reset()
    {
        position_ = 0.0f;
        target_   = 0.0f;
        updateGains();
    }

    // ── Parameter control ─────────────────────────────────────────────────────

    /// Set target path position. 0.0 = 100% Chemin A, 1.0 = 100% Chemin B.
    /// Transition will be smoothed over fadeTimeMs.
    void setPosition(float target)
    {
        target_ = std::fmax(0.0f, std::fmin(1.0f, target));
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    /// Process one sample pair. Returns the crossfaded output.
    /// @param sampleA  Output from Neve path (Chemin A)
    /// @param sampleB  Output from JE-990 path (Chemin B)
    /// @return         Weighted mix of A and B
    float processSample(float sampleA, float sampleB)
    {
        position_ += coeff_ * (target_ - position_);

        if (!isTransitioning())
            position_ = target_;

        updateGains();

        return gainA_ * sampleA + gainB_ * sampleB;
    }

    // ── State queries ─────────────────────────────────────────────────────────

    /// True while fading between paths.
    bool isTransitioning() const
    {
        return std::fabs(position_ - target_) > 1e-6f;
    }

    /// Current position (0=A, 1=B).
    float getPosition() const { return position_; }

    /// Current gain applied to path A [linear].
    float getGainA() const { return gainA_; }

    /// Current gain applied to path B [linear].
    float getGainB() const { return gainB_; }

private:
    // ── Smoothing state ───────────────────────────────────────────────────────
    float sampleRate_ = kDefaultSampleRate;
    float fadeTimeMs_ = 5.0f;
    float coeff_      = 0.0f;
    float position_   = 0.0f;
    float target_     = 0.0f;

    // ── Crossfade gains ───────────────────────────────────────────────────────
    float gainA_ = 1.0f;
    float gainB_ = 0.0f;

    // ── Internal helpers ──────────────────────────────────────────────────────

    void updateGains()
    {
        const float theta = position_ * kPif * 0.5f;
        gainA_ = std::cos(theta);
        gainB_ = std::sin(theta);
    }
};

} // namespace transfo
