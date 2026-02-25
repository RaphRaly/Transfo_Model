#pragma once

// =============================================================================
// ToleranceModel — TMT (Tolerance Modeling Technology) for stereo imaging.
//
// [v3 NOUVEAU] Simulates component tolerance variations between L and R
// channels to create natural stereo width, inspired by the fact that real
// analog hardware has slightly different component values per unit.
//
// Applies small variations (+/-1-5%) to:
//   - Rdc primary and secondary
//   - Parasitic capacitances
//   - Leakage inductance
//
// These affect resonance frequencies, gain, and phase slightly differently
// per channel, creating the "analog stereo spread" effect.
//
// CPU cost: ~0 (applied once at prepareToPlay, not per sample)
//
// Reference: Brainworx Patent US 10,725,727
// =============================================================================

#include "TransformerConfig.h"
#include <cstdlib>
#include <cmath>

namespace transfo {

struct ToleranceOffset
{
    float dR_pri   = 0.0f;   // % variation on primary DC resistance
    float dR_sec   = 0.0f;   // % variation on secondary DC resistance
    float dC       = 0.0f;   // % variation on capacitances
    float dL_leak  = 0.0f;   // % variation on leakage inductance
};

class ToleranceModel
{
public:
    ToleranceModel() = default;

    // ─── Configure offsets for L and R channels ─────────────────────────────
    void setOffsets(const ToleranceOffset& left, const ToleranceOffset& right)
    {
        offsets_L_ = left;
        offsets_R_ = right;
    }

    // ─── Generate random realistic offsets ──────────────────────────────────
    // maxPercent: maximum deviation in percent (e.g., 3.0 for +/-3%)
    void generateRandomOffsets(float maxPercent, unsigned int seed = 42)
    {
        // Simple LCG pseudo-random for determinism
        auto nextRand = [&seed]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return (static_cast<float>(seed & 0xFFFF) / 32768.0f) - 1.0f; // [-1, 1]
        };

        float scale = maxPercent / 100.0f;

        offsets_L_.dR_pri  = nextRand() * scale;
        offsets_L_.dR_sec  = nextRand() * scale;
        offsets_L_.dC      = nextRand() * scale;
        offsets_L_.dL_leak = nextRand() * scale;

        offsets_R_.dR_pri  = nextRand() * scale;
        offsets_R_.dR_sec  = nextRand() * scale;
        offsets_R_.dC      = nextRand() * scale;
        offsets_R_.dL_leak = nextRand() * scale;
    }

    // ─── Apply tolerance to a config for a specific channel ─────────────────
    enum class Channel { Left, Right };

    TransformerConfig applyToConfig(const TransformerConfig& base, Channel channel) const
    {
        const auto& offset = (channel == Channel::Left) ? offsets_L_ : offsets_R_;

        TransformerConfig cfg = base;
        cfg.windings.Rdc_primary   *= (1.0f + offset.dR_pri);
        cfg.windings.Rdc_secondary *= (1.0f + offset.dR_sec);
        cfg.windings.C_sec_shield  *= (1.0f + offset.dC);
        cfg.windings.C_interwinding *= (1.0f + offset.dC);
        cfg.windings.L_leakage     *= (1.0f + offset.dL_leak);

        return cfg;
    }

    const ToleranceOffset& getLeftOffset()  const { return offsets_L_; }
    const ToleranceOffset& getRightOffset() const { return offsets_R_; }

private:
    ToleranceOffset offsets_L_;
    ToleranceOffset offsets_R_;
};

} // namespace transfo
