#pragma once

// =============================================================================
// JilesAthertonLeaf — WDF one-port nonlinear element wrapping the J-A model
//                     with Bertotti dynamic loss extension.
//
// This leaf connects the HysteresisModel (magnetic domain) to the WDF
// circuit (wave domain). Used in Physical mode for maximum accuracy.
//
// Wave-to-magnetic conversion:
//   H = (a_m - alpha2 * M_committed) / alpha1
//   where alpha1, alpha2 are derived from the port resistance and
//   core geometry (Gamma = magnetic path length, Lambda = cross-section).
//
// Dynamic extension (Bertotti field separation):
//   H_total = H_hyst + K_eddy·dB/dt + K_exc·sign(dB/dt)·|dB/dt|^0.5
//   The dynamic terms reduce the effective field driving the static J-A
//   model, concentrating distortion at low frequencies and providing
//   high-frequency transparency — matching real transformer behavior.
//
// Port resistance (adaptive):
//   Z_m = Gamma / (Lambda * mu0 * (1 + dM/dH))
//   Updated every adaptationInterval_ samples.
//
// Pattern: same as chowdsp_wdf DiodeT.h, TriodeT.h — NR local,
// warm-start on previous solution.
//
// Reference: Polimi thesis; chowdsp_wdf; Werner thesis;
//            Baghel & Kulkarni IEEE Trans. Magn. 2014 (field separation)
// =============================================================================

#include "../util/Constants.h"
#include "../wdf/WDOnePort.h"
#include "DynamicLosses.h"
#include "HysteresisModel.h"
#include <cmath>

namespace transfo {

template <typename AnhystType>
class JilesAthertonLeaf : public WDOnePort<JilesAthertonLeaf<AnhystType>> {
public:
  JilesAthertonLeaf() = default;

  // ─── Configuration ──────────────────────────────────────────────────────
  void configure(float Gamma, float Lambda, const JAParameterSet &params,
                 double sampleRate) {
    Gamma_ = Gamma;
    Lambda_ = Lambda;
    sampleRate_ = sampleRate;
    hystModel_.setParameters(params);
    hystModel_.setSampleRate(sampleRate);
    hystModel_.reset();
    dynLosses_.setCoefficients(params.K1, params.K2);
    dynLosses_.setSampleRate(sampleRate);
    dynLosses_.reset();
    updateCachedCoeffs();
  }

  void setParameters(const JAParameterSet &params) {
    hystModel_.setParameters(params);
    dynLosses_.setCoefficients(params.K1, params.K2);
    updateCachedCoeffs();
  }

  // ─── WDF scattering (called by WDOnePort CRTP) ─────────────────────────
  float scatterImpl(float a_m) {
    // 1. Convert wave variable to magnetic field H (applied by circuit)
    //    H = (a_m - alpha2_ * M_committed) / alpha1_
    const double M_c = hystModel_.getMagnetization();
    const double H_applied = (static_cast<double>(a_m) - alpha2_ * M_c) / alpha1_;

    // 2. Dynamic Bertotti extension: estimate dB/dt and compute H_dynamic
    //    B_pred = mu0*(H + M_committed) backward diff only captures μ₀·ΔH
    //    (M cancels). Self-consistent damped χ-scaling restores ΔM:
    //      dBdt = dBdt_raw * (1+χ) / (1 + K1·fs·μ₀·χ)
    //    The denominator bounds the correction — mirrors the physical
    //    self-limiting feedback of eddy currents in the laminations.
    double H_effective = H_applied;

    if (dynLosses_.isEnabled()) {
      const double chi = std::max(0.0, hystModel_.getInstantaneousSusceptibility());
      const double B_pred = kMu0 * (H_applied + M_c);
      const double dBdt_raw = dynLosses_.computeDBdt(B_pred);
      const double G = dynLosses_.getK1() * dynLosses_.getSampleRate() * kMu0 * chi;
      const double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
      const double H_dynamic = dynLosses_.computeHfromDBdt(dBdt);
      H_effective = H_applied - H_dynamic;
    }

    // 3. Solve J-A: Newton-Raphson trapezoidal (using effective field)
    //    Warm-start handled internally by HysteresisModel::solveImplicitStep
    const double M_new = hystModel_.solveImplicitStep(H_effective);

    // 4. Compute reflected wave b_m from scattering equation
    //    b_m = a_m - 2 * Z_m * I_m
    //    where I_m is the magnetic current (flux rate)
    //    I_m = Lambda * mu0 * dM  (simplified for WDF)
    const double dM = M_new - M_c;
    const double b_m = static_cast<double>(a_m) -
                       2.0 * static_cast<double>(this->Z_port_) * Lambda_ *
                           kMu0 * dM / (Gamma_ + kEpsilonD);

    // 5. Store physical state for BHScope monitoring and dynamic losses
    //    B uses the total applied H (not H_effective) per B = mu0*(H + M)
    lastH_ = static_cast<float>(H_applied);
    B_tentative_ = kMu0 * (H_applied + M_new);
    lastB_ = static_cast<float>(B_tentative_);

    return static_cast<float>(b_m);
  }

  // ─── Adaptive port resistance ───────────────────────────────────────────
  float getPortResistanceImpl() const {
    // Z_m = Gamma / (Lambda * mu0 * (1 + dM/dH))
    const double dMdH = hystModel_.getInstantaneousSusceptibility();
    const double suscept = 1.0 + dMdH;

    if (std::abs(suscept) < kEpsilonD)
      return static_cast<float>(Gamma_ / (Lambda_ * kMu0 * 1.0)); // fallback

    const double Z_m = Gamma_ / (Lambda_ * kMu0 * suscept);
    return static_cast<float>(std::clamp(Z_m, 1e-3, 1e12));
  }

  // ─── State management (HSIM interface) ──────────────────────────────────
  void commitState() {
    hystModel_.commitState();
    // Commit actual B (B_tentative) — the damped χ-scaling in scatterImpl
    // handles M-cancellation without needing B_pred commit.
    dynLosses_.commitState(B_tentative_);
  }

  void rollbackState() {
    hystModel_.rollbackState();
    // B_prev_committed stays at last committed value (no rollback needed —
    // dynLosses uses committed B_prev which is only updated in commitState)
  }

  // ─── Access ─────────────────────────────────────────────────────────────
  HysteresisModel<AnhystType> &getHysteresisModel() { return hystModel_; }
  const HysteresisModel<AnhystType> &getHysteresisModel() const {
    return hystModel_;
  }

  DynamicLosses &getDynamicLosses() { return dynLosses_; }
  const DynamicLosses &getDynamicLosses() const { return dynLosses_; }

  float getGamma() const { return static_cast<float>(Gamma_); }
  float getLambda() const { return static_cast<float>(Lambda_); }

  float getH() const { return lastH_; }
  float getB() const { return lastB_; }

  void reset() {
    hystModel_.reset();
    dynLosses_.reset();
    lastH_ = 0.0f;
    lastB_ = 0.0f;
    B_tentative_ = 0.0;
  }

private:
  HysteresisModel<AnhystType> hystModel_;
  DynamicLosses dynLosses_;

  float lastH_ = 0.0f;
  float lastB_ = 0.0f;

  double Gamma_ = 0.1;        // Magnetic path length [m]
  double Lambda_ = 1e-4;      // Cross-section area [m^2]
  double sampleRate_ = 44100.0;
  double B_tentative_ = 0.0;  // B during current HSIM iteration
  double alpha1_ = 1.0;       // Cached coefficient: Gamma / (Lambda * mu0)
  double alpha2_ = 0.0;       // Cached coefficient: related to alpha feedback

  void updateCachedCoeffs() {
    alpha1_ = Gamma_ / (Lambda_ * kMu0 + kEpsilonD);
    alpha2_ = hystModel_.getParameters().alpha * alpha1_;
  }
};

} // namespace transfo
