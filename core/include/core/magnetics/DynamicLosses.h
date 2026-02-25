#pragma once

// =============================================================================
// DynamicLosses — Classical eddy current and excess (anomalous) losses.
//
// The J-A static model captures hysteresis loss only. Real transformer cores
// also exhibit:
//   1. Classical eddy current losses: proportional to (dB/dt)^2
//   2. Excess/anomalous losses: proportional to |dB/dt|^1.5
//
// Total loss separation (Bertotti):
//   P_total = P_hysteresis + P_classical + P_excess
//
// In the time domain, these appear as additional H contributions:
//   H_dyn = K1 * dB/dt + K2 * |dB/dt|^0.5 * sign(dB/dt)
//
// K1 and K2 are identified in Phase 2 of the identification pipeline.
// =============================================================================

#include <cmath>

namespace transfo {

class DynamicLosses
{
public:
    DynamicLosses() = default;

    void setCoefficients(float K1, float K2)
    {
        K1_ = K1;
        K2_ = K2;
    }

    void setSampleRate(double sampleRate)
    {
        Ts_ = 1.0 / sampleRate;
    }

    void reset()
    {
        B_prev_ = 0.0;
    }

    // Compute dynamic loss contribution to H field
    // Call after computing B = mu0 * (H + M)
    double computeHdynamic(double B_current) const
    {
        const double dBdt = (B_current - B_prev_) / Ts_;

        // Classical eddy loss: H_eddy = K1 * dB/dt
        const double H_eddy = K1_ * dBdt;

        // Excess loss: H_excess = K2 * sqrt(|dB/dt|) * sign(dB/dt)
        const double absdBdt = std::abs(dBdt);
        const double H_excess = K2_ * std::sqrt(absdBdt) * ((dBdt >= 0.0) ? 1.0 : -1.0);

        return H_eddy + H_excess;
    }

    // Update state (call after commit)
    void updateState(double B_current)
    {
        B_prev_ = B_current;
    }

    double getK1() const { return K1_; }
    double getK2() const { return K2_; }

private:
    double K1_ = 0.0;      // Classical eddy coefficient
    double K2_ = 0.0;      // Excess loss coefficient
    double B_prev_ = 0.0;  // Previous flux density
    double Ts_ = 1.0 / 44100.0;
};

} // namespace transfo
