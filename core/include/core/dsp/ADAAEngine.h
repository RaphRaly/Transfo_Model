#pragma once

// =============================================================================
// ADAAEngine — Antiderivative Antialiasing for nonlinear WDF elements.
//
// [v3 NOUVEAU] Replaces oversampling in Realtime mode.
//
// ADAA uses antiderivatives of the nonlinear function to suppress aliasing
// without oversampling. For CPWL (continuous piecewise-linear) functions,
// the antiderivatives are polynomial per segment → exact, no approximation.
//
// 1st order ADAA:
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
//   where F(x) = integral of f(x)
//
// 2nd order ADAA (Parker eq.13):
//   Uses F and G (second antiderivative) for steeper alias suppression.
//
// Fallback when |x[n] - x[n-1]| < epsilon: y[n] = f(x[n]) (direct eval)
//
// Cost: 1 mul + 1 div extra per sample (1st order). No OS = huge CPU saving.
//
// Reference: Parker/Valimaki IEEE SPL 2017;
//            DAFx-2020 'ADAA in Nonlinear WDF'
// =============================================================================

#include "../util/Constants.h"
#include <cmath>

namespace transfo {

class ADAAEngine {
public:
  ADAAEngine() = default;

  void setOrder(int order) { order_ = order; }
  void setEpsilon(float eps) { epsilon_ = eps; }

  // ─── 1st Order ADAA ─────────────────────────────────────────────────────
  // F_xn, F_xnm1: antiderivative values at x[n] and x[n-1]
  // xn, xnm1: current and previous input
  // f_xn: direct function evaluation at x[n] (fallback)
  static inline float evaluate1stOrder(float F_xn, float F_xnm1, float xn,
                                       float xnm1, float f_xn) {
    const float dx = xn - xnm1;

    if (std::abs(dx) < kADAAEpsilon)
      return f_xn; // Fallback to direct evaluation

    return (F_xn - F_xnm1) / dx;
  }

  // ─── 2nd Order ADAA (Parker/Valimaki IEEE SPL 2017) ──────────────────────
  // The full recursive form requires storing the previous output:
  //   y[n] = 2·(G(xn) - G(xnm1))/dx² - y[n-1]
  //
  // This stateless API approximates it by using the 1st-order result (D1)
  // as the "previous estimate", giving:
  //   y = 2·(G(xn) - G(xnm1))/dx² - D1
  //
  // For CPWL (piecewise-linear), 1st order ADAA is already exact (no aliasing
  // at segment level). The 2nd order term adds correction for inter-segment
  // transitions. Use CPWLLeaf's internal state for true recursive 2nd order.
  static inline float evaluate2ndOrder(float F_xn, float F_xnm1, float G_xn,
                                       float G_xnm1, float xn, float xnm1,
                                       float f_xn) {
    const float dx = xn - xnm1;

    if (std::abs(dx) < kADAAEpsilon)
      return f_xn;

    // 1st order ADAA (exact for linear, good alias suppression for CPWL)
    const float D1 = (F_xn - F_xnm1) / dx;

    if (std::abs(dx) < 10.0f * kADAAEpsilon)
      return D1;

    // 2nd order correction term: Δ = 2·(G(xn)-G(xnm1))/dx² - 2·D1
    // Blended with D1 to stay near physically meaningful range.
    // Full 2nd order: use CPWLLeaf's recursive state for proper result.
    const float dx2 = dx * dx;
    const float G_term = 2.0f * (G_xn - G_xnm1) / dx2;

    // Weighted blend to approximate stateless 2nd order
    return 0.5f * (D1 + G_term);
  }

  int getOrder() const { return order_; }

private:
  int order_ = 1; // 1 or 2
  float epsilon_ = kADAAEpsilon;
};

// ─── CPWL Antiderivative Helpers ────────────────────────────────────────────
// For a CPWL segment f(x) = m_j * x + b_j on [break_j, break_{j+1}]:
//   F(x) = (1/2) * m_j * x^2 + b_j * x + c_j
//   G(x) = (1/6) * m_j * x^3 + (1/2) * b_j * x^2 + c_j * x + d_j
//
// c_j and d_j ensure C0 continuity of F and G at breakpoints.

struct CPWLSegmentCoeffs {
  float slope;      // m_j — slope of f(x) in this segment
  float intercept;  // b_j — intercept of f(x) in this segment
  float breakpoint; // x value where this segment starts

  // Antiderivative constants (computed for continuity)
  float F_const; // c_j for F(x)
  float G_const; // d_j for G(x)

  // Evaluate f(x) in this segment
  float eval(float x) const { return slope * x + intercept; }

  // Evaluate F(x) = integral of f(x)
  float evalF(float x) const {
    return 0.5f * slope * x * x + intercept * x + F_const;
  }

  // Evaluate G(x) = integral of F(x)
  float evalG(float x) const {
    const float x2 = x * x;
    const float x3 = x2 * x;
    return (1.0f / 6.0f) * slope * x3 + 0.5f * intercept * x2 + F_const * x +
           G_const;
  }
};

} // namespace transfo
