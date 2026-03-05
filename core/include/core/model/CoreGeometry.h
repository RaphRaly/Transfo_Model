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

    static CoreGeometry neve10468Input()
    {
        // Neve 10468 / Marinair T1444: mic input, EI NiFe 50%, ungapped
        // Larger core than Jensen (higher saturation current)
        CoreGeometry g;
        g.Gamma_center = 0.060f; g.Gamma_outer = 0.090f; g.Gamma_yoke = 0.070f;
        g.Lambda_center = 2.2e-4f; g.Lambda_outer = 2.2e-4f; g.Lambda_yoke = 2.2e-4f;
        g.airGapLength = 0.0f;
        return g;
    }

    static CoreGeometry neveLI1166Output()
    {
        // Neve LI1166: line output, EI NiFe 50%, GAPPED
        // Gap linearizes B-H → less saturation, more headroom
        CoreGeometry g;
        g.Gamma_center = 0.065f; g.Gamma_outer = 0.095f; g.Gamma_yoke = 0.075f;
        g.Lambda_center = 2.5e-4f; g.Lambda_outer = 2.5e-4f; g.Lambda_yoke = 2.5e-4f;
        g.airGapLength = 0.0001f; // 0.1 mm air gap
        return g;
    }

    static CoreGeometry neveLO2567Hot()
    {
        // Neve LO2567 "Hot": same physical core as LI1166 but UNGAPPED
        // Removes the linear reluctance → earlier saturation, more color
        CoreGeometry g;
        g.Gamma_center = 0.065f; g.Gamma_outer = 0.095f; g.Gamma_yoke = 0.075f;
        g.Lambda_center = 2.5e-4f; g.Lambda_outer = 2.5e-4f; g.Lambda_yoke = 2.5e-4f;
        g.airGapLength = 0.0f; // ungapped — saturates earlier than LI1166
        return g;
    }

    static CoreGeometry neveLO1173Output()
    {
        // Neve LO1173: line output of 1073, Drawing EDO 71/13 (6/11/73)
        // Cross-refs: VT22737 / VT22761 / T1684 / T1686
        // EI NiFe 50%, ungapped, slightly smaller than neve10468
        CoreGeometry g;
        g.Gamma_center = 0.055f; g.Gamma_outer = 0.085f; g.Gamma_yoke = 0.065f;
        g.Lambda_center = 2.0e-4f; g.Lambda_outer = 2.0e-4f; g.Lambda_yoke = 2.0e-4f;
        g.airGapLength = 0.0f; // ungapped
        return g;
    }

    static CoreGeometry neveMarinair()
    {
        // Legacy alias — now points to LI1166 output geometry
        return neveLI1166Output();
    }
};

} // namespace transfo
