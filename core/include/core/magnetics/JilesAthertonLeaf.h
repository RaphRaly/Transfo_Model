#pragma once

// =============================================================================
// JilesAthertonLeaf — WDF one-port nonlinear element wrapping the J-A model
//                     with Bertotti dynamic loss extension.
//
// This leaf connects the HysteresisModel (magnetic domain) to the WDF
// circuit (wave domain). Used in Physical mode for maximum accuracy.
//
// TWO MODES:
//
// (1) Legacy magnetic-domain (K_geo <= 0):
//   Wave-to-magnetic conversion:
//     H = (a_m - alpha2 * M_committed) / alpha1
//     where alpha1, alpha2 are derived from the port resistance and
//     core geometry (Gamma = magnetic path length, Lambda = cross-section).
//
// (2) Electrical-domain (K_geo > 0):
//   Uses a trapezoidal companion model with HISTORY STATE so the inductor
//   behaves as a reactive element (not a matched resistive load).
//   The outer NR residual is:
//     g(H) = N*A*(B(H) - B_prev) - (T/2)*(a - Z*(N/le)*H + V_prev)
//   This correctly couples the J-A nonlinear B(H) to the WDF scattering.
//
//   Reference: Giampiccolo/Bernardini/Sarti JAES 2021;
//              Werner CCRMA 2016 (companion model approach)
//
// Dynamic extension (Bertotti field separation):
//   H_total = H_hyst + K_eddy·dB/dt + K_exc·sign(dB/dt)·|dB/dt|^0.5
//   The dynamic terms reduce the effective field driving the static J-A
//   model, concentrating distortion at low frequencies and providing
//   high-frequency transparency — matching real transformer behavior.
//
// Port resistance (adaptive):
//   Electrical: Z = 2 * K_geo * mu0 * (1 + chi) * fs
//   Magnetic:   Z_m = Gamma / (Lambda * mu0 * (1 + dM/dH))
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
#include <algorithm>
#include <cmath>

namespace transfo {

template <typename AnhystType>
class JilesAthertonLeaf : public WDOnePort<JilesAthertonLeaf<AnhystType>> {
public:
  JilesAthertonLeaf() = default;

  // ─── Configuration ──────────────────────────────────────────────────────
  /// Configure with core geometry and optional K_geo for WDF circuit use.
  /// When K_geo > 0, port impedance and scattering use electrical-domain
  /// formulas with trapezoidal companion model (reactive behavior).
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
    // Sync Z_port_ so scatterImpl() uses the correct port resistance
    // in its companion model (not the WDOnePort default of 1.0).
    this->syncPortImpedance();
  }

  void setParameters(const JAParameterSet &params) {
    hystModel_.setParameters(params);
    dynLosses_.setCoefficients(params.K1, params.K2);
    updateCachedCoeffs();
    this->syncPortImpedance();
  }

  // ─── WDF scattering (called by WDOnePort CRTP) ─────────────────────────
  float scatterImpl(float a_m) {
    const double M_c = hystModel_.getMagnetization();
    const double Z = static_cast<double>(this->Z_port_);
    double H_applied;

    if (K_geo_ > 0.0f) {
      // ================================================================
      // Electrical-domain WDF: trapezoidal companion model
      //
      // The outer NR solves for H such that the flux-balance residual
      //   g(H) = N*A*(B_JA(H) - B_prev) - (Ts/2)*(a - Z*(N/le)*H + V_prev)
      // is zero. This correctly couples the J-A nonlinear B(H) to the
      // WDF port via history state (B_prev, V_prev), making the inductor
      // behave reactively instead of as a matched resistive load.
      //
      // Ref: Giampiccolo/Bernardini/Sarti JAES 2021 (eq. 11);
      //      Werner CCRMA 2016 (companion model approach)
      // ================================================================
      const double N_turns =
          std::sqrt(static_cast<double>(K_geo_) * Gamma_ / Lambda_);
      const double A_eff = Lambda_;
      const double le = Gamma_;
      const double Ts = 1.0 / sampleRate_;
      const double a = static_cast<double>(a_m);

      // Warm-start H from previous converged value
      double H = H_prev_e_;

      constexpr int kMaxOuterIter = 8;
      constexpr double kOuterTol = 1e-8;

      double B_new = 0.0;
      double M_new = 0.0;

      for (int iter = 0; iter < kMaxOuterIter; ++iter) {
        // 1. Compute H_effective with Bertotti dynamic losses
        double H_eff = H;
        if (dynLosses_.isEnabled()) {
          const double chi =
              std::max(0.0, hystModel_.getInstantaneousSusceptibility());
          const double B_pred = kMu0 * (H + M_c);
          const double dBdt_raw = dynLosses_.computeDBdt(B_pred);
          const double G =
              dynLosses_.getK1() * sampleRate_ * kMu0 * chi;
          const double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
          const double H_dyn = dynLosses_.computeHfromDBdt(dBdt);
          H_eff = H - H_dyn;
        }

        // 2. J-A solve: M(H_eff)
        M_new = hystModel_.solveImplicitStep(H_eff);
        B_new = kMu0 * (H + M_new);

        // 3. Trapezoidal flux balance residual
        //    g(H) = N*A*(B_new - B_prev) - (Ts/2)*(a - Z*(N/le)*H + V_prev)
        const double flux_term = N_turns * A_eff * (B_new - B_prev_e_);
        const double voltage_term =
            (Ts / 2.0) * (a - Z * (N_turns / le) * H + V_prev_e_);
        const double g = flux_term - voltage_term;

        // 4. Jacobian: g'(H) = N*A*dB/dH + (Ts/2)*Z*(N/le)
        const double dMdH = hystModel_.getInstantaneousSusceptibility();
        const double dBdH = kMu0 * (1.0 + std::max(dMdH, 0.0));
        const double g_prime =
            N_turns * A_eff * dBdH + (Ts / 2.0) * Z * (N_turns / le);

        if (std::abs(g_prime) < 1e-30)
          break;

        const double dH = -g / g_prime;
        H += std::clamp(dH, -1000.0, 1000.0); // Damped step

        // Rollback J-A tentative state for next iteration (if not converged)
        hystModel_.rollbackState();

        if (std::abs(dH) < kOuterTol * (1.0 + std::abs(H)))
          break;
      }

      // Final J-A solve with converged H
      double H_eff_final = H;
      if (dynLosses_.isEnabled()) {
        const double chi =
            std::max(0.0, hystModel_.getInstantaneousSusceptibility());
        const double B_pred = kMu0 * (H + M_c);
        const double dBdt_raw = dynLosses_.computeDBdt(B_pred);
        const double G =
            dynLosses_.getK1() * sampleRate_ * kMu0 * chi;
        const double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
        H_eff_final = H - dynLosses_.computeHfromDBdt(dBdt);
      }
      M_new = hystModel_.solveImplicitStep(H_eff_final);
      B_new = kMu0 * (H + M_new);

      // Compute reflected wave from Kirchhoff variables
      const double I_m = (N_turns / le) * H;
      const double b_m = a - 2.0 * Z * I_m;
      const double V_m = (a + b_m) / 2.0;

      // Store tentative state for commit/rollback
      H_tentative_e_ = H;
      B_tentative_e_ = B_new;
      V_tentative_e_ = V_m;

      H_applied = H; // For monitoring

      // Store physical state for BHScope monitoring
      lastH_ = static_cast<float>(H_applied);
      B_tentative_ = B_new;
      lastB_ = static_cast<float>(B_tentative_);

      return static_cast<float>(b_m);
    }

    // ==================================================================
    // Legacy magnetic-domain path (K_geo <= 0) — UNCHANGED
    // ==================================================================

    // 1. H from wave variable: H = (a_m - alpha2*M) / alpha1
    H_applied = (static_cast<double>(a_m) - alpha2_ * M_c) / alpha1_;

    // 2. Dynamic Bertotti extension
    double H_effective = H_applied;

    if (dynLosses_.isEnabled()) {
      const double chi =
          std::max(0.0, hystModel_.getInstantaneousSusceptibility());
      const double B_pred = kMu0 * (H_applied + M_c);
      const double dBdt_raw = dynLosses_.computeDBdt(B_pred);
      const double G =
          dynLosses_.getK1() * dynLosses_.getSampleRate() * kMu0 * chi;
      const double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
      const double H_dynamic = dynLosses_.computeHfromDBdt(dBdt);
      H_effective = H_applied - H_dynamic;
    }

    // 3. Solve J-A: Newton-Raphson trapezoidal
    const double M_new = hystModel_.solveImplicitStep(H_effective);

    // 4. Compute reflected wave b_m (legacy magnetic-domain)
    const double dM = M_new - M_c;
    const double b_m = static_cast<double>(a_m) -
                       2.0 * Z * Lambda_ * kMu0 * dM / (Gamma_ + kEpsilonD);

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
      // Electrical-domain: Z = 2 * Lm * fs = 2 * K_geo * mu0 * (1+chi) * fs
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
    dynLosses_.commitState(B_tentative_);
    // Commit electrical-domain trapezoidal companion state
    if (K_geo_ > 0.0f) {
      B_prev_e_ = B_tentative_e_;
      V_prev_e_ = V_tentative_e_;
      H_prev_e_ = H_tentative_e_;
    }
  }

  void rollbackState() {
    hystModel_.rollbackState();
    // Electrical-domain state stays at committed values (no rollback needed —
    // B_prev_e_, V_prev_e_, H_prev_e_ are only updated in commitState)
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
    // Reset electrical-domain trapezoidal companion state
    B_prev_e_ = 0.0;
    V_prev_e_ = 0.0;
    H_prev_e_ = 0.0;
    B_tentative_e_ = 0.0;
    V_tentative_e_ = 0.0;
    H_tentative_e_ = 0.0;
  }

private:
  HysteresisModel<AnhystType> hystModel_;
  DynamicLosses dynLosses_;

  float lastH_ = 0.0f;
  float lastB_ = 0.0f;

  float K_geo_ = 0.0f;        // Geometry scaling factor for electrical-domain WDF [m]
  double Gamma_ = 0.1;        // Magnetic path length [m]
  double Lambda_ = 1e-4;      // Cross-section area [m^2]
  double sampleRate_ = 44100.0;
  double B_tentative_ = 0.0;  // B during current HSIM iteration (for BHScope)
  double alpha1_ = 1.0;       // Cached coefficient: Gamma / (Lambda * mu0)
  double alpha2_ = 0.0;       // Cached coefficient: related to alpha feedback

  // Electrical-domain trapezoidal companion state (K_geo > 0 only)
  double B_prev_e_ = 0.0;     // B[n-1] committed
  double V_prev_e_ = 0.0;     // V[n-1] committed (port voltage)
  double H_prev_e_ = 0.0;     // H[n-1] for warm-start
  // Tentative values (for HSIM rollback)
  double B_tentative_e_ = 0.0;
  double V_tentative_e_ = 0.0;
  double H_tentative_e_ = 0.0;

  void updateCachedCoeffs() {
    alpha1_ = Gamma_ / (Lambda_ * kMu0 + kEpsilonD);
    alpha2_ = hystModel_.getParameters().alpha * alpha1_;
  }
};

} // namespace transfo
