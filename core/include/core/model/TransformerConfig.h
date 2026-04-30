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
#include <cmath>
#include <string>

namespace transfo {

// ─── Calibration Mode ─────────────────────────────────────────────────────────
// LegacyColor : historical hScale = a × 5 behavior. Deliberately overdrives
//               the virtual core for musical coloration at normal audio levels.
// Artistic    : Ampere-law reference scaling at calibrationFreqHz. This is an
//               honest coloring-grade calibration, not a predictive coupled
//               transformer solver.
enum class CalibrationMode {
    LegacyColor,
    Artistic,
    Physical [[deprecated("Use Artistic; Physical is reserved for a future coupled DAE solver")]] = Artistic
};

struct TransformerConfig
{
    std::string         name = "Default";
    std::string         displayName = "";          // A1.3: Musical/user-friendly name
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

    // ── Calibration ──────────────────────────────────────────────────────────
    CalibrationMode calibrationMode = CalibrationMode::LegacyColor;
    float calibrationFreqHz = 1000.0f;  // Reference frequency for Artistic calibration [Hz]

    // ── Flux Integrator (Problem #4 fix) ────────────────────────────────────
    // When enabled, replaces H = V × hScale (frequency-independent) with a
    // leaky integrator that provides B_peak ∝ 1/f (correct transformer physics).
    // At calibrationFreqHz, behavior is identical to the fixed-hScale model.
    //
    // Default OFF — only meaningful in Artistic calibration where hScale is
    // derived from Ampère's law. In LegacyColor calibration, hScale = a×5 is
    // already tuned for musical saturation; adding the 1/f integrator on top
    // produces meaningless over-saturation at LF (THD > 50% at −20 dBu).
    bool fluxIntegratorEnabled = false;

    // ── Legacy gain calibration (per-preset) ─────────────────────────────
    // Offsets applied on top of the Legacy GUI input/output knobs to bring
    // each preset to a "unity-ish" working point. Defaults to 0 so that
    // Double Legacy / Harrison cascades (which instantiate the raw factory
    // configs) are not affected. The Legacy mode in PluginProcessor::
    // applyPreset() overrides these to -10 / +15 dB across all Legacy
    // presets (the historical Codex GPT-5.4 calibration). Per-preset
    // refinement is possible by overriding here in any future factory.
    float legacyInputOffsetDb  = 0.0f;
    float legacyOutputOffsetDb = 0.0f;

    // ── Derive N_primary from K_geo and core geometry ────────────────────────
    // K_geo = N² · A_e / l_e  →  N = sqrt(K_geo · l_e / A_e)
    // This is an effective N consistent with the fitted K_geo, not necessarily
    // the physical turns count (K_geo can differ 5-50x from geometric estimate).
    float estimateNprimary() const {
        const float l_e = core.effectiveLength();
        const float A_e = core.effectiveArea();
        if (A_e <= 0.0f || l_e <= 0.0f) return 100.0f;
        return std::sqrt(geometry.K_geo * l_e / A_e);
    }

    // ── Factory: Jensen JT-115K-E ───────────────────────────────────────────
    // Line input transformer. Ratio 1:10. Mu-metal core.
    // Rdc_pri=19.7Ω, Rdc_sec=2465Ω, C_sec_shield=205pF
    // FR: -0.5dB@20Hz, -0.2dB@20kHz, BW~90kHz (-3dB), CMRR~110dB@60Hz
    static TransformerConfig Jensen_JT115KE()
    {
        TransformerConfig cfg;
        cfg.name          = "Jensen JT-115K-E";
        cfg.displayName   = "Studio Mic In";
        cfg.core          = CoreGeometry::jensenJT115KE();
        cfg.windings      = WindingConfig::jensenJT115KE();
        cfg.material      = JAParameterSet::defaultMuMetal();
        cfg.loadImpedance = 150000.0f;
        // K_geo = 736 [m] (fitted): mu-metal 1:10, produces Lm ~10 H at linear chi
        // → fc = Rs/(2π·Lm) ≈ 2.7 Hz, consistent with Jensen Lp ≈ 10 H datasheet
        cfg.geometry.K_geo = 736.0f;
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
    // Rdc=40Ω/winding, Cw=22nF, BW=0.18Hz-15MHz, THD=0.028%@20Hz/+4dBu
    // Drives 600Ω loads to +24dBu @20Hz. Insertion loss: -1.1dB.
    static TransformerConfig Jensen_JT11ELCF()
    {
        TransformerConfig cfg;
        cfg.name          = "Jensen JT-11ELCF";
        cfg.displayName   = "Master Bus";
        cfg.core          = CoreGeometry::jensenJT11ELCF();
        cfg.windings      = WindingConfig::jensenJT11ELCF();
        cfg.material      = JAParameterSet::output50NiFe();
        cfg.loadImpedance = 10000.0f;   // 10k bridging load (modern studio standard)
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

    // ── Factory: Harrison Console Mic Pre ────────────────────────────────
    // Uses Jensen JT-115K-E in Harrison 32-series console configuration.
    // Same electrical characteristics, different application context:
    // mic input with 6.8K balanced termination and U20 op-amp gain stage.
    static TransformerConfig Harrison_Console()
    {
        auto cfg = Jensen_JT115KE();
        cfg.name        = "Harrison Console";
        cfg.displayName = "Harrison Mic In";
        return cfg;
    }

    // ── Factory: Neve 10468 Input (Marinair T1444 / Sowter 9145) ─────────
    // Mic input. FR ±0.3 dB 20-20kHz. THD <0.1%@40Hz, <0.01% 500Hz-10kHz.
    // Max level: +10 dBu @ 40 Hz. Impédances: 1200/300 : 5000/1250.
    static TransformerConfig Neve_10468_Input()
    {
        TransformerConfig cfg;
        cfg.name        = "Neve 10468 Input";
        cfg.displayName = "Vintage Warm";
        cfg.core        = CoreGeometry::jensenJT115KE();
        cfg.windings.turnsRatio_N1 = 1;
        cfg.windings.turnsRatio_N2 = 2;
        cfg.windings.Rdc_primary   = 30.0f;
        cfg.windings.Rdc_secondary = 190.0f;
        cfg.windings.sourceImpedance = 300.0f;
        cfg.windings.hasFaradayShield = true;
        cfg.material      = JAParameterSet::defaultNiFe50();
        cfg.loadImpedance = 1200.0f;
        cfg.geometry.K_geo = 80.0f;
        cfg.lcParams.Lleak = 3.0e-3f;
        cfg.lcParams.Cw    = 80e-12f;
        cfg.lcParams.Cp_s  = 15e-12f;
        cfg.lcParams.Rz    = 3300.0f;
        cfg.lcParams.Cz    = 330e-12f;
        return cfg;
    }

    // ── Factory: Neve LI1166 Output ──────────────────────────────────────
    static TransformerConfig Neve_LI1166_Output()
    {
        TransformerConfig cfg;
        cfg.name        = "Neve LI1166 Output";
        cfg.displayName = "Heritage Output";
        cfg.core        = CoreGeometry::jensenJT11ELCF();
        cfg.windings.turnsRatio_N1 = 1;
        cfg.windings.turnsRatio_N2 = 1;
        cfg.windings.Rdc_primary   = 40.0f;
        cfg.windings.Rdc_secondary = 40.0f;
        cfg.windings.sourceImpedance = 150.0f;
        cfg.material      = JAParameterSet::defaultNiFe50();
        cfg.loadImpedance = 600.0f;
        cfg.geometry.K_geo = 2000.0f;
        cfg.lcParams.Lleak = 1.0e-3f;
        cfg.lcParams.Cw    = 15e-9f;
        cfg.lcParams.Cp_s  = 30e-12f;
        return cfg;
    }

    // ── Factory: Clean DI ────────────────────────────────────────────────
    // Very high Lm, transparent, minimal THD.
    static TransformerConfig Clean_DI()
    {
        TransformerConfig cfg;
        cfg.name        = "Clean DI";
        cfg.displayName = "Transparent";
        cfg.core        = CoreGeometry::jensenJT11ELCF();
        cfg.windings.turnsRatio_N1 = 1;
        cfg.windings.turnsRatio_N2 = 1;
        cfg.windings.Rdc_primary   = 10.0f;
        cfg.windings.Rdc_secondary = 10.0f;
        cfg.windings.sourceImpedance = 150.0f;
        cfg.material    = JAParameterSet::defaultMuMetal();
        cfg.material.a  = 50.0f;   // Wide linear region
        cfg.loadImpedance = 10000.0f;
        cfg.geometry.K_geo = 200.0f;  // Very high Lm
        cfg.lcParams.Lleak = 0.1e-3f;
        cfg.lcParams.Cw    = 10e-12f;
        return cfg;
    }

    // ── Musical Context Presets (A1.2 — differentiated circuit context) ──
    static TransformerConfig Vocal_Warmth()
    {
        // Neve 10468 optimized for vocal bus: lower source Z (mic preamp output),
        // higher K_geo for extended LF body, tighter Zobel for smooth HF
        auto cfg = Neve_10468_Input();
        cfg.name = "Vocal Warmth";
        cfg.displayName = "Vocal Warmth";
        cfg.windings.sourceImpedance = 150.0f;  // Low-Z mic preamp output (vs 300)
        cfg.geometry.K_geo = 120.0f;            // Higher Lm → warmer LF body (vs 80)
        cfg.lcParams.Rz = 2200.0f;              // Tighter Zobel → smoother HF (vs 3300)
        cfg.lcParams.Cz = 470e-12f;             // Matched Zobel cap (vs 330pF)
        return cfg;
    }

    static TransformerConfig Bass_Thickener()
    {
        // Jensen JT-115K-E optimized for bass: sharper B-H knee for more harmonics,
        // higher K_geo for stronger Lm-bass interaction, looser LC for resonance
        auto cfg = Jensen_JT115KE();
        cfg.name = "Bass Thickener";
        cfg.displayName = "Bass Thickener";
        cfg.material.a = 20.0f;                 // Sharper B-H knee → more saturation (vs 30)
        cfg.geometry.K_geo = 80.0f;             // More Lm interaction (vs 50)
        cfg.lcParams.Rz = 10000.0f;             // Looser Zobel → more LC resonance (vs 4700)
        cfg.loadImpedance = 47000.0f;           // Higher load → more bass response (vs 150k)
        return cfg;
    }

    static TransformerConfig Master_Glue()
    {
        // Jensen JT-11ELCF optimized for mastering: higher Lm for transparency,
        // gentler saturation, minimal leakage for clean HF
        auto cfg = Jensen_JT11ELCF();
        cfg.name = "Master Glue";
        cfg.displayName = "Master Glue";
        cfg.material.a = 70.0f;                 // Wider linear region → gentle saturation (vs 55)
        cfg.geometry.K_geo = 8000.0f;           // Very high Lm → minimal LF rolloff (vs 5300)
        cfg.lcParams.Lleak = 1.0e-6f;           // Ultra-low leakage → transparent HF (vs 2uH)
        cfg.loadImpedance = 10000.0f;           // Higher load → less insertion loss (vs 600)
        return cfg;
    }
};

} // namespace transfo
