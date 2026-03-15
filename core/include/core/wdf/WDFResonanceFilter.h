#pragma once

// =============================================================================
// WDFResonanceFilter -- Bilinear-transformed LC parasitic resonance filter
// for audio transformer modeling.
//
// Models the interaction between leakage inductance and total winding/cable
// capacitance that produces a high-frequency resonance in real transformers.
//
// Physical circuit:
//
//   Vin --[Rs]--[Lleak]--+--[R_load]-- Vout
//                        |      |
//                     [Ctotal] [Zobel: Rz+Cz series]  (optional)
//                        |      |
//                       GND    GND
//
// Transfer function (without Zobel):
//   H(s) = R_load / (L*C*R_load*s^2 + (Rs*C*R_load + L)*s + (Rs + R_load))
//
// With Zobel: H(s) is 3rd order (3 energy-storage elements: L, C, Cz).
//   Numerator:   R_load * (1 + s*Rz*Cz)
//   Denominator: 3rd-order polynomial in s
//
// Discretization: bilinear transform s = (2/T)*(z-1)/(z+1), ensuring
// passivity and correct DC gain H(0) = R_load / (Rs + R_load).
//
// Reference: Analog LC tank circuit analysis; bilinear transform
// =============================================================================

#include "../model/LCResonanceParams.h"
#include "../util/Constants.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace transfo {

class WDFResonanceFilter
{
public:
    // ── Safety limit ──────────────────────────────────────────────────────────
    // Maximum allowed Q factor. If natural Q exceeds this, Zobel damping is
    // automatically engaged to bring Q down to kMaxQ (P1-3 recommendation).
    static constexpr float kMaxQ = 5.0f;

    // ── Construction ─────────────────────────────────────────────────────────

    WDFResonanceFilter() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    void prepare(float sampleRate, const LCResonanceParams& params,
                 float Rs, float Rload)
    {
        sampleRate_ = sampleRate;

        // Store physical parameters for diagnostics and runtime updates
        const float Ll = std::max(params.Lleak, 1e-9f);
        const float Ct = std::max(params.computeCtotal(), 1e-15f);
        Lleak_ = Ll;
        Ctotal_ = Ct;
        Rs_ = std::max(Rs, kEpsilonF);
        Rload_ = std::max(Rload, 1.0f);
        Rz_ = params.Rz;
        Cz_ = params.Cz;

        hasZobel_ = params.hasZobel();
        hasBridgingZero_ = false;
        tauZero_ = 0.0f;

        computeFilterCoefficients(sampleRate, Ll, Ct, Rs_,
                                   Rload_, params);

        reset();
    }

    // Extended prepare: accounts for Cp_s bridging capacitance and Faraday
    // shield. Uses corrected Ctotal (Duerdoth model) and optionally adds a
    // feedforward zero when the bridging zero is below Nyquist/2.
    //
    // turnsRatio = N2/N1 (>1 for step-up, <1 for step-down)
    // hasShield  = true if Faraday shield grounds Cp_s
    void prepare(float sampleRate, const LCResonanceParams& params,
                 float Rs, float Rload,
                 float turnsRatio, bool hasShield)
    {
        sampleRate_ = sampleRate;

        const float Ll = std::max(params.Lleak, 1e-9f);
        const float Ct = std::max(params.computeCtotalCorrected(turnsRatio, hasShield), 1e-15f);
        Lleak_ = Ll;
        Ctotal_ = Ct;
        Rs_ = std::max(Rs, kEpsilonF);
        Rload_ = std::max(Rload, 1.0f);
        Rz_ = params.Rz;
        Cz_ = params.Cz;

        hasZobel_ = params.hasZobel();

        // Determine if Cp_s bridging creates a feedforward zero
        hasBridgingZero_ = false;
        tauZero_ = 0.0f;
        if (!hasShield && params.Cp_s > 0.0f && Rs_ > 0.0f) {
            float fZero = params.computeCpsBridgingZeroFreq(Rs_);
            // Only add the zero if it is within the representable range
            // (below 40% of Nyquist, same threshold as the resonance check)
            if (fZero > 0.0f && fZero < 0.4f * sampleRate) {
                hasBridgingZero_ = true;
                tauZero_ = params.Cp_s * Rs_;
            }
        }

        computeFilterCoefficients(sampleRate, Ll, Ct, Rs_,
                                   Rload_, params);

        reset();
    }

    void updateParameters(const LCResonanceParams& params,
                          float Rs, float Rload)
    {
        const float Ll = std::max(params.Lleak, 1e-9f);
        const float Ct = std::max(params.computeCtotal(), 1e-15f);
        Lleak_ = Ll;
        Ctotal_ = Ct;
        Rs_ = std::max(Rs, kEpsilonF);
        Rload_ = std::max(Rload, 1.0f);
        Rz_ = params.Rz;
        Cz_ = params.Cz;

        hasZobel_ = params.hasZobel();
        hasBridgingZero_ = false;
        tauZero_ = 0.0f;

        computeFilterCoefficients(sampleRate_, Ll, Ct, Rs_,
                                   Rload_, params);
    }

    // Extended updateParameters with Cp_s correction
    void updateParameters(const LCResonanceParams& params,
                          float Rs, float Rload,
                          float turnsRatio, bool hasShield)
    {
        const float Ll = std::max(params.Lleak, 1e-9f);
        const float Ct = std::max(params.computeCtotalCorrected(turnsRatio, hasShield), 1e-15f);
        Lleak_ = Ll;
        Ctotal_ = Ct;
        Rs_ = std::max(Rs, kEpsilonF);
        Rload_ = std::max(Rload, 1.0f);
        Rz_ = params.Rz;
        Cz_ = params.Cz;

        hasZobel_ = params.hasZobel();

        hasBridgingZero_ = false;
        tauZero_ = 0.0f;
        if (!hasShield && params.Cp_s > 0.0f && Rs_ > 0.0f) {
            float fZero = params.computeCpsBridgingZeroFreq(Rs_);
            if (fZero > 0.0f && fZero < 0.4f * sampleRate_) {
                hasBridgingZero_ = true;
                tauZero_ = params.Cp_s * Rs_;
            }
        }

        computeFilterCoefficients(sampleRate_, Ll, Ct, Rs_,
                                   Rload_, params);
    }

    // ── Reset ────────────────────────────────────────────────────────────────

    void reset()
    {
        std::memset(x_, 0, sizeof(x_));
        std::memset(y_, 0, sizeof(y_));
    }

    // ── Per-sample processing ────────────────────────────────────────────────

    float processSample(float input)
    {
        // Direct form II transposed (up to 3rd order)
        float output = b_[0] * input + x_[0];
        x_[0] = b_[1] * input - a_[1] * output + x_[1];
        x_[1] = b_[2] * input - a_[2] * output + x_[2];
        x_[2] = b_[3] * input - a_[3] * output;

        return output;
    }

    // ── Block processing ─────────────────────────────────────────────────────

    void processBlock(float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            buffer[i] = processSample(buffer[i]);
    }

    void processBlock(const float* input, float* output, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            output[i] = processSample(input[i]);
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────

    float getResonantFrequency() const
    {
        if (Lleak_ <= 0.0f || Ctotal_ <= 0.0f)
            return 0.0f;
        return 1.0f / (kTwoPif * std::sqrt(Lleak_ * Ctotal_));
    }

    // Correct Q for the actual circuit topology (series Rs + L, shunt C || Rload)
    // Q = sqrt((Rs + Rload) * L * C * Rload) / (Rs * C * Rload + L)
    float getQFactor() const
    {
        if (Rs_ <= 0.0f || Rload_ <= 0.0f || Lleak_ <= 0.0f || Ctotal_ <= 0.0f)
            return 1.0f;
        const double Ld = Lleak_, Cd = Ctotal_, Rsd = Rs_, Rld = Rload_;
        const double num = std::sqrt((Rsd + Rld) * Ld * Cd * Rld);
        const double den = Rsd * Cd * Rld + Ld;
        if (den <= 0.0) return 1.0f;
        return static_cast<float>(num / den);
    }

    // DEPRECATED: Simple series RLC formula Q = sqrt(L/C) / Rs
    float getQFactorSeries() const
    {
        if (Rs_ <= 0.0f || Lleak_ <= 0.0f || Ctotal_ <= 0.0f)
            return 1.0f;
        return (1.0f / Rs_) * std::sqrt(Lleak_ / Ctotal_);
    }

    bool hasZobel() const { return hasZobel_; }

    // ── Runtime Zobel update ──────────────────────────────────────────────────
    // Update Zobel damping at runtime (e.g., from plugin knob).
    // Recomputes filter coefficients with new Zobel values.
    // Does NOT reset filter state (allows smooth parameter change).
    void setZobel(float Rz, float Cz)
    {
        hasZobel_ = (Rz > 0.0f && Cz > 0.0f);
        Rz_ = Rz;
        Cz_ = Cz;

        if (hasZobel_)
            computeCoefficients3rdOrder(sampleRate_, Lleak_, Ctotal_,
                                         Rs_, Rload_, Rz, Cz);
        else
            computeCoefficients2ndOrder(sampleRate_, Lleak_, Ctotal_,
                                         Rs_, Rload_);
    }

    // ── Q diagnostics ─────────────────────────────────────────────────────────

    // Natural Q: the Q of the circuit without Zobel damping.
    // For the loaded LC circuit:
    //   H(s) = Rload / (LCR*s^2 + (RsCR + L)*s + (Rs + R))
    //   omega0^2 = (Rs+R)/(LCR)
    //   1/(Q*omega0) = (RsCR + L)/(LCR)
    //   Q = sqrt((Rs+R)*L*C*R) / (Rs*C*R + L)
    float computeNaturalQ() const
    {
        return computeNaturalQ(Lleak_, Ctotal_, Rs_, Rload_);
    }

    float computeNaturalQ(float L, float C, float Rs, float Rload) const
    {
        double Ld = L, Cd = C, Rsd = Rs, Rld = Rload;
        double num = std::sqrt((Rsd + Rld) * Ld * Cd * Rld);
        double den = Rsd * Cd * Rld + Ld;
        return (den > 0.0) ? static_cast<float>(num / den) : 1.0f;
    }

    // Stored Zobel accessors
    float getZobelR() const { return Rz_; }
    float getZobelC() const { return Cz_; }
    float getRload() const { return Rload_; }

private:
    // ── Dispatcher: choose bypass or full filter ────────────────────────────

    void computeFilterCoefficients(float fs, float L, float C,
                                    float Rs, float Rload,
                                    const LCResonanceParams& params)
    {
        // Compute analog resonant frequency
        const float fres = 1.0f / (kTwoPif * std::sqrt(L * C));

        // If resonant frequency is above 40% of Nyquist, the bilinear
        // transform cannot represent it correctly — degenerate to a
        // simple DC voltage divider gain: Rload / (Rs + Rload).
        if (fres > 0.4f * fs)
        {
            const float dcGain = Rload / (Rs + Rload);
            b_[0] = dcGain;
            b_[1] = 0.0f;
            b_[2] = 0.0f;
            b_[3] = 0.0f;
            a_[0] = 1.0f;
            a_[1] = 0.0f;
            a_[2] = 0.0f;
            a_[3] = 0.0f;
            order_ = 0;
            return;
        }

        if (hasZobel_)
        {
            computeCoefficients3rdOrder(fs, L, C, Rs, Rload,
                                         params.Rz, params.Cz);
        }
        else
        {
            // Q clamp safety net (P1-3): if natural Q exceeds kMaxQ,
            // auto-engage Zobel damping to bring Q down to kMaxQ.
            float naturalQ = computeNaturalQ(L, C, Rs, Rload);
            if (naturalQ > kMaxQ)
            {
                float Z0 = std::sqrt(L / C);
                float autoRz = Z0 / kMaxQ;
                float autoCz = L / (autoRz * autoRz);
                computeCoefficients3rdOrder(fs, L, C, Rs, Rload,
                                             autoRz, autoCz);
                autoZobelEngaged_ = true;
                return;
            }
            autoZobelEngaged_ = false;
            if (hasBridgingZero_)
                computeCoefficients2ndOrderWithZero(fs, L, C, Rs, Rload, tauZero_);
            else
                computeCoefficients2ndOrder(fs, L, C, Rs, Rload);
        }
    }

    // ── Compute biquad coefficients (no Zobel: 2nd order) ───────────────────
    //
    // H(s) = Rload / (a2*s^2 + a1*s + a0)
    //   a0 = Rs + Rload
    //   a1 = Rs*C*Rload + L
    //   a2 = L*C*Rload
    //
    // Bilinear: s = K*(z-1)/(z+1), K = 2*fs
    //
    // Numerator N(z):   Rload * (1 + z^-1)^2 / (...)
    // After clearing (z+1)^2:
    //   N(z) = Rload * (z^2 + 2z + 1)
    //   D(z) = (a2*K^2 + a1*K + a0)*z^2 + (-2*a2*K^2 + 2*a0)*z + (a2*K^2 - a1*K + a0)

    void computeCoefficients2ndOrder(float fs, float L, float C,
                                      float Rs, float Rload)
    {
        const double K = 2.0 * static_cast<double>(fs);
        const double K2 = K * K;
        const double Ld = static_cast<double>(L);
        const double Cd = static_cast<double>(C);
        const double Rsd = static_cast<double>(Rs);
        const double Rld = static_cast<double>(Rload);

        const double a0 = Rsd + Rld;
        const double a1 = Rsd * Cd * Rld + Ld;
        const double a2 = Ld * Cd * Rld;

        const double D0 = a2 * K2 + a1 * K + a0;
        const double D1 = -2.0 * a2 * K2 + 2.0 * a0;
        const double D2 = a2 * K2 - a1 * K + a0;

        const double invD0 = 1.0 / D0;

        b_[0] = static_cast<float>(Rld * invD0);
        b_[1] = static_cast<float>(2.0 * Rld * invD0);
        b_[2] = static_cast<float>(Rld * invD0);
        b_[3] = 0.0f;

        a_[0] = 1.0f;
        a_[1] = static_cast<float>(D1 * invD0);
        a_[2] = static_cast<float>(D2 * invD0);
        a_[3] = 0.0f;

        order_ = 2;
    }

    // ── Compute biquad coefficients with feedforward zero (2nd order) ───────
    //
    // H(s) = Rload * (1 + s*tau) / (a2*s^2 + a1*s + a0)
    //
    // This adds a feedforward zero from the Cp_s bridging current path.
    // tau = Cp_s * Rs (time constant of the bridging zero).
    // Bilinear: s = K*(z-1)/(z+1), K = 2*fs
    //
    // Numerator becomes 2nd order after bilinear:
    //   N(s) = Rload * (1 + tau*s)
    //   N(z) = Rload * [(1 + tau*K)*z + (1 - tau*K)] / (z+1)
    //   After clearing (z+1)^2:
    //   N(z) = Rload * [(1+tau*K)*(z+1) + tau*K*(z-1)]   -- wait, let's be precise
    //
    // N(s) = Rload*(1 + tau*s)
    //   s = K(z-1)/(z+1)
    //   N(z)(z+1)^2 = Rload * [(z+1)^2 + tau*K*(z-1)*(z+1)]
    //               = Rload * [(z^2+2z+1) + tau*K*(z^2-1)]
    //               = Rload * [(1+tau*K)*z^2 + 2*z + (1-tau*K)]
    //
    // D(z) same as computeCoefficients2ndOrder

    void computeCoefficients2ndOrderWithZero(float fs, float L, float C,
                                              float Rs, float Rload, float tau)
    {
        const double K = 2.0 * static_cast<double>(fs);
        const double K2 = K * K;
        const double Ld = static_cast<double>(L);
        const double Cd = static_cast<double>(C);
        const double Rsd = static_cast<double>(Rs);
        const double Rld = static_cast<double>(Rload);
        const double taud = static_cast<double>(tau);

        const double a0 = Rsd + Rld;
        const double a1 = Rsd * Cd * Rld + Ld;
        const double a2 = Ld * Cd * Rld;

        const double D0 = a2 * K2 + a1 * K + a0;
        const double D1 = -2.0 * a2 * K2 + 2.0 * a0;
        const double D2 = a2 * K2 - a1 * K + a0;

        const double invD0 = 1.0 / D0;

        // Numerator with feedforward zero: Rload*(1 + tau*s)
        // After bilinear and clearing (z+1)^2:
        // N(z) = Rld * [(1+tK)*z^2 + 2*z + (1-tK)]
        const double tK = taud * K;
        b_[0] = static_cast<float>(Rld * (1.0 + tK) * invD0);
        b_[1] = static_cast<float>(Rld * 2.0 * invD0);
        b_[2] = static_cast<float>(Rld * (1.0 - tK) * invD0);
        b_[3] = 0.0f;

        a_[0] = 1.0f;
        a_[1] = static_cast<float>(D1 * invD0);
        a_[2] = static_cast<float>(D2 * invD0);
        a_[3] = 0.0f;

        order_ = 2;
    }

    // ── Compute coefficients with Zobel (3rd order) ─────────────────────────
    //
    // Circuit: Vin → Rs → sL → [Rload || 1/(sC) || (Rz + 1/(sCz))] → GND
    //
    // Let Y_par = 1/Rload + sC + sCz/(1+sRzCz)
    //           = [p0 + p1*s + p2*s^2] / (1 + Rz*Cz*s)
    // where:
    //   p0 = 1/Rload
    //   p1 = Rz*Cz/Rload + C + Cz
    //   p2 = C*Rz*Cz
    //
    // H(s) = Z_par / (Rs + sL + Z_par) = 1 / (1 + (Rs+sL)*Y_par)
    // Clearing (1+sRzCz):
    //   Numerator:   (1 + s*Rz*Cz)
    //   Denominator: (Rs+sL)(p0+p1*s+p2*s^2) + (1+s*Rz*Cz)
    //
    // Expand denominator:
    //   d0 = Rs*p0 + 1 = Rs/Rload + 1 = (Rs+Rload)/Rload
    //   d1 = Rs*p1 + L*p0 + Rz*Cz
    //   d2 = Rs*p2 + L*p1
    //   d3 = L*p2
    //
    // Then multiply both num and den by Rload:
    //   Num: Rload*(1 + Rz*Cz*s)  (1st order)
    //   Den: d0'*s^0 + d1'*s^1 + d2'*s^2 + d3'*s^3
    //   d0' = Rs + Rload
    //   d1' = Rload*(Rs*p1 + L*p0 + Rz*Cz)
    //       = Rs*Rz*Cz + Rs*C*Rload + Rs*Cz*Rload + L + Rz*Cz*Rload
    //   d2' = Rload*(Rs*p2 + L*p1)
    //       = Rs*C*Rz*Cz*Rload + L*Rz*Cz + L*C*Rload + L*Cz*Rload
    //   d3' = Rload*L*p2 = L*C*Rz*Cz*Rload
    //
    // Bilinear transform s = K*(z-1)/(z+1), K = 2*fs
    //   s^k -> K^k * (z-1)^k / (z+1)^k
    //   Multiply through by (z+1)^3 to clear denominators.

    void computeCoefficients3rdOrder(float fs, float L, float C,
                                      float Rs, float Rload,
                                      float Rz, float Cz)
    {
        if (Rz <= 0.0f || Cz <= 0.0f)
        {
            computeCoefficients2ndOrder(fs, L, C, Rs, Rload);
            hasZobel_ = false;
            return;
        }

        const double K = 2.0 * static_cast<double>(fs);
        const double K2 = K * K;
        const double K3 = K2 * K;
        const double Ld  = static_cast<double>(L);
        const double Cd  = static_cast<double>(C);
        const double Rsd = static_cast<double>(Rs);
        const double Rld = static_cast<double>(Rload);
        const double Rzd = static_cast<double>(Rz);
        const double Czd = static_cast<double>(Cz);

        // Analog denominator coefficients (multiplied by Rload)
        const double d0 = Rsd + Rld;
        const double d1 = Rsd*Rzd*Czd + Rsd*Cd*Rld + Rsd*Czd*Rld
                        + Ld + Rzd*Czd*Rld;
        const double d2 = Rsd*Cd*Rzd*Czd*Rld + Ld*Rzd*Czd
                        + Ld*Cd*Rld + Ld*Czd*Rld;
        const double d3 = Ld*Cd*Rzd*Czd*Rld;

        // Analog numerator coefficients (multiplied by Rload)
        const double n0 = Rld;
        const double n1 = Rld * Rzd * Czd;

        // Bilinear transform: s = K*(z-1)/(z+1)
        // Multiply num and den by (z+1)^3 to clear all denominators.
        //
        // Numerator: n0*(z+1)^3 + n1*K*(z-1)*(z+1)^2
        // (z+1)^3 = z^3 + 3z^2 + 3z + 1
        // (z-1)(z+1)^2 = (z^2-1)(z+1) = z^3 + z^2 - z - 1
        //
        // N(z) = n0*(z^3 + 3z^2 + 3z + 1) + n1*K*(z^3 + z^2 - z - 1)
        const double N0 = n0 + n1*K;
        const double N1 = 3.0*n0 + n1*K;
        const double N2 = 3.0*n0 - n1*K;
        const double N3 = n0 - n1*K;

        // Denominator:
        // d0*(z+1)^3 + d1*K*(z-1)*(z+1)^2 + d2*K^2*(z-1)^2*(z+1) + d3*K^3*(z-1)^3
        //
        // (z+1)^3     = z^3 + 3z^2 + 3z + 1
        // (z-1)(z+1)^2 = z^3 + z^2 - z - 1
        // (z-1)^2(z+1) = z^3 - z^2 - z + 1
        // (z-1)^3      = z^3 - 3z^2 + 3z - 1
        const double D0z = d0 + d1*K + d2*K2 + d3*K3;
        const double D1z = 3.0*d0 + d1*K - d2*K2 - 3.0*d3*K3;
        const double D2z = 3.0*d0 - d1*K - d2*K2 + 3.0*d3*K3;
        const double D3z = d0 - d1*K + d2*K2 - d3*K3;

        const double invD0z = 1.0 / D0z;

        b_[0] = static_cast<float>(N0 * invD0z);
        b_[1] = static_cast<float>(N1 * invD0z);
        b_[2] = static_cast<float>(N2 * invD0z);
        b_[3] = static_cast<float>(N3 * invD0z);

        a_[0] = 1.0f;
        a_[1] = static_cast<float>(D1z * invD0z);
        a_[2] = static_cast<float>(D2z * invD0z);
        a_[3] = static_cast<float>(D3z * invD0z);

        order_ = 3;
    }

    // ── Filter coefficients (up to 3rd order) ───────────────────────────────

    float b_[4] = {1.0f, 0.0f, 0.0f, 0.0f};  // Numerator (feedforward)
    float a_[4] = {1.0f, 0.0f, 0.0f, 0.0f};  // Denominator (feedback)

    // ── Filter state (transposed direct form II) ────────────────────────────

    float x_[3] = {0.0f, 0.0f, 0.0f};  // State variables

    // Delay line not needed — kept for potential future use
    float y_[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // ── Physical parameters for diagnostics ─────────────────────────────────

    float Lleak_  = 1e-3f;
    float Ctotal_ = 1e-12f;
    float Rs_     = 150.0f;
    float Rload_  = 10000.0f;
    float Rz_     = 0.0f;
    float Cz_     = 0.0f;

    // ── Configuration ────────────────────────────────────────────────────────

    float sampleRate_        = kDefaultSampleRate;
    bool  hasZobel_          = false;
    bool  autoZobelEngaged_  = false;   // true if Q clamp auto-engaged Zobel
    bool  hasBridgingZero_   = false;   // true if Cp_s bridging zero is active
    float tauZero_           = 0.0f;    // time constant for bridging zero (Cp_s * Rs)
    int   order_             = 2;
};

} // namespace transfo
