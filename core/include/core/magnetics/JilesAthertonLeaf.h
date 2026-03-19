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
  /// Configure with core geometry and optional K_geo for WDF circuit use.
  /// When K_geo > 0, port impedance and scattering use electrical-domain
  /// formulas: Z = 2*K_geo*mu_inc*fs, correct for referred-to-primary WDF.
  /// When K_geo <= 0 (default), uses legacy magnetic-domain formulas.
  void configure(float Gamma, float Lambda, const JAParameterSet &params,
                 double sampleRate, float K_geo = 0.0f) {
    Gamma_ = Gamma;
    Lambda_ = Lambda;
    K_geo_ = K_geo;
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
    const double M_c = hystModel_.getMagnetization();
    const double Z = static_cast<double>(this->Z_port_);
    double H_applied;

    if (K_geo_ > 0.0f) {
      // Electrical-domain WDF: wave variable a_m is in volts.
      // I = H * le / N,  V = N * Ae * dB/dt
      // From WDF: I = (a - b) / (2Z).
      // For adapted port at the root of an NR solve, we estimate H from
      // the incident wave using the linearized companion model:
      //   a ≈ 2*V = 2*Z*I + 2*V_hist ≈ 2*Z*I (ignoring history for the NR)
      // So: I ≈ a/(2Z), H = N*I/le = N*a/(2*Z*le)
      // With K_geo = N²*Ae/le → N/le = K_geo/(N*Ae) = sqrt(K_geo/le)/sqrt(Ae)...
      // Simpler: N² = K_geo*le/Ae → N = sqrt(K_geo*le/Ae)
      // H = N * (a_m) / (2*Z*le)  [rough linearization for NR warm-start]
      const double N = std::sqrt(static_cast<double>(K_geo_) * Gamma_ / Lambda_);
      H_applied = N * static_cast<double>(a_m) / (2.0 * Z * Gamma_);
    } else {
      // Legacy magnetic-domain: H = (a_m - alpha2*M) / alpha1
      H_applied = (static_cast<double>(a_m) - alpha2_ * M_c) / alpha1_;
    }

    // 2. Dynamic Bertotti extension
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

    // 3. Solve J-A: Newton-Raphson trapezoidal
    const double M_new = hystModel_.solveImplicitStep(H_effective);

    // 4. Compute reflected wave b_m
    const double dM = M_new - M_c;
    double b_m;

    if (K_geo_ > 0.0f) {
      // Electrical-domain: b = a - 2*Z*I where I = H*le/N
      const double N = std::sqrt(static_cast<double>(K_geo_) * Gamma_ / Lambda_);
      const double H_new = H_effective; // Use effective H for current I
      const double I_m = H_new * Gamma_ / N;
      b_m = static_cast<double>(a_m) - 2.0 * Z * I_m;
    } else {
      // Legacy magnetic-domain
      b_m = static_cast<double>(a_m) -
            2.0 * Z * Lambda_ * kMu0 * dM / (Gamma_ + kEpsilonD);
    }

    // 5. Store physical state for BHScope monitoring
    lastH_ = static_cast<float>(H_applied);
    B_tentative_ = kMu0 * (H_applied + M_new);
    lastB_ = static_cast<float>(B_tentative_);

    return static_cast<float>(b_m);
  }

  // ─── Adaptive port resistance ───────────────────────────────────────────
  float getPortResistanceImpl() const {
    const double dMdH = hystModel_.getInstantaneousSusceptibility();
    const double suscept = 1.0 + dMdH;

    if (K_geo_ > 0.0f) {
      // Electrical-domain: Z = 2 * Lm * fs = 2 * K_geo * mu0 * (1+χ) * fs
      const double mu_inc = kMu0 * std::max(suscept, kEpsilonD);
      const double Lm = K_geo_ * mu_inc;
      const double Z_e = 2.0 * Lm * sampleRate_;
      return static_cast<float>(std::clamp(Z_e, 1.0, 1e12));
    }

    // Legacy magnetic-domain: Z_m = Gamma / (Lambda * mu0 * (1 + dM/dH))
    if (std::abs(suscept) < kEpsilonD)
      return static_cast<float>(Gamma_ / (Lambda_ * kMu0 * 1.0));

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

  float  K_geo_ = 0.0f;       // Geometry scaling factor for electrical-domain WDF [m]
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
