#pragma once

// =============================================================================
// WindingConfig — Electrical parameters of transformer windings.
//
// Defines turns ratio, DC resistance, parasitic capacitances, and
// leakage inductance for a transformer winding configuration.
//
// Jensen JT-115K-E reference (from datasheet):
//   Ratio: 1:10, Rdc_pri=19.7Ω, Rdc_sec=2465Ω, C_sec_shield=205pF
//   Source: 150Ω, Load: 150kΩ (secondary)
//   Lp~10H (derived from fc~2.5Hz), BW~140kHz
// =============================================================================

namespace transfo {

struct WindingConfig
{
    // ── Turns ───────────────────────────────────────────────────────────────
    int   turnsRatio_N1 = 1;         // Primary turns
    int   turnsRatio_N2 = 10;        // Secondary turns

    // ── DC Resistances ──────────────────────────────────────────────────────
    float Rdc_primary   = 19.7f;     // Primary DC resistance [Ohm]
    float Rdc_secondary = 2465.0f;   // Secondary DC resistance [Ohm]

    // ── Parasitic Capacitances ──────────────────────────────────────────────
    float C_sec_shield      = 205e-12f; // Secondary-to-shield capacitance [F]
    float C_interwinding    = 10e-12f;  // Interwinding capacitance [F] (to be fitted)

    // ── Inductances ─────────────────────────────────────────────────────────
    float Lp_primary    = 10.0f;     // Primary magnetizing inductance [H]
    float L_leakage     = 1e-3f;     // Leakage inductance [H] (to be fitted on FR)

    // ── Source/Load impedances ──────────────────────────────────────────────
    float sourceImpedance = 150.0f;      // Nominal source [Ohm]
    float loadImpedance   = 150000.0f;   // Nominal load [Ohm] (secondary referred)

    // ── Derived ─────────────────────────────────────────────────────────────
    float turnsRatio() const
    {
        return static_cast<float>(turnsRatio_N2) / static_cast<float>(turnsRatio_N1);
    }

    // ── Presets ─────────────────────────────────────────────────────────────
    static WindingConfig jensenJT115KE()
    {
        return { 1, 10,  19.7f, 2465.0f,  205e-12f, 10e-12f,
                 10.0f, 1e-3f,  150.0f, 150000.0f };
    }

    static WindingConfig neveLO1166()
    {
        return { 1, 10,  50.0f, 5000.0f,  150e-12f, 8e-12f,
                 20.0f, 2e-3f,  300.0f, 10000.0f };
    }

    static WindingConfig apiAP2503()
    {
        return { 1, 5,  15.0f, 375.0f,  100e-12f, 5e-12f,
                 5.0f, 0.5e-3f,  600.0f, 10000.0f };
    }
};

} // namespace transfo
