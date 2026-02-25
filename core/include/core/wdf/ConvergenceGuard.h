#pragma once

// =============================================================================
// ConvergenceGuard — Anti-click protection for HSIM solver non-convergence.
//
// When the HSIM fixed-point iteration fails to converge within maxIter,
// this guard provides graceful degradation instead of audio artifacts:
//
//   1. Converged:     store + return candidate output
//   2. Not converged: lerp(lastConverged, candidate, 0.5) — smooth blend
//   3. 3+ failures:   relax epsilon *= 2 to help convergence
//
// The failure counter is atomic for UI thread monitoring.
//
// [v2] Essential for production audio — prevents clicks/pops when the
// nonlinearity is too aggressive (high drive, low-frequency saturation).
//
// Reference: Werner thesis Stanford ch.3-4 (fixed-point convergence)
// =============================================================================

#include <atomic>
#include <cmath>

namespace transfo {

class ConvergenceGuard
{
public:
    ConvergenceGuard() = default;

    void configure(float baseEpsilon)
    {
        baseEpsilon_ = baseEpsilon;
        adaptiveEpsilon_ = baseEpsilon;
    }

    void reset()
    {
        lastConvergedOutput_ = 0.0f;
        consecutiveFailures_ = 0;
        adaptiveEpsilon_ = baseEpsilon_;
        failureCounter_.store(0, std::memory_order_relaxed);
    }

    // ─── Main interface: get safe output after HSIM iteration ───────────────
    float getSafeOutput(float candidate, bool converged)
    {
        if (converged)
        {
            lastConvergedOutput_ = candidate;
            consecutiveFailures_ = 0;
            adaptiveEpsilon_ = baseEpsilon_;
            return candidate;
        }

        // Not converged — blend with last known good output
        consecutiveFailures_++;
        failureCounter_.fetch_add(1, std::memory_order_relaxed);

        // Progressive relaxation after repeated failures
        if (consecutiveFailures_ >= 3)
        {
            adaptiveEpsilon_ *= 2.0f;
            // Cap relaxation to prevent completely bypassing convergence
            adaptiveEpsilon_ = std::min(adaptiveEpsilon_, baseEpsilon_ * 64.0f);
        }

        // Smooth blend: avoid hard discontinuity
        const float alpha = 0.5f;
        return lastConvergedOutput_ * (1.0f - alpha) + candidate * alpha;
    }

    // ─── Getters ────────────────────────────────────────────────────────────
    float getAdaptiveEpsilon() const { return adaptiveEpsilon_; }
    int   getConsecutiveFailures() const { return consecutiveFailures_; }

    // Atomic counter for UI monitoring (thread-safe read)
    int getFailureCount() const
    {
        return failureCounter_.load(std::memory_order_relaxed);
    }

    void resetFailureCount()
    {
        failureCounter_.store(0, std::memory_order_relaxed);
    }

private:
    float lastConvergedOutput_ = 0.0f;
    int   consecutiveFailures_ = 0;
    float adaptiveEpsilon_     = 1e-5f;
    float baseEpsilon_         = 1e-5f;

    std::atomic<int> failureCounter_{0};
};

} // namespace transfo
