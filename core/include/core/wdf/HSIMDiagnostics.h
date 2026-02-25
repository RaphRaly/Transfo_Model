#pragma once

// =============================================================================
// HSIMDiagnostics — Debug-build-only diagnostics for the HSIM solver.
//
// Computes the spectral radius rho(J_T) of the fixed-point operator
// T(v) during HSIM iterations. This is the key diagnostic for convergence:
//
//   rho > 1  →  Z poorly adapted (increase adaptation frequency)
//   rho < 1 but slow  →  nonlinearity too steep (relax epsilon)
//   rho << 1  →  fast convergence, everything is fine
//
// Active only in debug builds (#ifndef NDEBUG). Zero cost in release.
//
// [v3.1] Implementation cost: ~0.5 day. Can save weeks of Phase 3 debugging.
//
// Reference: Werner thesis Stanford ch.3-4 (spectral radius analysis)
// =============================================================================

#include "../util/SmallMatrix.h"
#include <cmath>
#include <algorithm>

namespace transfo {

template <int N>
class HSIMDiagnostics
{
public:
    HSIMDiagnostics() = default;

    // ─── Compute spectral radius from consecutive HSIM iterates ─────────────
    // v_current and v_prev are the nonlinear port wave vectors at
    // consecutive iterations. T_v is the operator output.
    //
    // Approximation: rho ≈ ||v_current - v_prev|| / ||v_prev - v_prev_prev||
    // (ratio of successive residuals — simple but effective)
    float computeSpectralRadius(const float* v_current, const float* v_prev,
                                 const float* v_prev_prev)
    {
#ifndef NDEBUG
        float norm_current = 0.0f;
        float norm_prev = 0.0f;

        for (int i = 0; i < N; ++i)
        {
            float diff_curr = v_current[i] - v_prev[i];
            float diff_prev = v_prev[i] - v_prev_prev[i];
            norm_current += diff_curr * diff_curr;
            norm_prev += diff_prev * diff_prev;
        }

        norm_current = std::sqrt(norm_current);
        norm_prev = std::sqrt(norm_prev);

        if (norm_prev < 1e-15f)
        {
            spectralRadius_ = 0.0f;
            return 0.0f;
        }

        spectralRadius_ = norm_current / norm_prev;
        maxSpectralRadius_ = std::max(maxSpectralRadius_, spectralRadius_);

        return spectralRadius_;
#else
        (void)v_current; (void)v_prev; (void)v_prev_prev;
        return 0.0f;
#endif
    }

    // ─── Query ──────────────────────────────────────────────────────────────
    bool isEnabled() const
    {
#ifndef NDEBUG
        return true;
#else
        return false;
#endif
    }

    float getLastSpectralRadius() const { return spectralRadius_; }
    float getMaxSpectralRadius()  const { return maxSpectralRadius_; }

    void resetMax() { maxSpectralRadius_ = 0.0f; }

    // ─── Diagnosis helpers ──────────────────────────────────────────────────
    bool isZPoorlyAdapted() const { return spectralRadius_ > 1.0f; }
    bool isConvergingSlow() const { return spectralRadius_ > 0.8f && spectralRadius_ <= 1.0f; }
    bool isConvergingFast() const { return spectralRadius_ < 0.5f; }

private:
    float spectralRadius_    = 0.0f;
    float maxSpectralRadius_ = 0.0f;
};

} // namespace transfo
