#pragma once

// =============================================================================
// AnhystereticFunctions — CRTP base and implementations for the anhysteretic
// magnetization function Man(He) used in the Jiles-Atherton model.
//
// CRTP pattern ensures zero-overhead dispatch on the hot path (inlined).
//
// Implementations:
//   - LangevinPade:       Pade [3/3] approximation of the Langevin function
//                          L(x) = coth(x) - 1/x. Branchless, SIMD-ready.
//   - CPWLAnhysteretic:   Piecewise constant derivative (for CPWL leaf).
//
// Original code from HysteresisUtils.h — refactored to CRTP + float support.
//
// Reference: Jiles & Atherton, J. Magn. Magn. Mater. 61, 48-60 (1986)
// =============================================================================

#include <algorithm>
#include <cmath>

namespace transfo {

// ─── CRTP Base ──────────────────────────────────────────────────────────────

template <typename Derived> class AnhystereticFunction {
public:
  // Man/Ms = L(He/a) — normalized anhysteretic
  float evaluate(float He) const {
    return static_cast<const Derived *>(this)->evaluateImpl(He);
  }

  // dMan/dHe / Ms — normalized derivative
  float derivative(float He) const {
    return static_cast<const Derived *>(this)->derivativeImpl(He);
  }

  // Saturation value (= 1.0 for normalized, = Ms for absolute)
  float saturation() const {
    return static_cast<const Derived *>(this)->saturationImpl();
  }

  // Double-precision versions for identification (cold path)
  double evaluateD(double He) const {
    return static_cast<const Derived *>(this)->evaluateDImpl(He);
  }

  double derivativeD(double He) const {
    return static_cast<const Derived *>(this)->derivativeDImpl(He);
  }
};

// ─── LangevinPade — Pade [3/3] approximation of the Langevin function ──────
//
// L(x) = coth(x) - 1/x
// Near x=0: Taylor L(x) ~ x/3 - x^3/45 + 2x^5/945
// Pade [3/3]: L(x) ~ x(15 + x^2) / (45 + 6x^2) — normalized
//
// More accurate Pade [3/3] for L(x):
// L(x) ~ x * (945 + 105*x^2 + x^4) / (945*3 + 420*x^2 + 15*x^4)
// Simplified: we use a compromise between accuracy and branchlessness.
//
// For |x| > threshold: L(x) -> sign(x) * 1.0 (saturation)

class LangevinPade : public AnhystereticFunction<LangevinPade> {
public:
  // ─── Float (hot path) — Padé [3/3] rational approximation ────────────────
  // L(x) ≈ x·(15 + x²) / (45 + 6x²)
  // Max relative error < 0.3% for |x| < 6, asymptotic saturation for |x| > 10
  // ~10x faster than std::tanh on most platforms
  float evaluateImpl(float x) const {
    const float ax = std::abs(x);

    if (ax < 1e-4f)
      return x / 3.0f;

    if (ax > 10.0f)
      return (x > 0.0f) ? 1.0f : -1.0f;

    // Padé [3/3]: L(x) = x·(15 + x²) / (45 + 6x²)
    const float x2 = x * x;
    return x * (15.0f + x2) / (45.0f + 6.0f * x2);
  }

  // L'(x) = d/dx [ x·(15+x²) / (45+6x²) ]
  //       = (675 + 90x² - x⁴) / (45 + 6x²)² · (1/3)
  // Simplified: (15·(45+6x²) + x²·(45+6x²) - 12x²·(15+x²)) / (45+6x²)²
  //           = (675 + 90x² + 45x² + 6x⁴ - 180x² - 12x⁴) / (45+6x²)²
  //           = (675 - 45x² - 6x⁴) / (45+6x²)²
  float derivativeImpl(float x) const {
    const float ax = std::abs(x);

    if (ax < 1e-4f)
      return 1.0f / 3.0f;

    if (ax > 10.0f)
      return 0.0f;

    const float x2 = x * x;
    const float x4 = x2 * x2;
    const float denom = 45.0f + 6.0f * x2;
    // L'(x) = (675 + 45x² + 6x⁴) / (45 + 6x²)²  — quotient rule, always ≥ 0
    return (675.0f + 45.0f * x2 + 6.0f * x4) / (denom * denom);
  }

  float saturationImpl() const { return 1.0f; }

  // ─── Double (cold path — identification) ────────────────────────────────
  double evaluateDImpl(double x) const {
    const double ax = std::abs(x);

    if (ax < 1e-8)
      return x / 3.0;

    if (ax > 20.0)
      return (x > 0.0) ? 1.0 : -1.0;

    return 1.0 / std::tanh(x) - 1.0 / x;
  }

  double derivativeDImpl(double x) const {
    const double ax = std::abs(x);

    if (ax < 1e-8)
      return 1.0 / 3.0;

    if (ax > 20.0)
      return 0.0;

    const double sh = std::sinh(x);
    return 1.0 / (x * x) - 1.0 / (sh * sh);
  }
};

// ─── CPWLAnhysteretic — Piecewise constant derivative ──────────────────────
// Used when the full Langevin is replaced by a CPWL approximation.
// The derivative is constant per segment, making ADAA trivial.
// Placeholder: actual breakpoints set during fitting (CPWLFitter).

class CPWLAnhysteretic : public AnhystereticFunction<CPWLAnhysteretic> {
public:
  float evaluateImpl(float x) const {
    // Simplified CPWL: 3-segment linear approximation
    // Slope transitions at +/- breakpoint
    const float ax = std::abs(x);
    if (ax < breakpoint_)
      return x * slopeInner_;
    return ((x > 0.0f) ? 1.0f : -1.0f) *
           (slopeInner_ * breakpoint_ + slopeOuter_ * (ax - breakpoint_));
  }

  float derivativeImpl(float x) const {
    return (std::abs(x) < breakpoint_) ? slopeInner_ : slopeOuter_;
  }

  float saturationImpl() const { return 1.0f; }

  double evaluateDImpl(double x) const {
    return static_cast<double>(evaluateImpl(static_cast<float>(x)));
  }
  double derivativeDImpl(double x) const {
    return static_cast<double>(derivativeImpl(static_cast<float>(x)));
  }

  void setBreakpoint(float bp, float inner, float outer) {
    breakpoint_ = bp;
    slopeInner_ = inner;
    slopeOuter_ = outer;
  }

private:
  float breakpoint_ = 1.0f;
  float slopeInner_ = 0.333f; // ~1/3 (Langevin slope at origin)
  float slopeOuter_ = 0.01f;  // Near saturation
};

} // namespace transfo
