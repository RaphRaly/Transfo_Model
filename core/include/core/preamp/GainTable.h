#pragma once

// =============================================================================
// GainTable — 11-position stepped gain control for the dual-topology preamp.
//
// Models the Grayhill 71BD 2-deck rotary switch with 11 positions.
// Each position selects a feedback resistor Rfb. Gain = 1 + Rfb / Rg.
//
// Rfb values from ANALYSE_ET_DESIGN_Rev2.md gain table (E96 1% series):
//
//   Position | Rfb [Ohm]  | Gain ampli | + T1 1:10 | Total
//   ---------|------------|------------|-----------|------
//   1        | 100        | +10 dB     | +20 dB    | +29 dB
//   2        | 187        | +14 dB     | +20 dB    | +33 dB
//   3        | 324        | +18 dB     | +20 dB    | +37 dB
//   4        | 536        | +22 dB     | +20 dB    | +41 dB
//   5        | 887        | +26 dB     | +20 dB    | +45 dB
//   6        | 1430       | +30 dB     | +20 dB    | +49 dB
//   7        | 2320       | +34 dB     | +20 dB    | +53 dB
//   8        | 3650       | +38 dB     | +20 dB    | +57 dB
//   9        | 5900       | +42 dB     | +20 dB    | +61 dB
//   10       | 9310       | +46 dB     | +20 dB    | +65 dB
//   11       | 14700      | +50 dB     | +20 dB    | +69 dB
//
// Note: With T1 in 1:5 mode, subtract 6 dB from Total column.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md Annexe B, gain table
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>

namespace transfo {

struct GainTable
{
    static constexpr int kNumPositions = 11;
    static constexpr float kRg = 47.0f;  // Reference resistor [Ohm]

    // ── Rfb values (E96 1% series, from design doc) ─────────────────────────
    static constexpr std::array<float, kNumPositions> kRfb = {{
        100.0f,    // Position 1:  +10 dB
        187.0f,    // Position 2:  +14 dB
        324.0f,    // Position 3:  +18 dB
        536.0f,    // Position 4:  +22 dB
        887.0f,    // Position 5:  +26 dB
        1430.0f,   // Position 6:  +30 dB
        2320.0f,   // Position 7:  +34 dB
        3650.0f,   // Position 8:  +38 dB
        5900.0f,   // Position 9:  +42 dB
        9310.0f,   // Position 10: +46 dB
        14700.0f   // Position 11: +50 dB
    }};

    // ── Gain computation (amplifier stage only, excludes T1 gain) ───────────

    /// Linear voltage gain at a given position (0-based index).
    /// Gain = 1 + Rfb / Rg (non-inverting op-amp / discrete equivalent)
    static float getGainLinear(int position)
    {
        return 1.0f + getRfb(position) / kRg;
    }

    /// Gain in dB at a given position (0-based index).
    static float getGainDB(int position)
    {
        return 20.0f * std::log10(getGainLinear(position));
    }

    /// Feedback resistor value at a given position (0-based index).
    static float getRfb(int position)
    {
        const int idx = std::clamp(position, 0, kNumPositions - 1);
        return kRfb[static_cast<size_t>(idx)];
    }

    /// Total gain including transformer T1 ratio.
    /// ratioGainDB: +20 dB for 1:10, +14 dB for 1:5
    static float getTotalGainDB(int position, float ratioGainDB = 20.0f)
    {
        return getGainDB(position) + ratioGainDB;
    }

    /// Find nearest gain position for a target amplifier gain in dB.
    /// Returns 0-based position index.
    static int findNearestPosition(float targetGainDB)
    {
        int best = 0;
        float bestDiff = 1e6f;
        for (int i = 0; i < kNumPositions; ++i) {
            float diff = std::abs(getGainDB(i) - targetGainDB);
            if (diff < bestDiff) {
                bestDiff = diff;
                best = i;
            }
        }
        return best;
    }
};

} // namespace transfo
