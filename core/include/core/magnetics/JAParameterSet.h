#pragma once

// =============================================================================
// JAParameterSet — Jiles-Atherton material parameter set.
//
// Extracted from HysteresisProcessor. Encapsulates the 7 J-A parameters
// with validation, bounds per material family, and log-space conversion
// for CMA-ES optimization [v3].
//
// Physical parameters:
//   Ms    : Saturation magnetization [A/m]
//   a     : Anhysteretic shape (thermal parameter) [A/m]
//   alpha : Inter-domain coupling [-]
//   k     : Pinning coefficient (coercivity) [A/m]
//   c     : Reversibility ratio [0,1]
//   K1    : Classical eddy current loss coefficient
//   K2    : Excess (anomalous) loss coefficient
//
// STABILITY CONDITION: k > alpha * Ms (MANDATORY)
//
// References: Jiles & Atherton 1986, Magnetic Shields Ltd data,
//             Sun et al. 2023, Szewczyk bounds.
// =============================================================================

#include "../util/Constants.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace transfo {

// ─── Material Family (for CMA-ES bounds) [v3] ──────────────────────────────
enum class MaterialFamily {
  MuMetal_80NiFe, // Jensen, Lundahl — Alloy 4 (ASTM A753)
  NiFe_50,        // Neve Marinair — Alloy 2 (ASTM A753)
  GO_SiFe,        // Grain-oriented silicon-iron (API, etc.)
  Custom          // User-defined bounds
};

// ─── CMA-ES Bounds ──────────────────────────────────────────────────────────
struct JABounds {
  float Ms_min, Ms_max;
  float a_min, a_max;
  float k_min, k_max;
  float alpha_min, alpha_max;
  float c_min, c_max;
};

// ─── JAParameterSet ─────────────────────────────────────────────────────────
struct JAParameterSet {
  // ── Core J-A parameters ─────────────────────────────────────────────────
  float Ms = 5.5e5f;   // Saturation magnetization [A/m]
  float a = 30.0f;     // Anhysteretic shape [A/m]
  float alpha = 1e-4f; // Inter-domain coupling [-]
  float k = 50.0f;     // Pinning coefficient [A/m]
  float c = 0.85f;     // Reversibility ratio [0,1]

  // ── Dynamic loss parameters ─────────────────────────────────────────────
  float K1 = 0.0f; // Classical eddy current loss coefficient
  float K2 = 0.0f; // Excess (anomalous) loss coefficient

  // ── Validation ──────────────────────────────────────────────────────────
  bool isPhysicallyValid() const {
    // 1. All positive
    if (Ms <= 0.0f || a <= 0.0f || k <= 0.0f || alpha < 0.0f)
      return false;

    // 2. Reversibility in [0, 1]
    if (c < 0.0f || c > 1.0f)
      return false;

    // 3. Stability condition: k > alpha * Ms
    if (k <= alpha * Ms)
      return false;

    // 4. Dynamic losses non-negative
    if (K1 < 0.0f || K2 < 0.0f)
      return false;

    return true;
  }

  JAParameterSet clampToValid() const {
    JAParameterSet p = *this;
    p.Ms = std::max(1e3f, p.Ms);
    p.a = std::max(1.0f, p.a);
    p.k = std::max(1.0f, p.k);
    p.alpha = std::clamp(p.alpha, 0.0f, 0.01f);
    p.c = std::clamp(p.c, 0.0f, 1.0f);
    p.K1 = std::max(0.0f, p.K1);
    p.K2 = std::max(0.0f, p.K2);

    // Enforce stability: k > alpha * Ms
    if (p.k <= p.alpha * p.Ms)
      p.k = p.alpha * p.Ms * 1.1f;

    return p;
  }

  // ── CMA-ES Bounds by Material Family [v3] ──────────────────────────────
  static JABounds getDefaultBounds(MaterialFamily family) {
    switch (family) {
    case MaterialFamily::MuMetal_80NiFe:
      return {4.5e5f, 7.0e5f, // Ms
              5.0f,   60.0f,  // a
              10.0f,  200.0f, // k
              1e-6f,  5e-4f,  // alpha
              0.70f,  0.98f}; // c

    case MaterialFamily::NiFe_50:
      return {6.0e5f, 9.0e5f, // Ms
              20.0f,  200.0f, // a
              50.0f,  500.0f, // k
              5e-6f,  1e-3f,  // alpha
              0.50f,  0.90f}; // c

    case MaterialFamily::GO_SiFe:
      return {1.2e6f, 1.8e6f,  // Ms
              50.0f,  500.0f,  // a
              200.0f, 2000.0f, // k
              1e-5f,  5e-3f,   // alpha
              0.20f,  0.80f};  // c

    case MaterialFamily::Custom:
    default:
      return {1e4f,    2e6f,  1.0f,  1000.0f, 1.0f,
              5000.0f, 1e-7f, 1e-1f, 0.01f,   0.99f};
    }
  }

  // ── Log-space conversion for CMA-ES [v3] ───────────────────────────────
  // Optimizes on log(Ms), log(a), log(k) to guarantee positivity.
  // c is mapped via sigmoid: c_tilde = log(c / (1-c))
  static constexpr int kNumOptParams = 5;

  std::array<float, kNumOptParams> toLogSpace() const {
    return {
        std::log(Ms), std::log(a), std::log(k), std::log(alpha + 1e-10f),
        std::log(c / (1.0f - std::clamp(c, 0.01f, 0.99f))) // sigmoid^-1
    };
  }

  static JAParameterSet
  fromLogSpace(const std::array<float, kNumOptParams> &logParams) {
    JAParameterSet p;
    p.Ms = std::exp(logParams[0]);
    p.a = std::exp(logParams[1]);
    p.k = std::exp(logParams[2]);
    p.alpha = std::exp(logParams[3]);
    p.c = 1.0f / (1.0f + std::exp(-logParams[4])); // sigmoid
    return p;
  }

  // ── Default presets ─────────────────────────────────────────────────────
  static JAParameterSet defaultMuMetal() {
    // Ms=5.5e5, alpha=1e-4 → alpha*Ms=55. k must be > 55 → use k=100
    return {5.5e5f, 30.0f, 1e-4f, 100.0f, 0.85f, 0.0f, 0.0f};
  }

  static JAParameterSet defaultNiFe50() {
    // Ms=7.5e5, alpha=5e-4 → alpha*Ms=375. k must be > 375 → use k=500
    return {7.5e5f, 80.0f, 5e-4f, 500.0f, 0.70f, 0.0f, 0.0f};
  }

  static JAParameterSet defaultSiFe() {
    // Si-Fe placeholder (from original HysteresisProcessor)
    return {1.2e6f, 80.0f, 1e-4f, 200.0f, 0.10f, 0.0f, 0.0f};
  }
};

} // namespace transfo
