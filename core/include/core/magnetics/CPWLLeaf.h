#pragma once

// =============================================================================
// CPWLLeaf — Continuous Piecewise-Linear WDF leaf with integrated ADAA.
//
// [v3 MAJEUR] This is the key innovation for Realtime mode:
// - Replaces JilesAthertonLeaf for monitoring/playback
// - Directional: separate breakpoints for ascending/descending (hysteresis)
// - ADAA integrated in scatterImpl() — no oversampling needed
// - Passivity constraints: 0 < m_min <= m_j <= m_max [v3.1]
// - Internal scaling to +/-1 for numerical conditioning [v3.1]
//
// The CPWL approximation of the B-H hysteresis loop:
// - Each direction (ascending/descending) has its own set of breakpoints
// - Slopes m_j between breakpoints define the local permeability
// - ADAA antiderivatives F(x) and G(x) are polynomial per segment
//   → computed exactly (no numerical integration needed)
//
// Fitting pipeline (CPWLFitter):
//   1. Simulate J-A cycle → get B-H curve
//   2. Fit breakpoints for ascending + descending branches
//   3. Precompute ADAA coefficients F_j, G_j
//   4. Validate: THD CPWL vs J-A < 1 dB
//
// Performance: ~80 ns for 3 CPWL leaves with ADAA (0.4% of 44.1 kHz budget)
//
// Reference: CPWL+HSIM paper; Parker/Valimaki 2017; Polimi thesis;
//            EUSIPCO-2017 'Canonical PWL in WD Domain'
// =============================================================================

#include "../dsp/ADAAEngine.h"
#include "../util/Constants.h"
#include "../wdf/WDOnePort.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace transfo {

class CPWLLeaf : public WDOnePort<CPWLLeaf> {
public:
  static constexpr int kMaxSegments = 32;

  enum class Direction { ASCENDING, DESCENDING };

  CPWLLeaf() = default;

  float getH() const { return lastH_; }
  float getB() const { return lastB_; }

  // ─── WDF scattering with integrated ADAA [v3] ──────────────────────────
  float scatterImpl(float a_m) {
    // Apply internal scaling for conditioning [v3.1]
    const float a_scaled = a_m * internalScale_;

    // 1. Update direction state from da sign
    const float da = a_scaled - a_prev_;
    if (da > 0.0f)
      direction_ = Direction::ASCENDING;
    else if (da < 0.0f)
      direction_ = Direction::DESCENDING;
    // else: keep previous direction

    // 2. Select active breakpoint/coefficient set
    const auto &segments =
        (direction_ == Direction::ASCENDING) ? segs_asc_ : segs_desc_;
    const int numSegs =
        (direction_ == Direction::ASCENDING) ? numSegs_asc_ : numSegs_desc_;

    // 3. Find active segment for current input
    const int seg = findSegment(a_scaled, segments, numSegs);

    // 4. ADAA evaluation
    float y;
    const float dx = a_scaled - a_prev_;

    if (std::abs(dx) < kADAAEpsilon) {
      // Fallback: direct evaluation f(a_n)
      y = segments[seg].eval(a_scaled);
    } else {
      // ADAA 1st order: y = (F(a_n) - F(a_prev)) / (a_n - a_prev)
      const int seg_prev = findSegment(a_prev_, segments, numSegs);
      const float F_curr = segments[seg].evalF(a_scaled);
      const float F_prev = segments[seg_prev].evalF(a_prev_);
      y = (F_curr - F_prev) / dx;
    }

    // 5. Update state
    a_prev_ = a_scaled;

    // 6. Compute b from scattering equation
    //    For adapted element: b = 2*y - a (when Z matches slope)
    //    General: b = a - 2*Z*(f(a) mapped to current)
    const float b_m = (2.0f * y - a_scaled) / internalScale_;

    // Store physical state for BHScope monitoring
    lastH_ = a_scaled;
    lastB_ = y;

    return b_m;
  }

  // ─── Port resistance = slope of current segment ─────────────────────────
  float getPortResistanceImpl() const {
    const auto &segments =
        (direction_ == Direction::ASCENDING) ? segs_asc_ : segs_desc_;
    const int numSegs =
        (direction_ == Direction::ASCENDING) ? numSegs_asc_ : numSegs_desc_;
    const int seg = findSegment(a_prev_, segments, numSegs);

    // Port resistance proportional to inverse slope
    const float slope = segments[seg].slope;
    if (std::abs(slope) < kEpsilonF)
      return 1e6f; // Very high Z for flat segment (saturation)

    return std::clamp(1.0f / std::abs(slope), 1e-3f, 1e8f);
  }

  // ─── State management ───────────────────────────────────────────────────
  void commitState() {
    // CPWL is memoryless (no M state) — commit is a no-op for the leaf
    // Direction state is committed implicitly
  }

  void rollbackState() {
    // Nothing to rollback for CPWL
  }

  // ─── Fitting interface ──────────────────────────────────────────────────

  // Set ascending branch segments
  void setAscendingSegments(const CPWLSegmentCoeffs *segs, int count) {
    numSegs_asc_ = std::min(count, kMaxSegments);
    for (int i = 0; i < numSegs_asc_; ++i)
      segs_asc_[i] = segs[i];
  }

  // Set descending branch segments
  void setDescendingSegments(const CPWLSegmentCoeffs *segs, int count) {
    numSegs_desc_ = std::min(count, kMaxSegments);
    for (int i = 0; i < numSegs_desc_; ++i)
      segs_desc_[i] = segs[i];
  }

  // Precompute ADAA coefficients (F/G continuity constants) [v3]
  void precomputeADAACoeffs() {
    computeAntiderivativeConstants(segs_asc_.data(), numSegs_asc_);
    computeAntiderivativeConstants(segs_desc_.data(), numSegs_desc_);
  }

  // ─── Passivity assertions [v3.1] ────────────────────────────────────────

  // Verify all slopes satisfy 0 < m_min <= m_j <= m_max
  bool assertPassivity() const {
    auto checkBranch = [&](const auto &segs, int n) -> bool {
      for (int i = 0; i < n; ++i)
        if (segs[i].slope < m_min_ || segs[i].slope > m_max_)
          return false;
      return true;
    };
    return checkBranch(segs_asc_, numSegs_asc_) &&
           checkBranch(segs_desc_, numSegs_desc_);
  }

  // Check WDF feasibility for a given Z_port range
  bool assertWDFeasibility(float Z_min, float Z_max) const {
    // All slopes must produce Z_port within the feasible range
    auto checkBranch = [&](const auto &segs, int n) -> bool {
      for (int i = 0; i < n; ++i) {
        if (std::abs(segs[i].slope) < kEpsilonF)
          continue;
        float Z = 1.0f / std::abs(segs[i].slope);
        if (Z < Z_min || Z > Z_max)
          return false;
      }
      return true;
    };
    return checkBranch(segs_asc_, numSegs_asc_) &&
           checkBranch(segs_desc_, numSegs_desc_);
  }

  // ─── Configuration ──────────────────────────────────────────────────────
  void setPassivityBounds(float m_min, float m_max) {
    m_min_ = m_min;
    m_max_ = m_max;
  }

  void setInternalScale(float scale) { internalScale_ = scale; }

  void setGeometry(float Gamma, float Lambda) {
    Gamma_ = Gamma;
    Lambda_ = Lambda;
  }

  void reset() {
    a_prev_ = 0.0f;
    direction_ = Direction::ASCENDING;
  }

private:
  // ─── Segment data ───────────────────────────────────────────────────────
  std::array<CPWLSegmentCoeffs, kMaxSegments> segs_asc_{};
  std::array<CPWLSegmentCoeffs, kMaxSegments> segs_desc_{};
  int numSegs_asc_ = 1;
  int numSegs_desc_ = 1;

  // ─── State ──────────────────────────────────────────────────────────────
  float a_prev_ = 0.0f;
  Direction direction_ = Direction::ASCENDING;

  float lastH_ = 0.0f;
  float lastB_ = 0.0f;

  // ─── Passivity bounds [v3.1] ────────────────────────────────────────────
  float m_min_ = 0.001f;  // Minimum slope (avoid zero → infinite Z)
  float m_max_ = 1000.0f; // Maximum slope

  // ─── Scaling [v3.1] ─────────────────────────────────────────────────────
  float internalScale_ = 1.0f;

  // ─── Geometry ───────────────────────────────────────────────────────────
  float Gamma_ = 0.1f;
  float Lambda_ = 1e-4f;

  // ─── Helpers ────────────────────────────────────────────────────────────

  // Find which segment contains x (binary search for large N, linear for small)
  static int
  findSegment(float x, const std::array<CPWLSegmentCoeffs, kMaxSegments> &segs,
              int numSegs) {
    if (numSegs <= 1)
      return 0;

    // Linear search (fast for typical N < 16)
    for (int i = numSegs - 1; i > 0; --i)
      if (x >= segs[i].breakpoint)
        return i;
    return 0;
  }

  // Compute F and G continuity constants for ADAA
  static void computeAntiderivativeConstants(CPWLSegmentCoeffs *segs,
                                             int numSegs) {
    if (numSegs < 1)
      return;

    // First segment: F_const = 0, G_const = 0 (arbitrary reference)
    segs[0].F_const = 0.0f;
    segs[0].G_const = 0.0f;

    // Ensure C0 continuity of F and G at breakpoints
    for (int j = 1; j < numSegs; ++j) {
      const float bp = segs[j].breakpoint;

      // F must be continuous: F_{j-1}(bp) = F_j(bp)
      float F_prev_at_bp = segs[j - 1].evalF(bp);
      float F_j_at_bp_no_const =
          0.5f * segs[j].slope * bp * bp + segs[j].intercept * bp;
      segs[j].F_const = F_prev_at_bp - F_j_at_bp_no_const;

      // G must be continuous: G_{j-1}(bp) = G_j(bp)
      float G_prev_at_bp = segs[j - 1].evalG(bp);
      float G_j_at_bp_no_const = (1.0f / 6.0f) * segs[j].slope * bp * bp * bp +
                                 0.5f * segs[j].intercept * bp * bp +
                                 segs[j].F_const * bp;
      segs[j].G_const = G_prev_at_bp - G_j_at_bp_no_const;
    }
  }
};

} // namespace transfo
