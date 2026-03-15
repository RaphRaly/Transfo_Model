#pragma once

// =============================================================================
// TransformerGeometry — Lumped geometric constant for magnetizing inductance.
//
// DEFINITION:
//   K_geo = N^2 * A_eff / l_eff  [units: meters]
//
// where:
//   N     = primary turns count (dimensionless)
//   A_eff = effective core cross-section [m^2]
//   l_eff = effective magnetic path length [m]
//
// USAGE:
//   Lm(t) = K_geo * mu_inc(t)  [Henry]
//
// where:
//   mu_inc = mu0 * (1 + dM/dH)  [H/m]
//   mu0    = 1.2566e-6 [H/m]  (permeability of free space)
//   dM/dH  = incremental susceptibility from J-A model (dimensionless)
//
// PRESET VALUES: The hardcoded K_geo values in presets (e.g., 50 for Jensen)
// are FITTED CONSTANTS derived from experimental tuning, NOT computed from
// the formula using N, A_eff, l_eff. They may differ from geometric
// estimates by 5-50x due to winding distribution, fringing, and flux
// concentration effects. For best accuracy, fit K_geo to measured data.
//
// Typical fitted values:
//   Mic input (Jensen):    K_geo ~ 50 m
//   Line output (studio):  K_geo ~ 8-30 m
//   Guitar amp output:     K_geo ~ 150-200 m
//
// Reference: nonlinear-lm-and-lc-resonance-extension.md §A.4
// =============================================================================

#include "../util/Constants.h"
#include <algorithm>
#include <cmath>

namespace transfo {

struct TransformerGeometry
{
    float K_geo = 10.0f;  // Lumped geometric constant [m], multiply by mu_inc [H/m] to get Lm [H]

    // ── Compute Lm from incremental permeability ─────────────────────────────
    // mu_inc = mu0 * (1 + dM/dH) from J-A model
    float computeLm(float mu_inc) const
    {
        return K_geo * mu_inc;
    }

    // ── Compute WDF port resistance for magnetizing inductance ───────────────
    // R_port = 2 * Lm * fs  (trapezoidal discretization)
    float computeRport(float Lm, float sampleRate) const
    {
        return 2.0f * Lm * sampleRate;
    }

    // ── Compute K_geo from physical dimensions ───────────────────────────────
    // INPUT:  N = primary turns [dimensionless]
    //         A_eff = effective core cross-section [m^2]
    //         l_eff = effective magnetic path length [m]
    // OUTPUT: K_geo [m]
    // NOTE:   This gives a geometric estimate. Actual K_geo for a real
    //         transformer may differ by 5-50x. For best accuracy, fit
    //         K_geo from measured frequency response data.
    static float computeKgeo(int N, float A_eff, float l_eff)
    {
        if (l_eff <= 0.0f)
            return 10.0f;
        return static_cast<float>(N * N) * A_eff / l_eff;
    }

    // ── Validation ───────────────────────────────────────────────────────────
    bool isPhysicallyValid() const
    {
        return K_geo > 0.0f;
    }
};

} // namespace transfo
