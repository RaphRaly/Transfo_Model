#pragma once

// =============================================================================
// OpAmpGainStage — Digital IIR implementation of the Harrison U20 gain stage.
//
// Implements H(s, alpha) as a second-order biquad via bilinear transform
// with pre-warping at the HF compensation pole (~1.59 kHz).
//
// Transfer function (verified algebraically, KCL at Pin2 and N_MIN):
//
//   H(s) = (A*s^2 + Bn*s + D) / (A*s^2 + Bd*s + D)
//
//   where:
//     Rg = alpha*R109 + R108           (variable shunt resistance)
//     G  = 1/Rf + 1/Rm                 (sum of conductances)
//     K  = G*Rg + 1                    (combination factor)
//     A  = Cc * C * K                  (s^2 coefficient, same num & den)
//     Bn = Cc*G + C*(Rg + Rf)/(Rm*Rf)  (s^1 numerator)
//     Bd = Cc*G + C*Rg/(Rf*Rm)         (s^1 denominator)
//     D  = 1/(Rf*Rm)                   (s^0 coefficient, same num & den)
//
// Key properties:
//   - H(0)   = 1  (DC blocked by C_BLOCK)
//   - H(inf) = 1  (C102 shorts feedback at HF)
//   - Bn - Bd = C/Rm (constant, independent of alpha)
//   - True mid-band gain (f << 1591 Hz) = 1 + Rf/Rg
//   - b1_z == a1_z (z^-1 coeff identical in num & den)
//
// Precision: coefficients computed in double, state accumulated in double,
// I/O in float. This avoids catastrophic cancellation near z=1 (DC pole
// at 0.033 Hz) and ensures stability at all sample rates up to 192 kHz.
// =============================================================================

#include "ComponentValues.h"
#include <cmath>
#include <algorithm>

namespace Harrison {
namespace MicPre {

class OpAmpGainStage
{
public:
    OpAmpGainStage() = default;

    /// Call once in prepareToPlay. Computes the pre-warp constant K_blt.
    void prepare(float sampleRate)
    {
        sampleRate_ = sampleRate;

        // Pre-warp constant for bilinear transform (computed in double)
        // K = omega0 / tan(omega0 * T / 2)
        const double omega0 = 6.283185307179586 * static_cast<double>(F_PREWARP);
        const double halfOmegaT = omega0 / (2.0 * static_cast<double>(sampleRate));
        K_blt_ = omega0 / std::tan(halfOmegaT);
        K2_ = K_blt_ * K_blt_;

        // Force coefficient recalculation on next process
        lastAlpha_ = -1.0f;

        reset();
    }

    /// Reset filter state (call on parameter jump or silence).
    void reset()
    {
        s1_ = 0.0;
        s2_ = 0.0;
    }

    /// Recalculate biquad coefficients for a given pot position alpha in [0, 1].
    ///
    /// alpha = 0 -> wiper at MIN -> Rg = R108 -> max gain (~1.4545 at f<<1591Hz)
    /// alpha = 1 -> wiper at MAX -> Rg = R109 + R108 -> min gain (~1.004)
    ///
    /// All intermediate arithmetic in double to avoid float32 cancellation.
    void updateCoefficients(float alpha)
    {
        lastAlpha_ = alpha;

        // Promote component values to double
        const double dRf  = static_cast<double>(Rf);
        const double dRm  = static_cast<double>(Rm);
        const double dCc  = static_cast<double>(Cc);
        const double dC   = static_cast<double>(C);
        const double dR109 = static_cast<double>(R109);
        const double dR108 = static_cast<double>(R108);

        // Variable shunt resistance
        const double Rg = static_cast<double>(alpha) * dR109 + dR108;

        // Sum of conductances
        const double dG = 1.0 / dRf + 1.0 / dRm;

        // s-domain coefficients
        const double K_comb = dG * Rg + 1.0;
        const double A_s  = dCc * dC * K_comb;
        const double Bn_s = dCc * dG + dC * (Rg + dRf) / (dRm * dRf);
        const double Bd_s = dCc * dG + dC * Rg / (dRf * dRm);
        const double D_s  = 1.0 / (dRf * dRm);

        // Bilinear transform: s = K * (1 - z^-1) / (1 + z^-1)
        //
        //   Numerator z-coefficients:
        //     c0 = A*K^2 + Bn*K + D
        //     c1 = -2*A*K^2 + 2*D
        //     c2 = A*K^2 - Bn*K + D
        //
        //   Denominator z-coefficients:
        //     d0 = A*K^2 + Bd*K + D
        //     d1 = c1              (proven: shared s^2 and s^0)
        //     d2 = A*K^2 - Bd*K + D

        const double AK2 = A_s * K2_;

        const double c0 = AK2 + Bn_s * K_blt_ + D_s;
        const double c1 = -2.0 * AK2 + 2.0 * D_s;
        const double c2 = AK2 - Bn_s * K_blt_ + D_s;

        const double d0 = AK2 + Bd_s * K_blt_ + D_s;
        const double d2 = AK2 - Bd_s * K_blt_ + D_s;

        // Normalize by d0
        const double inv_d0 = 1.0 / d0;
        b0_ = c0 * inv_d0;
        b1_ = c1 * inv_d0;
        b2_ = c2 * inv_d0;
        a1_ = c1 * inv_d0;  // = b1_ (d1 == c1)
        a2_ = d2 * inv_d0;
    }

    /// Process a single sample through the biquad (Direct Form II Transposed).
    /// State variables kept in double for numerical robustness under
    /// time-varying coefficients.
    float processSample(float x)
    {
        const double xd = static_cast<double>(x);
        const double y = b0_ * xd + s1_;
        s1_ = b1_ * xd - a1_ * y + s2_;
        s2_ = b2_ * xd - a2_ * y;
        return static_cast<float>(y);
    }

    /// Get the asymptotic mid-band gain (f << f_pole) for display purposes.
    /// G_mid = 1 + Rf / (alpha*R109 + R108)
    float getMidBandGain(float alpha) const
    {
        const float Rg = alpha * R109 + R108;
        return 1.0f + Rf / Rg;
    }

    /// Check if coefficients need recalculation (threshold: 1e-6).
    bool needsUpdate(float alpha) const
    {
        return std::abs(alpha - lastAlpha_) > 1e-6f;
    }

    // Accessors for verification / testing (return double for precision)
    double getB0() const { return b0_; }
    double getB1() const { return b1_; }
    double getB2() const { return b2_; }
    double getA1() const { return a1_; }
    double getA2() const { return a2_; }

private:
    float sampleRate_ = 44100.0f;

    // Bilinear pre-warp constants (double)
    double K_blt_ = 0.0;
    double K2_    = 0.0;

    // Biquad coefficients (double, normalized: a0 = 1)
    double b0_ = 1.0;
    double b1_ = 0.0;
    double b2_ = 0.0;
    double a1_ = 0.0;
    double a2_ = 0.0;

    // Direct Form II Transposed state (double for accumulator precision)
    double s1_ = 0.0;
    double s2_ = 0.0;

    float lastAlpha_ = -1.0f;  // force initial coefficient computation
};

} // namespace MicPre
} // namespace Harrison
