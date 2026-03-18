#pragma once

// =============================================================================
// PreampConfig — Complete configuration for the dual-topology preamp model.
//
// Aggregates all sub-configurations:
//   - InputStageConfig:  phantom, pad, T1 ratio, termination
//   - NevePathConfig:    Chemin A transistors + bias components
//   - JE990PathConfig:   Chemin B transistors + inductors + compensation
//   - Output T2 config:  JT-11ELCF transformer
//   - Gain reference:    Rg = 47 Ohm
//
// Factory method DualTopology() creates the default configuration matching
// ANALYSE_ET_DESIGN_Rev2.md Annexe B (Dual Topology B+C).
//
// Pattern: Value Object (immutable after construction) + Factory Method.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md Rev 2.0
// =============================================================================

#include "BJTParams.h"
#include "TransformerConfig.h"
#include <string>

namespace transfo {

// ── Input Stage ─────────────────────────────────────────────────────────────
struct InputStageConfig
{
    bool  phantomEnabled   = true;
    float R_phantom        = 6810.0f;     // [Ohm] 0.1% precision, x2 (one per leg)
    bool  padEnabled       = false;
    float R_pad            = 649.0f;      // [Ohm] SSL-spec pad (x2 for balanced)
    float R_termination    = 13700.0f;    // [Ohm] Jensen/SSL termination
    float C_termination    = 680e-12f;    // [F]   HF resonance damping (100pF SSL alt.)

    enum class Ratio { X5, X10 };
    Ratio ratio = Ratio::X10;

    TransformerConfig t1Config;           // JT-115K-E input transformer

    /// Transformer voltage gain in dB for the selected ratio.
    float ratioGainDB() const
    {
        return (ratio == Ratio::X10) ? 20.0f : 14.0f;
    }

    bool isValid() const
    {
        return R_phantom > 0.0f && R_pad > 0.0f
            && R_termination > 0.0f && C_termination > 0.0f;
    }
};

// ── Chemin A: Neve Heritage ─────────────────────────────────────────────────
struct NevePathConfig
{
    BJTParams q1 = BJTParams::BC184C();   // CE stage 1 (NPN)
    BJTParams q2 = BJTParams::BC214C();   // CE stage 2 (PNP)
    BJTParams q3 = BJTParams::BD139();    // Emitter follower output (NPN)

    // Bias and passive components (from schematic section 3.2)
    float R_bias_base_q1   = 100000.0f;  // [Ohm] R6A + R7A voltage divider (100K each)
    float R_collector_q1   = 15000.0f;   // [Ohm] Q1 collector load
    float R_emitter_q2     = 7500.0f;    // [Ohm] R11 Q2 emitter to +24V
    float R_collector_q2   = 6800.0f;    // [Ohm] R12 Q2 collector to -24V
    float R_bias_q3        = 390.0f;     // [Ohm] Q3 EF bias resistor
    float C_miller         = 100e-12f;   // [F]   Miller compensation
    float C_input          = 100e-6f;    // [F]   Input coupling C3A
    float C_emitter_bypass = 470e-6f;    // [F]   C6 emitter bypass (gain node)
    float C_out            = 220e-6f;    // [F]   Output coupling (+ 4.7uF film)
    float C_out_film       = 4.7e-6f;    // [F]   Film cap parallel to C_out
    float R_series_out     = 10.0f;      // [Ohm] Series R before T2
    float Vcc              = 24.0f;      // [V]   Supply voltage (+/- 24V)

    bool isValid() const
    {
        return q1.isValid() && q2.isValid() && q3.isValid()
            && R_bias_q3 > 0.0f && C_miller > 0.0f
            && C_out > 0.0f && Vcc > 0.0f;
    }
};

// ── Chemin B: JE-990 DIY ────────────────────────────────────────────────────
struct JE990PathConfig
{
    // Transistors (per original Deane Jensen 1980 schematic)
    BJTParams q1q2        = BJTParams::LM394();      // Matched diff pair (NPN)
    BJTParams q3_cascode  = BJTParams::N2N4250A();    // PNP cascode
    BJTParams q4_tail     = BJTParams::N2N2484();     // NPN tail current source
    BJTParams q5_cascode  = BJTParams::N2N4250A();    // PNP cascode mirror
    BJTParams q6_vas      = BJTParams::N2N4250A();    // PNP VAS driver
    BJTParams q7_predriver = BJTParams::N2N2484();    // NPN pre-driver
    BJTParams q8_top      = BJTParams::MJE181();      // NPN output top
    BJTParams q9_bottom   = BJTParams::MJE171();      // PNP output bottom

    // Jensen-exclusive inductors (HF linearization, patent technique)
    float L1               = 20e-6f;     // [H] Emitter inductor Q1
    float L2               = 20e-6f;     // [H] Emitter inductor Q2
    float L3               = 40e-6f;     // [H] Output load isolator

    // Load isolator resistor (Allen Bradley 39 Ohm)
    float R_load_isolator  = 39.0f;      // [Ohm]

    // Compensation network
    float C1_miller        = 150e-12f;   // [F] Main Miller compensation
    float C2_comp          = 62e-12f;    // [F] Output comp top
    float C3_comp          = 91e-12f;    // [F] Output comp bottom

    // Output coupling
    float C_out            = 220e-6f;    // [F] Output DC blocking

    // Supply
    float Vcc              = 24.0f;      // [V] (+/- 24V)

    bool isValid() const
    {
        return q1q2.isValid() && q3_cascode.isValid() && q4_tail.isValid()
            && q6_vas.isValid() && q8_top.isValid() && q9_bottom.isValid()
            && L1 > 0.0f && L2 > 0.0f && L3 > 0.0f
            && R_load_isolator > 0.0f
            && C1_miller > 0.0f && C_out > 0.0f && Vcc > 0.0f;
    }
};

// ── Complete Preamp Configuration ───────────────────────────────────────────
struct PreampConfig
{
    std::string       name;
    InputStageConfig  input;
    NevePathConfig    neveConfig;
    JE990PathConfig   je990Config;
    TransformerConfig t2Config;           // JT-11ELCF output transformer
    float             Rg = 47.0f;        // [Ohm] Gain reference resistor

    bool isValid() const
    {
        return input.isValid()
            && neveConfig.isValid()
            && je990Config.isValid()
            && Rg > 0.0f;
    }

    // ── Factory: Default Dual Topology (Annexe B) ───────────────────────────
    /// Creates the complete dual-topology preamp configuration
    /// matching ANALYSE_ET_DESIGN_Rev2.md Annexe B.
    static PreampConfig DualTopology()
    {
        PreampConfig cfg;
        cfg.name = "Dual Topology Neve/Jensen";
        cfg.input.t1Config  = TransformerConfig::Jensen_JT115KE();
        cfg.t2Config        = TransformerConfig::Jensen_JT11ELCF();
        // All other fields use struct defaults (already set from design doc)
        return cfg;
    }
};

} // namespace transfo
