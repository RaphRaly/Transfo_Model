#pragma once

// =============================================================================
// BJTCompanionModel — Newton-Raphson companion linearization for BJT in WDF.
//
// At each NR iteration, the nonlinear Ebers-Moll BJT is replaced by a
// linear companion circuit: a Thevenin source (V_eq, R_eq) at the BE port.
//
// Forward-Active Simplification:
//   In the forward-active region (Vbe > 0, Vbc < 0), the BC junction is
//   reverse-biased. The diode current Ibc ≈ -Is/Br (negligible).
//   The BJT reduces to:
//     - BE junction: nonlinear diode Ibe = Is*(exp(Vbe/Vt) - 1) / Bf
//     - Collector current: Ic = Bf * Ibe (current-controlled source)
//
//   This is the primary operating mode for audio preamp circuits where
//   transistors are biased in Class-A or Class-AB.
//
// The companion model provides:
//   1. solve() — NR iteration for Vbe given incident wave a
//   2. getCompanionResistance() — linearized small-signal R at operating point
//   3. getCollectorCurrent() — output current from the companion source
//   4. getTransconductance() — gm = Ic / Vt
//
// Reference: Lauritzen "Compact Models for BJT" IEEE 1999;
//            Najm SPICE BJT model; Werner WDF thesis ch.5
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include <algorithm>
#include <cmath>

namespace transfo {

class BJTCompanionModel
{
public:
    BJTCompanionModel() = default;

    void configure(const BJTParams& params)
    {
        params_ = params;
        sign_ = params.polaritySign();
    }

    /// NR iteration: solve for Vbe given WDF incident wave a and port impedance Z.
    ///
    /// For the BE junction in wave domain:
    ///   V_be = (a + b) / 2
    ///   I_be = (a - b) / (2Z)
    ///   I_be = (Is / Bf) * (exp(sign * Vbe / Vt) - 1)
    ///
    /// f(Vbe) = Vbe - a + Z * sign * (Is/Bf) * (exp(sign*Vbe/Vt) - 1) = 0
    /// f'(Vbe) = 1 + Z * (Is/Bf) / Vt * exp(sign*Vbe/Vt)  [sign² = 1]
    ///
    /// For PNP: sign = -1, so Vbe is negative in forward-active.
    ///
    /// Returns the converged Vbe.
    float solve(float a, float Z)
    {
        const double Is = static_cast<double>(params_.Is);
        const double Bf = static_cast<double>(params_.Bf);
        const double Vt = static_cast<double>(params_.Vt);
        const double s  = static_cast<double>(sign_);
        const double Zd = static_cast<double>(Z);
        const double a_d = static_cast<double>(a);

        // Is_eff = Is / Bf (base current coefficient)
        const double Is_eff = Is / Bf;

        // Warm-start
        double Vbe = static_cast<double>(Vbe_prev_);
        Vbe = std::clamp(Vbe, -2.0, 1.5);

        int iter = 0;
        for (; iter < kMaxNRIter; ++iter) {
            // Clamp for safety
            const double sVbe = s * std::clamp(Vbe, -2.0, 1.5);
            const double expTerm = std::exp(sVbe / Vt);

            // f(Vbe) = Vbe - a + Z * s * Is_eff * (exp(s*Vbe/Vt) - 1)
            // The sign factor accounts for PNP current direction reversal.
            const double f = Vbe - a_d + Zd * s * Is_eff * (expTerm - 1.0);

            // f'(Vbe) = 1 + Z * Is_eff / Vt * exp(s*Vbe/Vt)  [s² = 1]
            const double fp = 1.0 + Zd * Is_eff / Vt * expTerm;

            if (std::abs(fp) < 1e-30)
                break;

            double dV = f / fp;

            // Damping
            const double maxStep = 5.0 * Vt;
            dV = std::clamp(dV, -maxStep, maxStep);

            Vbe -= dV;

            if (std::abs(dV) < 1e-6 * Vt) {
                ++iter;
                break;
            }
        }

        Vbe_prev_ = static_cast<float>(Vbe);
        lastIterCount_ = iter;

        // Update operating point
        const double sVbe = sign_ * Vbe_prev_;
        const double expVbe = std::exp(std::clamp(sVbe, -2.0, 35.0)
                                       / static_cast<double>(params_.Vt));
        Ib_last_ = static_cast<float>(sign_ * Is / Bf * (expVbe - 1.0));
        Ic_last_ = static_cast<float>(params_.Bf * Ib_last_);
        gm_ = std::abs(Ic_last_) / params_.Vt;

        return Vbe_prev_;
    }

    // ── Companion circuit outputs ───────────────────────────────────────────

    /// Small-signal resistance at the BE junction (linearized).
    /// rbe = Bf * Vt / Ic = Vt / Ib
    float getCompanionResistance() const
    {
        const float Ib_abs = std::abs(Ib_last_) + 1e-15f;
        return std::clamp(params_.Vt / Ib_abs, 1.0f, 1e8f);
    }

    /// Collector current at the current operating point [A].
    float getCollectorCurrent() const { return Ic_last_; }

    /// Base current at the current operating point [A].
    float getBaseCurrent() const { return Ib_last_; }

    /// Transconductance gm = |Ic| / Vt [S]
    float getTransconductance() const { return gm_; }

    /// Last converged Vbe [V]
    float getVbe() const { return Vbe_prev_; }

    /// Number of NR iterations in the last solve() call.
    int getLastIterCount() const { return lastIterCount_; }

    // ── Early effect (output impedance) ─────────────────────────────────────

    /// Small-signal output resistance rce = Vaf / |Ic| [Ohm]
    float getOutputResistance() const
    {
        return params_.Vaf / (std::abs(Ic_last_) + 1e-15f);
    }

    void reset()
    {
        Vbe_prev_ = sign_ > 0 ? 0.6f : -0.6f;  // Typical Vbe for forward-active
        Ic_last_  = 0.0f;
        Ib_last_  = 0.0f;
        gm_       = 0.0f;
    }

private:
    BJTParams params_;
    float sign_          = 1.0f;    // +1 NPN, -1 PNP
    float Vbe_prev_      = 0.6f;   // NR warm-start
    float Ic_last_       = 0.0f;
    float Ib_last_       = 0.0f;
    float gm_            = 0.0f;
    int   lastIterCount_ = 0;
};

} // namespace transfo
