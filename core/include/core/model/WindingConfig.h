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

    // Neve 10468 / Marinair T1444 mic input (1073)
    // Drawing EDO 71/13: 300 ohm parallel -> 1200 ohm, gain +6 dB
    // Ratio 1:2, FR ±0.3 dB 20Hz-20kHz
    static WindingConfig neve10468Input()
    {
        WindingConfig w;
        w.turnsRatio_N1 = 1;  w.turnsRatio_N2 = 2;
        w.Rdc_primary = 8.0f; w.Rdc_secondary = 32.0f;
        w.C_sec_shield = 120e-12f; w.C_interwinding = 8e-12f;
        w.Lp_primary = 5.0f;  w.L_leakage = 0.5e-3f;
        w.sourceImpedance = 300.0f; w.loadImpedance = 1200.0f;
        return w;
    }

    // Neve LI1166 line output (1073, gapped)
    // Drawing EDO 71/13: 200 ohm series -> 600 ohm, gain -4 dB
    // Step-down ~1:0.63, gapped core for linearity
    static WindingConfig neveLI1166Output()
    {
        WindingConfig w;
        w.turnsRatio_N1 = 5;  w.turnsRatio_N2 = 3;
        w.Rdc_primary = 12.0f; w.Rdc_secondary = 18.0f;
        w.C_sec_shield = 100e-12f; w.C_interwinding = 6e-12f;
        w.Lp_primary = 8.0f;  w.L_leakage = 1e-3f;
        w.sourceImpedance = 200.0f; w.loadImpedance = 600.0f;
        return w;
    }

    // Jensen JT-115K-E in Harrison Preamp circuit
    // Primary: 2x 6.8kΩ series (R100/R101) → Z_source = 13.6kΩ differential
    // Secondary: R102/R103 (680Ω damping) + R104 (160Ω to GND) + C107 (470pF)
    // Effective secondary load ≈ 160Ω (R104 dominates)
    static WindingConfig jensenJT115KE_Harrison()
    {
        WindingConfig w = jensenJT115KE(); // Same transformer
        w.sourceImpedance = 13600.0f;      // 2 × 6.8kΩ (R100 + R101)
        w.loadImpedance   = 160.0f;        // R104 (dominant secondary load)
        return w;
    }

    // Neve LO2567 "Hot" (ungapped output variant of LI1166)
    // Same windings as LI1166: 200→600 ohm, step-down 5:3, gain -4dB
    // The difference is in the core (ungapped) — more saturation, more color
    static WindingConfig neveLO2567Hot()
    {
        return neveLI1166Output(); // Same electrical parameters
    }

    // Neve LO1173 line output (1073)
    // Drawing EDO 71/13 (6/11/73), cross-refs: VT22737/VT22761/T1684/T1686
    // 70 ohm series → 600 ohm, gain -8 dB, step-down 3:1
    static WindingConfig neveLO1173Output()
    {
        WindingConfig w;
        w.turnsRatio_N1 = 3;  w.turnsRatio_N2 = 1;
        w.Rdc_primary = 5.0f; w.Rdc_secondary = 15.0f;
        w.C_sec_shield = 80e-12f; w.C_interwinding = 5e-12f;
        w.Lp_primary = 3.0f;  w.L_leakage = 0.3e-3f;
        w.sourceImpedance = 70.0f; w.loadImpedance = 600.0f;
        return w;
    }

    static WindingConfig neveLO1166()
    {
        // Legacy alias — now points to LI1166 output
        return neveLI1166Output();
    }

    static WindingConfig apiAP2503()
    {
        return { 1, 5,  15.0f, 375.0f,  100e-12f, 5e-12f,
                 5.0f, 0.5e-3f,  600.0f, 10000.0f };
    }

    // Fender-style output transformer
    // Push-pull plate-to-speaker, step-down ~25:1 (centertapped primary)
    // Low source Z (tube plate), low load Z (speaker)
    static WindingConfig fenderOutput()
    {
        WindingConfig w;
        w.turnsRatio_N1 = 25; w.turnsRatio_N2 = 1;
        w.Rdc_primary = 120.0f; w.Rdc_secondary = 0.5f;
        w.C_sec_shield = 50e-12f; w.C_interwinding = 15e-12f;
        w.Lp_primary = 20.0f; w.L_leakage = 5e-3f;
        w.sourceImpedance = 3200.0f; w.loadImpedance = 8.0f;
        return w;
    }

    // Lundahl LL1538: premium line input, mu-metal
    // Ratio 1:8, 150 ohm → high-Z, similar concept to Jensen JT-115K-E
    static WindingConfig lundahlLL1538()
    {
        WindingConfig w;
        w.turnsRatio_N1 = 1; w.turnsRatio_N2 = 8;
        w.Rdc_primary = 16.0f; w.Rdc_secondary = 1800.0f;
        w.C_sec_shield = 180e-12f; w.C_interwinding = 8e-12f;
        w.Lp_primary = 12.0f; w.L_leakage = 0.8e-3f;
        w.sourceImpedance = 150.0f; w.loadImpedance = 100000.0f;
        return w;
    }
};

} // namespace transfo
