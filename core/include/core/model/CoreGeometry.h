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

    // ── Derived quantities ──────────────────────────────────────────────────
    float effectiveArea() const { return Lambda_center; }
    float effectiveLength() const { return Gamma_center + Gamma_yoke; }

    // ── Presets ─────────────────────────────────────────────────────────────
    static CoreGeometry jensenJT115KE()
    {
        // Jensen JT-115K-E: small mu-metal EI core
        return { 0.048f, 0.076f, 0.058f,
                 1.2e-4f, 1.2e-4f, 1.2e-4f };
    }

    static CoreGeometry neveMarinair()
    {
        // Neve Marinair LO1166: larger NiFe core
        return { 0.065f, 0.095f, 0.075f,
                 2.5e-4f, 2.5e-4f, 2.5e-4f };
    }
};

} // namespace transfo
