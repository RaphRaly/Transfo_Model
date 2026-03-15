#pragma once

// =============================================================================
// DynamicLosses — Classical eddy current and excess (anomalous) losses
//                 (Bertotti field separation).
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
// where:
//   K1 = K_eddy = d^2 / (12 * rho)   [s/m]
//   K2 = K_exc                         [A·m^-1·(T/s)^-0.5]
//
// Derivative estimation: backward difference dB/dt = fs * (B[n] - B[n-1]).
// Stable for all inputs (no Nyquist pole, unlike the bilinear derivative).
// First-order accurate with a half-sample delay — sufficient for audio
// frequencies well below Nyquist.
//
// HSIM-compatible: supports commit/rollback for iterative WDF solvers.
//
// K1 and K2 are identified in Phase 2 of the identification pipeline.
//
// Reference: Bertotti 1998; Baghel & Kulkarni IEEE Trans. Magn. 2014
// =============================================================================

#include <cmath>
#include <algorithm>

namespace transfo {

class DynamicLosses
{
public:
    DynamicLosses() = default;

    void setCoefficients(float K1, float K2)
    {
        K1_ = std::max(0.0, static_cast<double>(K1));
        K2_ = std::max(0.0, static_cast<double>(K2));
        enabled_ = (K1_ > 0.0) || (K2_ > 0.0);
    }

    void setSampleRate(double sampleRate)
    {
        // Reset state when sample rate changes — stale B_prev at the old rate
        // would produce a transient spike on the first sample.
        if (sampleRate != sampleRate_) {
            B_prev_committed_    = 0.0;
            B_prev_backup_       = 0.0;
        }
        sampleRate_ = sampleRate;
    }

    bool isEnabled() const { return enabled_; }

    void reset()
    {
        B_prev_committed_    = 0.0;
        B_prev_backup_       = 0.0;
    }

    // ─── Compute H_dynamic from a pre-computed dB/dt ────────────────────────
    // This is the preferred entry point when dBdt is already known
    // (e.g. estimated at the leaf level from wave variables).
    double computeHfromDBdt(double dBdt) const
    {
        // Classical eddy loss: H_eddy = K1 * dB/dt
        const double H_eddy = K1_ * dBdt;

        // Excess loss: H_excess = K2 * sqrt(|dB/dt|) * sign(dB/dt)
        const double absdBdt = std::abs(dBdt);
        const double sign_dBdt = (dBdt > 0.0) ? 1.0 : (dBdt < 0.0) ? -1.0 : 0.0;
        const double H_excess = K2_ * sign_dBdt * std::sqrt(absdBdt + kEpsSqrt);

        return H_eddy + H_excess;
    }

    // ─── Backward difference derivative ─────────────────────────────────────
    // dBdt[n] = fs · (B[n] − B[n−1])
    //
    // First-order accurate with a half-sample delay. Stable for all inputs
    // (no pole at Nyquist, unlike the bilinear transform s=(2/T)(z-1)/(z+1)
    // which has a pole at z=-1 causing permanent oscillation after any step).
    double computeDBdt(double B_current) const
    {
        return sampleRate_ * (B_current - B_prev_committed_);
    }

    // ─── Compute H_dynamic from B_current (backward difference dB/dt) ──────
    // Computes dBdt internally using the backward difference derivative.
    double computeHdynamic(double B_current) const
    {
        const double dBdt = computeDBdt(B_current);
        return computeHfromDBdt(dBdt);
    }

    // ─── NR Jacobian: dH_dynamic / dB ──────────────────────────────────────
    // Used to update the Newton-Raphson Jacobian when solving the combined
    // static + dynamic system. Both terms contribute:
    //   dH_eddy/dB   = K1 * fs   (backward diff: d(dBdt)/dB = fs)
    //   dH_excess/dB = K2 * 0.5 * fs / sqrt(|dBdt| + eps)
    //   Note: d/dx [sign(x)*sqrt(|x|)] = 0.5/sqrt(|x|) for all x != 0
    //         (always positive — the function has the same slope sign on both sides)
    double computeJacobian(double dBdt) const
    {
        // Backward difference sensitivity: d(dBdt)/dB = sampleRate
        const double dDBdt_dB = sampleRate_;

        const double dH_eddy_dB = K1_ * dDBdt_dB;

        const double absdBdt = std::abs(dBdt);
        const double dH_exc_dB = K2_ * 0.5 * dDBdt_dB
                                 / std::sqrt(absdBdt + kEpsJac);

        return dH_eddy_dB + dH_exc_dB;
    }

    // ─── HSIM state management ─────────────────────────────────────────────
    // commitState: lock in the current B as the new B_prev for next sample.
    void commitState(double B_committed)
    {
        B_prev_committed_ = B_committed;
    }

    // savePrevState / restorePrevState: snapshot for HSIM rollback.
    void savePrevState()
    {
        B_prev_backup_ = B_prev_committed_;
    }

    void restorePrevState()
    {
        B_prev_committed_ = B_prev_backup_;
    }

    // ─── Legacy API (backward compatibility with ObjectiveFunction) ─────────
    // Alias for commitState() — used by identification pipeline.
    void updateState(double B_current)
    {
        commitState(B_current);
    }

    // ─── Accessors ─────────────────────────────────────────────────────────
    double getK1() const { return K1_; }
    double getK2() const { return K2_; }
    double getBprevCommitted() const { return B_prev_committed_; }
    double getSampleRate() const { return sampleRate_; }

    // ─── Helper: compute K_eddy from physical material properties ──────────
    // d = lamination thickness [m], rho = resistivity [Ohm·m]
    static float computeKeddy(float d_meters, float rho_ohm_m)
    {
        if (rho_ohm_m <= 0.0f) return 0.0f;
        return (d_meters * d_meters) / (12.0f * rho_ohm_m);
    }

private:
    double K1_ = 0.0;               // Classical eddy coefficient (K_eddy)
    double K2_ = 0.0;               // Excess loss coefficient   (K_exc)
    double sampleRate_ = 44100.0;
    bool   enabled_ = false;

    // HSIM double-buffered state
    double B_prev_committed_    = 0.0; // B[k-1] confirmed
    double B_prev_backup_       = 0.0; // Snapshot for rollback

    // Epsilon for sqrt(0) safety in the function value.
    // Small enough to not affect the sound at zero crossings.
    static constexpr double kEpsSqrt = 1e-12;

    // Epsilon for the Jacobian sqrt denominator.
    // Larger than kEpsSqrt to keep NR Jacobian finite near dBdt=0.
    static constexpr double kEpsJac = 1e-6;
};

} // namespace transfo
