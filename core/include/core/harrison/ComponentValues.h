#pragma once

// =============================================================================
// ComponentValues — Harrison Console Mic Preamp component values.
//
// All values verified against original schematic photos (8 zooms).
// Each value cross-referenced on at least 2 images.
//
// Reference: Harrison console mic preamp, Jensen JE-115-K-E transformer.
// =============================================================================

namespace Harrison {
namespace MicPre {

    // ---- BALANCED TERMINATION ----
    constexpr float R100 = 6800.0f;         // 6.8K 1% -- HI -> GND
    constexpr float R101 = 6800.0f;         // 6.8K 1% -- LO -> GND

    // ---- 20dB PAD (T-pad balanced, primary side) ----
    constexpr float R102 = 680.0f;          // Series leg HI
    constexpr float R103 = 680.0f;          // Series leg LO
    constexpr float R104 = 160.0f;          // Shunt between legs
    // beta = (R104/2) / (R102 + R104/2) = 80/760 = 0.10526
    constexpr float PAD_ATTENUATION = 0.10526f;

    // ---- PHANTOM POWER DECOUPLING (out of signal path) ----
    constexpr float C101 = 10.0e-9f;        // 0.01 uF

    // ---- U20 GAIN STAGE ----
    constexpr float R105 = 150000.0f;       // 150K -- N_H -> Pin 2 (DC bias, 0A ideal)
    constexpr float R106 = 100.0f;          // 100R -- Pin 6 -> N_MIN (Rf in non-inv)
    constexpr float R107 = 1000000.0f;      // 1M   -- Pin 2 -> N_MIN (Rm, DC path)
    constexpr float C102 = 100.0e-12f;      // 100 pF -- Pin 6 -> Pin 2 (feedback comp)
    constexpr float C116 = 10.0e-12f;       // 10 pF  -- Pin 6 -> Pin 3 (feedforward)

    // ---- MIC GAIN NETWORK ----
    constexpr float R109 = 25000.0f;        // 25K pot -- N_MIN(MIN) -> N_MAX(MAX)
    constexpr float R108 = 220.0f;          // 220R -- N_MAX -> N_R108
    constexpr float C103 = 100.0e-6f;       // 100 uF electrolytic -- N_R108(+) -> GND
    constexpr float C104 = 100.0e-6f;       // 100 uF electrolytic -- N_R108(+) -> GND
    constexpr float C_BLOCK = C103 + C104;  // 200 uF total (parallel)
    constexpr float R110 = 36000.0f;        // 36K -- N_R108 -> +18V
    constexpr float R111 = 75000.0f;        // 75K -- N_R108 -> +18V
    constexpr float R_BIAS = (R110 * R111) / (R110 + R111); // ~24324 Ohm

    // ---- CHARACTERISTIC FREQUENCIES ----
    // f_DC_block = 1/(2*pi*R_BIAS*C_BLOCK) ~ 0.033 Hz
    // f_HF_comp  = 1/(2*pi*R107*C102)      ~ 1.59 kHz

    // ---- SUPPLY RAILS ----
    constexpr float V_PLUS  =  18.0f;
    constexpr float V_MINUS = -18.0f;

    // ---- DERIVED CONSTANTS (used by OpAmpGainStage) ----

    // Rf = R106 (non-inverting feedback resistor)
    constexpr float Rf = R106;
    // Rm = R107 (DC feedback / million-ohm path)
    constexpr float Rm = R107;
    // Cc = C102 (HF compensation cap)
    constexpr float Cc = C102;
    // C  = C_BLOCK (DC blocking caps in shunt)
    constexpr float C  = C_BLOCK;
    // G  = 1/Rf + 1/Rm (total conductance)
    constexpr float G  = 1.0f / Rf + 1.0f / Rm;  // ~0.010001

    // Pre-warp frequency: HF pole at 1/(2*pi*Rm*Cc)
    constexpr float F_PREWARP = 1.0f / (6.2831853f * Rm * Cc); // ~1591 Hz

} // namespace MicPre
} // namespace Harrison
