#pragma once

// =============================================================================
// CoreGeometry — Physical dimensions of a transformer core.
//
// Defines the magnetic path lengths and cross-section areas for each
// leg of the core. Used to compute WDF port resistances and convert
// between wave variables and magnetic quantities (H, B, Phi).
//
// Typical audio transformer: E-I or EI core with 3 legs.
//   - Center leg: primary winding
//   - Outer legs: return path (or secondary windings for split designs)
// =============================================================================

namespace transfo {

struct CoreGeometry
{
    // ── Magnetic path lengths (meters) ──────────────────────────────────────
    float Gamma_center = 0.05f;     // Center leg path length [m]
    float Gamma_outer  = 0.08f;     // Outer leg path length [m]
    float Gamma_yoke   = 0.06f;     // Yoke path length [m]

    // ── Cross-section areas (square meters) ─────────────────────────────────
    float Lambda_center = 1.5e-4f;  // Center leg cross-section [m^2]
    float Lambda_outer  = 1.5e-4f;  // Outer leg cross-section [m^2]
    float Lambda_yoke   = 1.5e-4f;  // Yoke cross-section [m^2]

    // ── Air gap (meters) — 0 = ungapped ─────────────────────────────────────
    // Gapped cores (e.g. Neve LI1166) add a linear reluctance in series:
    //   R_gap = l_gap / (mu0 * A_core)
    // This linearizes the B-H curve, reduces saturation, increases headroom.
    float airGapLength = 0.0f;      // Air gap length [m] (0 = no gap)

    // ── Derived quantities ──────────────────────────────────────────────────
    float effectiveArea() const { return Lambda_center; }
    float effectiveLength() const { return Gamma_center + Gamma_yoke + airGapLength; }
    bool  isGapped() const { return airGapLength > 0.0f; }

    // ── Presets ─────────────────────────────────────────────────────────────
    static CoreGeometry jensenJT115KE()
    {
        // Jensen JT-115K-E: small mu-metal EI core, ungapped
        CoreGeometry g;
        g.Gamma_center = 0.048f; g.Gamma_outer = 0.076f; g.Gamma_yoke = 0.058f;
        g.Lambda_center = 1.2e-4f; g.Lambda_outer = 1.2e-4f; g.Lambda_yoke = 1.2e-4f;
        g.airGapLength = 0.0f;
        return g;
    }

    static CoreGeometry jensenJT11ELCF()
    {
        // Jensen JT-11ELCF: 50% NiFe EI core, ungapped, 1:1 line output
        // Physical: ~43mm x 29mm x 28mm (datasheet drawing)
        // Lambda ~3.8e-4 m² estimated from core window and stack height
        // (larger effective area than JT-115K-E due to output level requirements)
        CoreGeometry g;
        g.Gamma_center = 0.035f; g.Gamma_outer = 0.055f; g.Gamma_yoke = 0.042f;
        g.Lambda_center = 3.8e-4f; g.Lambda_outer = 3.8e-4f; g.Lambda_yoke = 3.8e-4f;
        g.airGapLength = 0.0f;
        return g;
    }

    // ── Compute K_geo from this geometry and a given primary turns count ─────
    // K_geo = N^2 * A_eff / l_eff
    float computeKgeo(int N_primary) const
    {
        const float l_eff = effectiveLength();
        if (l_eff <= 0.0f) return 10.0f;
        return static_cast<float>(N_primary * N_primary) * effectiveArea() / l_eff;
    }
};

} // namespace transfo
