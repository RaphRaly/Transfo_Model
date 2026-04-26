#pragma once
#include <cmath>

// =============================================================================
// HysteresisUtils — Mathematical utility functions for the Jiles-Atherton model.
//
// The Langevin function L(x) = coth(x) - 1/x describes the mean-field
// approximation of domain alignment in ferromagnetic materials.
// Both L(x) and L'(x) have singularities at x = 0 that require Taylor
// series approximations for numerical stability.
//
// Reference: Jiles & Atherton, "Theory of ferromagnetic hysteresis",
//            J. Magn. Magn. Mater. 61, 48-60 (1986)
// =============================================================================

namespace HysteresisUtils
{

// ─── Langevin function: L(x) = coth(x) - 1/x ────────────────────────────────
// Near x = 0: Taylor expansion L(x) ≈ x/3 - x³/45 + ...
// For large |x|: L(x) → ±1 (saturation)
inline double langevin(double x)
{
    const double abs_x = std::abs(x);

    if (abs_x < 1e-4)
        return x / 3.0;            // First-order Taylor, error O(x³)

    if (abs_x > 20.0)
        return (x > 0.0) ? 1.0 : -1.0;   // Asymptotic saturation

    return 1.0 / std::tanh(x) - 1.0 / x;  // coth(x) - 1/x
}

// ─── Derivative of Langevin: L'(x) = 1/x² - csch²(x) ────────────────────────
// Near x = 0: Taylor expansion L'(x) ≈ 1/3 - x²/15 + ...
// For large |x|: L'(x) → 0
inline double langevinDeriv(double x)
{
    const double abs_x = std::abs(x);

    if (abs_x < 1e-4)
        return 1.0 / 3.0;          // First-order Taylor, error O(x²)

    if (abs_x > 20.0)
        return 0.0;                 // Derivative → 0 in saturation

    const double csch = 1.0 / std::sinh(x);
    return 1.0 / (x * x) - csch * csch;
}

// ─── Physical constants ───────────────────────────────────────────────────────
constexpr double mu_0 = 1.2566370614e-6;   // Permeability of free space (H/m)

} // namespace HysteresisUtils
