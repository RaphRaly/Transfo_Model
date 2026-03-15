#pragma once

// =============================================================================
// HSIMSolver — Hybrid Scattering-Impedance Method solver for WDF circuits
//              with multiple nonlinear elements.
//
// The HSIM solver handles the transformer WDF circuit containing:
//   - 3 nonlinear reluctances (magnetic core legs) — NonlinearLeaf
//   - 6 linear elements (resistors, capacitors, inductors)
//   - 3 ME junctions (one per winding: primary + 2 secondary halves)
//   - 1 magnetic topological junction (9-port for 3-legged core)
//   - Electric topological junctions
//
// Algorithm per sample:
//   1. Check adaptation counter → update Z if needed (every 16 samples)
//   2. HSIM iteration loop (max 20 iterations):
//      a. Nonlinear leaf scattering (3 reluctances, SIMD-parallel)
//      b. Linear leaf scattering (trivial: b = state)
//      c. Forward scan (leaves → root junction via ME junctions)
//      d. Root junction scattering (9x9 S * a, optionally SIMD)
//      e. Backward scan (root → leaves via ME junctions)
//      f. Convergence check (progressive, with ConvergenceGuard)
//   3. Commit states (M[k], ME memory waves)
//   4. ConvergenceGuard::getSafeOutput()
//   5. [DEBUG] HSIMDiagnostics::computeSpectralRadius()
//
// Template parameters:
//   NonlinearLeaf : CPWLLeaf (Realtime) or JilesAthertonLeaf (Physical)
//   NumNL         : Number of nonlinear elements (typically 3)
//   NumLinear     : Number of linear elements
//   NumME         : Number of ME junctions (windings)
//   NumMagPorts   : Total magnetic junction ports
//
// Reference: chowdsp_wdf arXiv:2210.12554; Werner thesis Stanford
// =============================================================================

#include "../util/AlignedBuffer.h"
#include "../util/Constants.h"
#include "ConvergenceGuard.h"
#include "HSIMDiagnostics.h"
#include "MEJunction.h"
#include "TopologicalJunction.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace transfo {

template <typename NonlinearLeaf, int NumNL = 3, int NumLinear = 6,
          int NumME = 3, int NumMagPorts = 9>
class HSIMSolver {
public:
  HSIMSolver() = default;

  // ─── Setup ──────────────────────────────────────────────────────────────
  void prepareToPlay(float sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;

    convergenceGuard_.configure(epsilon_);
    convergenceGuard_.reset();

    adaptationCounter_ = 0;

    // Reset all elements
    for (int i = 0; i < NumNL; ++i)
      nonlinearLeaves_[i] = NonlinearLeaf();

    for (int i = 0; i < NumME; ++i)
      meJunctions_[i].reset();

    // Clear wave buffers
    a_nl_.clear();
    b_nl_.clear();
    a_mag_.clear();
    b_mag_.clear();
  }

  // ─── Access nonlinear leaves (for configuration) ────────────────────────
  NonlinearLeaf &getNonlinearLeaf(int index) { return nonlinearLeaves_[index]; }
  const NonlinearLeaf &getNonlinearLeaf(int index) const {
    return nonlinearLeaves_[index];
  }

  // ─── Access ME junctions ────────────────────────────────────────────────
  MEJunction &getMEJunction(int index) { return meJunctions_[index]; }

  // ─── Component Counts ───────────────────────────────────────────────────
  int getNumNonlinearLeaves() const { return NumNL; }
  int getNumMEJunctions() const { return NumME; }

  // ─── Access magnetic junction ───────────────────────────────────────────
  TopologicalJunction<NumMagPorts> &getMagneticJunction() {
    return magJunction_;
  }

  // ─── Main processing: one sample ────────────────────────────────────────
  float processSample(float inputVoltage) {
    // [1] ADAPTATION Z — conditional, every adaptationInterval_ samples
    adaptationCounter_++;
    if (adaptationCounter_ >= adaptationInterval_) {
      adaptationCounter_ = 0;
      updatePortResistances();
    }

    // Set input voltage on source element
    inputVoltage_ = inputVoltage;

    // [2] HSIM ITERATION
    bool converged = false;
    int iterCount = 0;

#ifndef NDEBUG
    // Store previous iteration vectors for diagnostics
    AlignedArray<float, NumNL> v_prev_prev;
    AlignedArray<float, NumNL> v_prev;
    v_prev.clear();
    v_prev_prev.clear();
#endif

    for (int g = 0; g < maxIterations_; ++g) {
      iterCount = g + 1;

#ifndef NDEBUG
      // Save previous NL port waves for spectral radius
      v_prev_prev = v_prev;
      for (int i = 0; i < NumNL; ++i)
        v_prev[i] = b_nl_[i];
#endif

      // [2a] NONLINEAR LEAF SCATTERING (3 reluctances)
      for (int i = 0; i < NumNL; ++i)
        b_nl_[i] = nonlinearLeaves_[i].scatter(a_nl_[i]);

      // [2a'] LINEAR LEAF SCATTERING
      // Linear leaves: b = stored state (O(1) per element)
      // Handled implicitly by the adapted elements

      // [2b] FORWARD SCAN (leaves → root via ME junctions)
      forwardScan();

      // [2c] ROOT SCATTERING (magnetic junction: NumMagPorts x NumMagPorts S*a)
      magJunction_.scatter(a_mag_.data(), b_mag_.data());

      // [2d] BACKWARD SCAN (root → leaves via ME junctions)
      backwardScan();

      // [2e] CONVERGENCE CHECK
      float maxDiff = 0.0f;
      for (int i = 0; i < NumNL; ++i) {
        float diff = std::abs(b_nl_[i] - a_nl_prev_[i]);
        maxDiff = std::max(maxDiff, diff);
      }

      // Store current incident waves for next iteration comparison
      for (int i = 0; i < NumNL; ++i)
        a_nl_prev_[i] = a_nl_[i];

      if (maxDiff < convergenceGuard_.getAdaptiveEpsilon()) {
        converged = true;
        break;
      }
    }

    // [3] COMMIT STATES (M[k], ME memory)
    for (int i = 0; i < NumNL; ++i)
      nonlinearLeaves_[i].commitState();

    for (int i = 0; i < NumME; ++i)
      meJunctions_[i].commitMemory();

    // [4] Extract output and apply ConvergenceGuard
    float rawOutput = extractOutput();
    float safeOutput = convergenceGuard_.getSafeOutput(rawOutput, converged);

    // [5] [DEBUG] Spectral radius diagnostic
#ifndef NDEBUG
    if (iterCount > 1) {
      diagnostics_.computeSpectralRadius(b_nl_.data(), v_prev.data(),
                                         v_prev_prev.data());
    }
#endif

    lastIterCount_ = iterCount;
    lastConverged_ = converged;

    return safeOutput;
  }

  // ─── Configuration ──────────────────────────────────────────────────────
  void setEpsilon(float eps) {
    epsilon_ = eps;
    convergenceGuard_.configure(eps);
  }
  void setMaxIterations(int n) { maxIterations_ = n; }
  void setAdaptationInterval(int n) { adaptationInterval_ = n; }

  // ─── Monitoring ─────────────────────────────────────────────────────────
  int getLastIterationCount() const { return lastIterCount_; }
  bool getLastConverged() const { return lastConverged_; }
  const ConvergenceGuard &getConvergenceGuard() const {
    return convergenceGuard_;
  }

  const HSIMDiagnostics<NumNL> &getDiagnostics() const { return diagnostics_; }

  void reset() {
    convergenceGuard_.reset();
    adaptationCounter_ = 0;
    a_nl_.clear();
    b_nl_.clear();
    a_nl_prev_.clear();
    a_mag_.clear();
    b_mag_.clear();
    inputVoltage_ = 0.0f;
    lastIterCount_ = 0;
    lastConverged_ = true;

    for (int i = 0; i < NumNL; ++i)
      nonlinearLeaves_[i].reset();

    for (int i = 0; i < NumME; ++i)
      meJunctions_[i].reset();
  }

private:
  // ─── Circuit elements ───────────────────────────────────────────────────
  TopologicalJunction<NumMagPorts> magJunction_;
  std::array<MEJunction, NumME> meJunctions_;
  std::array<NonlinearLeaf, NumNL> nonlinearLeaves_;

  // ─── Wave variable buffers ──────────────────────────────────────────────
  AlignedArray<float, NumNL> a_nl_{};        // Incident waves to NL leaves
  AlignedArray<float, NumNL> b_nl_{};        // Reflected waves from NL leaves
  AlignedArray<float, NumNL> a_nl_prev_{};   // Previous iteration (convergence)
  AlignedArray<float, NumMagPorts> a_mag_{}; // Magnetic junction incident
  AlignedArray<float, NumMagPorts> b_mag_{}; // Magnetic junction reflected

  // ─── Solver parameters ──────────────────────────────────────────────────
  float epsilon_ = 1e-5f;
  int maxIterations_ = kMaxNRIter;
  int adaptationInterval_ = kDefaultAdaptationInterval;
  int adaptationCounter_ = 0;
  float sampleRate_ = kDefaultSampleRate;
  float inputVoltage_ = 0.0f;

  // ─── Diagnostics ────────────────────────────────────────────────────────
  ConvergenceGuard convergenceGuard_;
  HSIMDiagnostics<NumNL> diagnostics_;
  int lastIterCount_ = 0;
  bool lastConverged_ = true;

  // ─── Forward scan: propagate waves from leaves toward root ──────────────
  void forwardScan() {
    // Each ME junction receives waves from its electrical and magnetic sides
    // and propagates toward the magnetic root junction.
    //
    // The exact topology depends on the transformer configuration.
    // For the standard 3-legged core (Jensen JT-115K-E):
    //   ME[0] connects primary winding to magnetic leg 0
    //   ME[1] connects secondary winding half 1 to magnetic leg 1
    //   ME[2] connects secondary winding half 2 to magnetic leg 2
    //
    // Forward: NL leaf b → ME magnetic input → magnetic junction a

    for (int i = 0; i < std::min(NumNL, NumME); ++i) {
      // NL leaf reflected wave → magnetic side of ME junction
      auto result = meJunctions_[i].scatterFull(inputVoltage_, b_nl_[i]);

      // ME junction output → magnetic junction input port
      if (i < NumMagPorts)
        a_mag_[i] = result.bm;
    }
  }

  // ─── Backward scan: propagate waves from root back to leaves ────────────
  void backwardScan() {
    // Root junction reflected waves → ME junctions → NL leaf incident waves

    for (int i = 0; i < std::min(NumNL, NumME); ++i) {
      // Magnetic junction reflected wave → NL leaf incident
      if (i < NumMagPorts)
        a_nl_[i] = b_mag_[i];
    }
  }

  // ─── Update port resistances (called every adaptationInterval_ samples) ─
  void updatePortResistances() {
    for (int i = 0; i < NumNL; ++i) {
      float Z_new = nonlinearLeaves_[i].getPortResistance();
      float Z_old = magJunction_.getPortResistance(i);

      if (std::abs(Z_new - Z_old) > kEpsilonF) {
        magJunction_.updateRank1(i, Z_old, Z_new);
      }
    }
  }

  // ─── Extract output voltage from the circuit ────────────────────────────
  float extractOutput() {
    // Output is taken from the secondary winding's electrical port
    // For now: use ME junction [1] electric port (secondary)
    if (NumME > 1) {
      // Output voltage = Kirchhoff voltage at secondary electric port
      // V = (a + b) / 2 from the secondary ME junction
      return (a_mag_[1] + b_mag_[1]) * 0.5f;
    }

    return b_mag_[0]; // Fallback: first port reflected wave
  }
};

} // namespace transfo
