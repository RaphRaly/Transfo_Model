#pragma once

// =============================================================================
// TransformerConfig — Complete transformer configuration combining core,
// windings, material, and load parameters.
//
// Provides factory methods for:
//   - Jensen JT-115K-E (1:10, 80% NiFe mu-metal, line input)
//   - Jensen JT-11ELCF (1:1, 50% NiFe, line output)
//
// Serializable to/from JSON for preset management.
// =============================================================================

#include "CoreGeometry.h"
#include "WindingConfig.h"
#include "LCResonanceParams.h"
#include "TransformerGeometry.h"
#include "../magnetics/JAParameterSet.h"
#include <string>

namespace transfo {

struct TransformerConfig
{
    std::string         name = "Default";
    CoreGeometry        core;
    WindingConfig       windings;
    JAParameterSet      material;
    float               loadImpedance = 150000.0f;  // Secondary load [Ohm]

    // ── Impedance reflection helpers (referred to primary) ──────────────
    float Rdc_sec_reflected() const {
        float n = windings.turnsRatio();
        return windings.Rdc_secondary / (n * n);
    }
    float Rload_reflected() const {
        float n = windings.turnsRatio();
        return loadImpedance / (n * n);
    }
    float C_sec_reflected() const {
        float n = windings.turnsRatio();
        return windings.C_sec_shield * (n * n);
    }

    TransformerGeometry geometry;                     // K_geo for nonlinear Lm [v4]
    LCResonanceParams   lcParams;                    // LC parasitic resonance [v4]

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
        // K_geo = 50 [m] (fitted): mu-metal 1:10, produces Lm_max ~6 H at peak mu
        cfg.geometry.K_geo = 50.0f;
        // LC: Bessel alignment (Q~0.577), f_res ~320kHz (above audio)
        cfg.lcParams.Lleak = 5.0e-3f;      // 5 mH (high turns → high leakage)
        cfg.lcParams.Cw    = 50e-12f;       // 50 pF
        cfg.lcParams.Cp_s  = 10e-12f;       // 10 pF (Faraday shield)
        cfg.lcParams.CL    = 0.0f;
        cfg.lcParams.Rz    = 4700.0f;       // 4.7 kΩ Zobel
        cfg.lcParams.Cz    = 220e-12f;      // 220 pF Zobel
        return cfg;
    }

    // ── Factory: Jensen JT-11ELCF ─────────────────────────────────────────────
    // Line output transformer. Ratio 1:1 bifilar. 50% NiFe core.
    // Rdc=40Ω/winding, Cw=22nF, BW=0.18Hz-15MHz, THD<0.001%@1kHz/+4dBu
    // Drives 600Ω loads to +24dBu @20Hz. Insertion loss: -1.1dB.
    static TransformerConfig Jensen_JT11ELCF()
    {
        TransformerConfig cfg;
        cfg.name          = "Jensen JT-11ELCF";
        cfg.core          = CoreGeometry::jensenJT11ELCF();
        cfg.windings      = WindingConfig::jensenJT11ELCF();
        cfg.material      = JAParameterSet::output50NiFe();
        cfg.loadImpedance = 600.0f;     // 600 Ohm line load (secondary)
        // K_geo = 5300 [m] (fitted): 50% NiFe 1:1 bifilar, high turns count
        // Produces Lm ~33 H at nominal mu, consistent with f_3dB = 0.18 Hz
        cfg.geometry.K_geo = 5300.0f;
        // LC: bifilar → ultra-low leakage (2 uH), high Cw (22 nF)
        // f_res ~760 kHz (far above audio). No Zobel needed.
        cfg.lcParams.Lleak = 2.0e-6f;      // 2 uH (bifilar winding)
        cfg.lcParams.Cw    = 22e-9f;       // 22 nF (datasheet: winding-to-winding)
        cfg.lcParams.Cp_s  = 50e-12f;      // 50 pF (datasheet: windings-to-frame)
        cfg.lcParams.CL    = 0.0f;
        cfg.lcParams.Rz    = 0.0f;         // No Zobel (f_res >> audio)
        cfg.lcParams.Cz    = 0.0f;
        return cfg;
    }
};

} // namespace transfo
