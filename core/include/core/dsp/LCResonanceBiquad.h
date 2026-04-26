#pragma once

// =============================================================================
// LCResonanceBiquad -- Bilinear-transformed LC parasitic resonance filter
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
// Implementation note: this class is a pure analytical filter (no wave
// variables, no WDF tree, no scattering). It replaces the previous
// WDFResonanceFilter, whose name was misleading -- the math was already
// analytical-domain BLT.
//
// Reference: Analog LC tank circuit analysis; bilinear transform
// =============================================================================

#include "../model/LCResonanceParams.h"
#include "../util/Constants.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace transfo {

class LCResonanceBiquad
{
public:
    // ── Safety limit ──────────────────────────────────────────────────────────
    // Maximum allowed Q factor. If natural Q exceeds this, Zobel damping is
    // automatically engaged to bring Q down to kMaxQ.
    static constexpr float kMaxQ = 5.0f;

    LCResonanceBiquad() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    void prepare(float sampleRate, const LCResonanceParams& params,
                 float Rs, float Rload)
    {
        sampleRate_ = sampleRate;

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

        computeFilterCoefficients(sampleRate, Ll, Ct, Rs_, Rload_, params);

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

        hasBridgingZero_ = false;
        tauZero_ = 0.0f;
        if (!hasShield && params.Cp_s > 0.0f && Rs_ > 0.0f) {
            float fZero = params.computeCpsBridgingZeroFreq(Rs_);
            if (fZero > 0.0f && fZero < 0.4f * sampleRate) {
                hasBridgingZero_ = true;
                tauZero_ = params.Cp_s * Rs_;
            }
        }

        computeFilterCoefficients(sampleRate, Ll, Ct, Rs_, Rload_, params);

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

        computeFilterCoefficients(sampleRate_, Ll, Ct, Rs_, Rload_, params);
    }

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

        computeFilterCoefficients(sampleRate_, Ll, Ct, Rs_, Rload_, params);
    }

    void reset()
    {
        std::memset(x_, 0, sizeof(x_));
        std::memset(y_, 0, sizeof(y_));
    }

    // ── Per-sample processing (DF II Transposed, up to 3rd order) ───────────

    float processSample(float input)
    {
        float output = b_[0] * input + x_[0];
        x_[0] = b_[1] * input - a_[1] * output + x_[1];
        x_[1] = b_[2] * input - a_[2] * output + x_[2];
        x_[2] = b_[3] * input - a_[3] * output;
        return output;
    }

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

    // Loaded-circuit Q: Q = sqrt((Rs + Rload) * L * C * Rload) / (Rs * C * Rload + L)
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

    // Update Zobel damping at runtime without resetting filter state.
    void setZobel(float Rz, float Cz)
    {
        hasZobel_ = (Rz > 0.0f && Cz > 0.0f);
        Rz_ = Rz;
        Cz_ = Cz;

        if (hasZobel_)
            computeCoefficients3rdOrder(sampleRate_, Lleak_, Ctotal_, Rs_, Rload_, Rz, Cz);
        else
            computeCoefficients2ndOrder(sampleRate_, Lleak_, Ctotal_, Rs_, Rload_);
    }

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

    float getZobelR() const { return Rz_; }
    float getZobelC() const { return Cz_; }
    float getRload() const { return Rload_; }

private:
    void computeFilterCoefficients(float fs, float L, float C,
                                    float Rs, float Rload,
                                    const LCResonanceParams& params)
    {
        const float fres = 1.0f / (kTwoPif * std::sqrt(L * C));

        // BLT degeneracy: if resonance is above 40% of Nyquist, the bilinear
        // transform cannot represent it correctly. Fall back to the DC
        // voltage divider Rload / (Rs + Rload).
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
            computeCoefficients3rdOrder(fs, L, C, Rs, Rload, params.Rz, params.Cz);
        }
        else
        {
            float naturalQ = computeNaturalQ(L, C, Rs, Rload);
            if (naturalQ > kMaxQ)
            {
                float Z0 = std::sqrt(L / C);
                float autoRz = Z0 / kMaxQ;
                float autoCz = L / (autoRz * autoRz);
                computeCoefficients3rdOrder(fs, L, C, Rs, Rload, autoRz, autoCz);
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

    // 2nd-order BLT: H(s) = Rload / (a2*s^2 + a1*s + a0)
    void computeCoefficients2ndOrder(float fs, float L, float C,
                                      float Rs, float Rload)
    {
        const double K = 2.0 * static_cast<double>(fs);
        const double K2 = K * K;
        const double Ld = L, Cd = C, Rsd = Rs, Rld = Rload;

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

    // 2nd-order with feedforward zero from Cp_s bridging:
    // H(s) = Rload * (1 + s*tau) / (a2*s^2 + a1*s + a0), tau = Cp_s * Rs
    void computeCoefficients2ndOrderWithZero(float fs, float L, float C,
                                              float Rs, float Rload, float tau)
    {
        const double K = 2.0 * static_cast<double>(fs);
        const double K2 = K * K;
        const double Ld = L, Cd = C, Rsd = Rs, Rld = Rload;
        const double taud = static_cast<double>(tau);

        const double a0 = Rsd + Rld;
        const double a1 = Rsd * Cd * Rld + Ld;
        const double a2 = Ld * Cd * Rld;

        const double D0 = a2 * K2 + a1 * K + a0;
        const double D1 = -2.0 * a2 * K2 + 2.0 * a0;
        const double D2 = a2 * K2 - a1 * K + a0;

        const double invD0 = 1.0 / D0;

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

    // 3rd-order with Zobel network (Rz + Cz in series, shunt to GND).
    // See WDFResonanceFilter.h history for full algebraic derivation.
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
        const double Ld = L, Cd = C, Rsd = Rs, Rld = Rload;
        const double Rzd = Rz, Czd = Cz;

        const double d0 = Rsd + Rld;
        const double d1 = Rsd*Rzd*Czd + Rsd*Cd*Rld + Rsd*Czd*Rld
                        + Ld + Rzd*Czd*Rld;
        const double d2 = Rsd*Cd*Rzd*Czd*Rld + Ld*Rzd*Czd
                        + Ld*Cd*Rld + Ld*Czd*Rld;
        const double d3 = Ld*Cd*Rzd*Czd*Rld;

        const double n0 = Rld;
        const double n1 = Rld * Rzd * Czd;

        const double N0 = n0 + n1*K;
        const double N1 = 3.0*n0 + n1*K;
        const double N2 = 3.0*n0 - n1*K;
        const double N3 = n0 - n1*K;

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

    // Filter coefficients (up to 3rd order)
    float b_[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float a_[4] = {1.0f, 0.0f, 0.0f, 0.0f};

    // Filter state (transposed direct form II)
    float x_[3] = {0.0f, 0.0f, 0.0f};
    float y_[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Physical parameters (for diagnostics + runtime updates)
    float Lleak_  = 1e-3f;
    float Ctotal_ = 1e-12f;
    float Rs_     = 150.0f;
    float Rload_  = 10000.0f;
    float Rz_     = 0.0f;
    float Cz_     = 0.0f;

    float sampleRate_       = kDefaultSampleRate;
    bool  hasZobel_         = false;
    bool  autoZobelEngaged_ = false;
    bool  hasBridgingZero_  = false;
    float tauZero_          = 0.0f;
    int   order_            = 2;
};

} // namespace transfo
