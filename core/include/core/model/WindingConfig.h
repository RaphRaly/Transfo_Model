#pragma once

// =============================================================================
// WindingConfig — Electrical parameters of transformer windings.
//
// Defines turns ratio, DC resistance, parasitic capacitances, and
// leakage inductance for a transformer winding configuration.
//
// Jensen JT-115K-E reference (from datasheet):
//   Ratio: 1:10, Rdc_pri=19.7Ω, Rdc_sec=2465Ω, C_sec_shield=205pF, C_pri_shield=475pF
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
    float C_pri_shield     = 475e-12f; // Primary-to-shield capacitance [F] (JT-115K-E datasheet)

    // ── Inductances ─────────────────────────────────────────────────────────
    float Lp_primary    = 10.0f;     // Primary magnetizing inductance [H]
    float L_leakage     = 5e-3f;     // Leakage inductance [H] (harmonized with LCResonanceParams)

    // ── Source/Load impedances ──────────────────────────────────────────────
    float sourceImpedance = 150.0f;      // Nominal source [Ohm] (winding/circuit R, not plate)
    float loadImpedance   = 150000.0f;   // Nominal load [Ohm] (secondary referred)

    // ── Plate impedance (tube output stages) ──────────────────────────────
    // For tube output transformers, the plate impedance drives the HP
    // filter (bass rolloff from source Z / Lp interaction). Stored
    // separately from sourceImpedance so the LC filter can use a
    // winding-only Rs while the HP filter sees the full tube impedance.
    // Set to 0 for non-tube circuits (line-level, solid-state).
    float plateImpedance  = 0.0f;        // Tube plate impedance [Ohm] (0 = unused)

    // ── Faraday shield ─────────────────────────────────────────────────────
    // True if the transformer has a Faraday (electrostatic) shield between
    // primary and secondary. When present, the shield grounds Cp_s so it
    // acts as a simple shunt capacitance. Without a shield, Cp_s is a
    // bridging capacitance whose effective value depends on the turns ratio
    // (Duerdoth / Miller effect).
    bool hasFaradayShield = true;        // Default true for most studio transformers

    // ── Derived ─────────────────────────────────────────────────────────────
    float turnsRatio() const
    {
        return static_cast<float>(turnsRatio_N2) / static_cast<float>(turnsRatio_N1);
    }

    // ── Presets ─────────────────────────────────────────────────────────────
    static WindingConfig jensenJT115KE()
    {
        WindingConfig w;
        w.turnsRatio_N1 = 1; w.turnsRatio_N2 = 10;
        w.Rdc_primary = 19.7f; w.Rdc_secondary = 2465.0f;
        w.C_sec_shield = 205e-12f; w.C_interwinding = 10e-12f;
        w.C_pri_shield = 475e-12f;
        w.Lp_primary = 10.0f; w.L_leakage = 5e-3f;
        w.sourceImpedance = 150.0f; w.loadImpedance = 150000.0f;
        w.hasFaradayShield = true;  // Jensen: shielded
        return w;
    }
};

} // namespace transfo
