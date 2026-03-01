#pragma once

// =============================================================================
// JilesAthertonLeaf — WDF one-port nonlinear element wrapping the J-A model.
//
// This leaf connects the HysteresisModel (magnetic domain) to the WDF
// circuit (wave domain). Used in Physical mode for maximum accuracy.
//
// Wave-to-magnetic conversion:
//   H = (a_m - alpha2 * M_committed) / alpha1
//   where alpha1, alpha2 are derived from the port resistance and
//   core geometry (Gamma = magnetic path length, Lambda = cross-section).
//
// Port resistance (adaptive):
//   Z_m = Gamma / (Lambda * mu0 * (1 + dM/dH))
//   Updated every adaptationInterval_ samples.
//
// Pattern: same as chowdsp_wdf DiodeT.h, TriodeT.h — NR local,
// warm-start on previous solution.
//
// Reference: Polimi thesis; chowdsp_wdf; Werner thesis
// =============================================================================

#include "../util/Constants.h"
#include "../wdf/WDOnePort.h"
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
    hystModel_.setParameters(params);
    hystModel_.setSampleRate(sampleRate);
    hystModel_.reset();
    updateCachedCoeffs();
  }

  void setParameters(const JAParameterSet &params) {
    hystModel_.setParameters(params);
    updateCachedCoeffs();
  }

  // ─── WDF scattering (called by WDOnePort CRTP) ─────────────────────────
  float scatterImpl(float a_m) {
    // 1. Convert wave variable to magnetic field H
    //    H = (a_m - alpha2_ * M_committed) / alpha1_
    const double M_c = hystModel_.getMagnetization();
    const double H = (static_cast<double>(a_m) - alpha2_ * M_c) / alpha1_;

    // 2. Warm-start: extrapolative prediction [v2]
    //    (handled internally by HysteresisModel::solveImplicitStep)

    // 3. Solve J-A: Newton-Raphson trapezoidal
    const double M_new = hystModel_.solveImplicitStep(H);

    // 4. Compute reflected wave b_m from scattering equation
    //    b_m = a_m - 2 * Z_m * I_m
    //    where I_m is the magnetic current (flux rate)
    //    I_m = Lambda * mu0 * dM  (simplified for WDF)
    const double dM = M_new - M_c;
    const double b_m = static_cast<double>(a_m) -
                       2.0 * static_cast<double>(this->Z_port_) * Lambda_ *
                           kMu0 * dM / (Gamma_ + kEpsilonD);

    // Store physical state for BHScope monitoring
    lastH_ = static_cast<float>(H);
    lastB_ = static_cast<float>(kMu0 * (H + M_new));

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
  void commitState() { hystModel_.commitState(); }

  void rollbackState() { hystModel_.rollbackState(); }

  // ─── Access ─────────────────────────────────────────────────────────────
  HysteresisModel<AnhystType> &getHysteresisModel() { return hystModel_; }
  const HysteresisModel<AnhystType> &getHysteresisModel() const {
    return hystModel_;
  }

  float getGamma() const { return static_cast<float>(Gamma_); }
  float getLambda() const { return static_cast<float>(Lambda_); }

  float getH() const { return lastH_; }
  float getB() const { return lastB_; }

  void reset() {
    hystModel_.reset();
    lastH_ = 0.0f;
    lastB_ = 0.0f;
  }

private:
  HysteresisModel<AnhystType> hystModel_;

  float lastH_ = 0.0f;
  float lastB_ = 0.0f;

  double Gamma_ = 0.1;   // Magnetic path length [m]
  double Lambda_ = 1e-4; // Cross-section area [m^2]
  double alpha1_ = 1.0;  // Cached coefficient: Gamma / (Lambda * mu0)
  double alpha2_ = 0.0;  // Cached coefficient: related to alpha feedback

  void updateCachedCoeffs() {
    alpha1_ = Gamma_ / (Lambda_ * kMu0 + kEpsilonD);
    alpha2_ = hystModel_.getParameters().alpha * alpha1_;
  }
};

} // namespace transfo
