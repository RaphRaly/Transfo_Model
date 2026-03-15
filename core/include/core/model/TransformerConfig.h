#pragma once

// =============================================================================
// TransformerConfig — Complete transformer configuration combining core,
// windings, material, and load parameters.
//
// Provides factory method for the Jensen JT-115K-E (1:10, mu-metal, line input).
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
};

} // namespace transfo
