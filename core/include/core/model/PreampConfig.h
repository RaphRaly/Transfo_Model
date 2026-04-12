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

// ── [ARCHIVED] Chemin A: Neve Heritage (replaced by Neve1063PathConfig) ─────
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

// ── Chemin A: Neve 1063 (replaces old NevePathConfig) ───────────────────
struct Neve1063PathConfig
{
    // BA183 A-M (B110-type line amplifier, all NPN, Class-A)
    BJTParams am_tr1        = BJTParams::BC184C();  // CE input stage
    BJTParams am_tr2        = BJTParams::BC184C();  // CE driver stage
    BJTParams am_tr3        = BJTParams::BDY61();   // CE power output (~70mA)

    float am_Rc_tr1         = 10000.0f;   // [Ohm] TR1 collector load
    float am_Re_tr1         = 270.0f;     // [Ohm] TR1 emitter degen (K-J, default 30 dB)
    float am_Rc_tr2         = 4700.0f;    // [Ohm] TR2 collector load
    float am_Re_tr2         = 100.0f;     // [Ohm] TR2 emitter degen
    float am_Rc_tr3         = 330.0f;     // [Ohm] TR3 collector load (to +24V via LO1166)
    float am_Re_output      = 47.0f;      // [Ohm] R8 output emitter resistor
    float am_C4             = 22e-6f;     // [F]   C4 AC feedback cap (estimated)
    float am_Ic_q_tr3       = 70e-3f;     // [A]   TR3 quiescent current (~70mA)

    // BA183 N-V (B112-type variable gain preamp, all NPN)
    BJTParams nv_tr4        = BJTParams::BC109();   // CE input (low-noise)
    BJTParams nv_tr5        = BJTParams::BC107();   // Buffer (linearized)
    BJTParams nv_tr6        = BJTParams::BC107();   // EF output

    float nv_Rc_tr4         = 6800.0f;    // [Ohm] TR4 collector load
    float nv_R18            = 330.0f;     // [Ohm] TR4 emitter degen (T-V gain ctrl)
    float nv_R_fb_SU        = 18000.0f;   // [Ohm] 18K S->U input series resistor
    float nv_Rc_tr5         = 6800.0f;    // [Ohm] TR5 collector load (CE stage)
    float nv_Re_tr5         = 7500.0f;    // [Ohm] TR5 emitter degen (CE stage)

    // BA184 N-V (B112-type mic preamp, all NPN)
    BJTParams ba184_tr4      = BJTParams::BC109();   // CE input (low-noise)
    BJTParams ba184_tr5      = BJTParams::BC107();   // Buffer (linearized)
    BJTParams ba184_tr6      = BJTParams::BC107();   // EF output

    float ba184_Rc_tr4       = 6800.0f;    // [Ohm] TR4 collector load
    float ba184_R_emitter    = 330.0f;     // [Ohm] TR4 emitter degen
    float ba184_Rc_tr5       = 6800.0f;    // [Ohm] TR5 collector load (CE stage)
    float ba184_Re_tr5       = 7500.0f;    // [Ohm] TR5 emitter degen (CE stage)

    // Cross-feedback (Level 2: BA183 NV output → BA184 input)
    float R_cross_fb         = 18000.0f;   // [Ohm] Cross-feedback resistor (R1)

    // Global feedback
    float R_fb_global       = 39000.0f;   // [Ohm] 39K feedback (BA183 A-M out -> N-V in)
    float R_divider_out     = 390.0f;     // [Ohm] 270+120 output divider at EQU point

    // Supply
    float Vcc               = 24.0f;      // [V] +24V single rail

    bool isValid() const
    {
        return am_tr1.isValid() && am_tr2.isValid() && am_tr3.isValid()
            && nv_tr4.isValid() && nv_tr6.isValid()
            && ba184_tr4.isValid() && ba184_tr6.isValid()
            && am_Rc_tr1 > 0.0f && am_Rc_tr2 > 0.0f
            && nv_Rc_tr4 > 0.0f && nv_R18 > 0.0f
            && ba184_Rc_tr4 > 0.0f && R_cross_fb > 0.0f
            && R_fb_global > 0.0f && Vcc > 0.0f;
    }
};

// ── [ARCHIVED] Chemin B: JE-990 DIY (on hold, Sprint 4 tuning pending) ──────
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
    std::string          name;
    InputStageConfig     input;
    NevePathConfig       neveConfig;         // [ARCHIVED] Legacy 3-BJT heritage
    Neve1063PathConfig   neve1063Config;     // Neve 1063 Channel Amplifier (ACTIVE)
    JE990PathConfig      je990Config;        // [ARCHIVED] On hold, Sprint 4
    TransformerConfig    t2Config;           // JT-11ELCF output transformer
    float                Rg = 47.0f;        // [Ohm] Gain reference resistor

    bool isValid() const
    {
        return input.isValid()
            && neve1063Config.isValid()
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
        // Output transformer: use Physical calibration so hScale is derived
        // from Ampere's law (N/2πfLml_e ≈ 0.065) rather than Artistic mode
        // (a×5 = 275).  Artistic mode deliberately overdrives the core for
        // standalone transformer coloration, but in the preamp context the
        // Neve path output at max gain (~0.3V) would push T2 deep into
        // J-A saturation, causing a cyclic collapse at 20 Hz.
        cfg.t2Config.calibrationMode = CalibrationMode::Physical;
        // All other fields use struct defaults (already set from design doc)
        return cfg;
    }
};

} // namespace transfo
