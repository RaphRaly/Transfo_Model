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
        sign_ = static_cast<double>(params.polaritySign());
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
    double solve(float a, float Z)
    {
        const double Is = static_cast<double>(params_.Is);
        const double Bf = static_cast<double>(params_.Bf);
        const double Vt = static_cast<double>(params_.Vt);
        const double s  = sign_;
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

        Vbe_prev_ = Vbe;
        lastIterCount_ = iter;

        // Update operating point (clamp must match NR loop to avoid exp overflow)
        const double sVbe = sign_ * Vbe_prev_;
        const double expVbe = std::exp(std::clamp(sVbe, -2.0, 1.5)
                                       / static_cast<double>(params_.Vt));
        Ib_last_ = sign_ * Is / Bf * (expVbe - 1.0);
        Ic_last_ = static_cast<double>(params_.Bf) * Ib_last_;
        gm_ = std::abs(Ic_last_) / static_cast<double>(params_.Vt);

        return Vbe_prev_;
    }

    // ── Companion circuit outputs ───────────────────────────────────────────

    /// Small-signal resistance at the BE junction (linearized).
    /// rbe = Bf * Vt / Ic = Vt / Ib
    double getCompanionResistance() const
    {
        const double Ib_abs = std::abs(Ib_last_) + 1e-15;
        return std::clamp(static_cast<double>(params_.Vt) / Ib_abs, 1.0, 1e8);
    }

    /// Collector current at the current operating point [A].
    double getCollectorCurrent() const { return Ic_last_; }

    /// Base current at the current operating point [A].
    double getBaseCurrent() const { return Ib_last_; }

    /// Transconductance gm = |Ic| / Vt [S]
    double getTransconductance() const { return gm_; }

    /// Last converged Vbe [V]
    double getVbe() const { return Vbe_prev_; }

    /// Number of NR iterations in the last solve() call.
    int getLastIterCount() const { return lastIterCount_; }

    // ── Early effect (output impedance) ─────────────────────────────────────

    /// Small-signal output resistance rce = Vaf / |Ic| [Ohm]
    double getOutputResistance() const
    {
        return static_cast<double>(params_.Vaf) / (std::abs(Ic_last_) + 1e-15);
    }

    void reset()
    {
        Vbe_prev_ = sign_ > 0.0 ? 0.6 : -0.6;  // Typical Vbe for forward-active
        Ic_last_  = 0.0;
        Ib_last_  = 0.0;
        gm_       = 0.0;
    }

    /// Restore AC-fast state (for Newton probe snapshot/restore).
    void restoreState(double Vbe, double Ic, double Ib, double gm, int iter)
    {
        Vbe_prev_      = Vbe;
        Ic_last_       = Ic;
        Ib_last_       = Ib;
        gm_            = gm;
        lastIterCount_ = iter;
    }

private:
    BJTParams params_;
    double sign_          = 1.0;    // +1 NPN, -1 PNP
    double Vbe_prev_      = 0.6;   // NR warm-start
    double Ic_last_       = 0.0;
    double Ib_last_       = 0.0;
    double gm_            = 0.0;
    int    lastIterCount_ = 0;
};

} // namespace transfo
