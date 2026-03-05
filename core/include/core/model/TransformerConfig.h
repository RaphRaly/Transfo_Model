#pragma once

// =============================================================================
// TransformerConfig — Complete transformer configuration combining core,
// windings, material, and load parameters.
//
// Provides factory methods for known transformer models:
//   - Jensen JT-115K-E (1:10, mu-metal, line input)
//   - Neve Marinair LO1166 (output transformer, NiFe 50%)
//   - API AP2503 (line output, SiFe)
//
// Serializable to/from JSON for preset management.
// =============================================================================

#include "CoreGeometry.h"
#include "WindingConfig.h"
#include "../magnetics/JAParameterSet.h"
#include <string>

namespace transfo {

struct TransformerConfig
{
    std::string    name = "Default";
    CoreGeometry   core;
    WindingConfig  windings;
    JAParameterSet material;
    float          loadImpedance = 150000.0f;  // Secondary load [Ohm]

    // ── Factory: Jensen JT-115K-E ───────────────────────────────────────────
    // Line input transformer. Ratio 1:10. Mu-metal core.
    // Rdc_pri=19.7Ω, Rdc_sec=2465Ω, C_sec_shield=205pF
    // FR: -0.5dB@20Hz, -0.2dB@20kHz, BW~140kHz, CMRR~110dB@60Hz
    static TransformerConfig Jensen_JT115KE()
    {
        TransformerConfig cfg;
        cfg.name          = "Jensen JT-115K-E";
        cfg.core          = CoreGeometry::jensenJT115KE();
        cfg.windings      = WindingConfig::jensenJT115KE();
        cfg.material      = JAParameterSet::defaultMuMetal();
        cfg.loadImpedance = 150000.0f;
        return cfg;
    }

    // ── Factory: Jensen JT-115K-E in Harrison Preamp ────────────────────────
    // Same transformer, but embedded in Harrison preamp circuit:
    // Source: 14kΩ differential (R100/R101 = 2×6.8kΩ)
    // Load: ~160Ω (R104 damping to GND, R102/R103 = 680Ω amortissement)
    // C107 = 470pF HF damping on secondary
    // More coloration: high source Z → magnetizing current dominates
    // Damped HF: heavy secondary loading rounds off the top end
    static TransformerConfig Jensen_JT115KE_Harrison()
    {
        TransformerConfig cfg;
        cfg.name          = "Jensen Harrison Preamp";
        cfg.core          = CoreGeometry::jensenJT115KE();
        cfg.windings      = WindingConfig::jensenJT115KE_Harrison();
        cfg.material      = JAParameterSet::defaultMuMetal();
        cfg.loadImpedance = 160.0f;
        return cfg;
    }

    // ── Factory: Neve 1073 Input (Marinair 10468 / Carnhill VTB9045) ────────
    // Mic input transformer. NiFe 50% (Radiometal / Alloy 2). Ungapped.
    // Ratio 1:2, 300 ohm -> 1200 ohm, gain +6 dB.
    // Source: Neve Drawing EDO 71/13 (22/3/72) + Marinair T1444 catalogue.
    // THD: < 0.1% @ 40 Hz, < 0.01% @ 1 kHz+. FR: ±0.3 dB 20-20k.
    static TransformerConfig Neve_1073_Input()
    {
        TransformerConfig cfg;
        cfg.name          = "Neve 1073 Input (10468)";
        cfg.core          = CoreGeometry::neve10468Input();
        cfg.windings      = WindingConfig::neve10468Input();
        cfg.material      = JAParameterSet::defaultNiFe50();
        cfg.loadImpedance = 1200.0f;
        return cfg;
    }

    // ── Factory: Neve 1073 Output (LI1166 gapped) ───────────────────────────
    // Line output transformer. NiFe 50%. GAPPED — linearized B-H.
    // Step-down ~1:0.63, 200 ohm -> 600 ohm, gain -4 dB.
    // Source: Neve Drawing EDO 71/13 (22/3/72).
    // Gap adds linear reluctance R_gap = l_gap / (mu0 * A_core).
    // Ungapped variant LO2567 exists for "Neve Hot" preset.
    static TransformerConfig Neve_1073_Output()
    {
        TransformerConfig cfg;
        cfg.name          = "Neve 1073 Output (LI1166)";
        cfg.core          = CoreGeometry::neveLI1166Output();
        cfg.windings      = WindingConfig::neveLI1166Output();
        cfg.material      = JAParameterSet::defaultNiFe50();
        cfg.loadImpedance = 600.0f;
        return cfg;
    }

    // Legacy alias for backward compatibility
    static TransformerConfig Neve_Marinair_LO1166()
    {
        return Neve_1073_Output();
    }

    // ── Factory: Neve LO2567 "Hot" (ungapped output) ────────────────────────
    // Same transformer as LI1166 but WITHOUT air gap.
    // Ungapped → earlier saturation, more harmonic color.
    // Step-down 5:3, 200→600 ohm, gain -4 dB. NiFe 50%.
    static TransformerConfig Neve_LO2567_Hot()
    {
        TransformerConfig cfg;
        cfg.name          = "Neve LO2567 Hot (Ungapped)";
        cfg.core          = CoreGeometry::neveLO2567Hot();
        cfg.windings      = WindingConfig::neveLO2567Hot();
        cfg.material      = JAParameterSet::defaultNiFe50();
        cfg.loadImpedance = 600.0f;
        return cfg;
    }

    // ── Factory: Neve LO1173 Line Output ──────────────────────────────────
    // Line output of 1073. Drawing EDO 71/13, 6/11/73.
    // Cross-refs: LO1173 = VT22737 = VT22761 = T1684 = T1686
    // 70 ohm series → 600 ohm, gain -8 dB. NiFe 50%, ungapped.
    static TransformerConfig Neve_LO1173_Output()
    {
        TransformerConfig cfg;
        cfg.name          = "Neve LO1173 Line Output";
        cfg.core          = CoreGeometry::neveLO1173Output();
        cfg.windings      = WindingConfig::neveLO1173Output();
        cfg.material      = JAParameterSet::defaultNiFe50();
        cfg.loadImpedance = 600.0f;
        return cfg;
    }

    // ── Factory: API AP2503 ─────────────────────────────────────────────────
    // Line output transformer. Grain-oriented SiFe.
    static TransformerConfig API_AP2503()
    {
        TransformerConfig cfg;
        cfg.name          = "API AP2503";
        cfg.core          = CoreGeometry::neveMarinair();
        cfg.windings      = WindingConfig::apiAP2503();
        cfg.material      = JAParameterSet::defaultSiFe();
        cfg.loadImpedance = 10000.0f;
        return cfg;
    }
};

} // namespace transfo
