#pragma once

// =============================================================================
// MEJunction — Magneto-Electric coupling junction for WDF transformers.
//
// This is the most innovative component of the architecture. It couples
// the electrical WDF domain to the magnetic WDF domain through the
// transformer turns ratio, implementing Faraday's law and Ampere's law
// in the wave digital domain.
//
// The ME junction converts between:
//   - Electric port: voltage V and current I (wave variables ae, be)
//   - Magnetic port: MMF F and flux rate dPhi/dt (wave variables am, bm)
//
// Coupling equations:
//   V = nt * dPhi/dt     (Faraday's law)
//   F = nt * I           (Ampere's law)
//
// In the wave domain, the ME junction includes memory elements (unit delays)
// to properly handle the coupling at discrete time steps.
//
// Port resistance adaptation: Ze = nt^2 / (Ts * Zm)
//
// [v3] No equivalent in chowdsp_wdf. References:
//   - DAFx-2015 'WDF adaptors for arbitrary topologies'
//   - Polimi thesis 'Multiphysics modeling of audio systems in WD'
// =============================================================================

#include "../util/Constants.h"
#include "../util/SmallMatrix.h"
#include <cmath>

namespace transfo {

class MEJunction {
public:
  MEJunction() = default;

  // ─── Configuration ──────────────────────────────────────────────────────
  void configure(int numTurns, float sampleRate) {
    nt_ = numTurns;
    Ts_ = 1.0f / sampleRate;
    nt2_ = static_cast<float>(nt_ * nt_);

    recomputeScatteringMatrices();
  }

  void setMagneticPortResistance(float Zm) {
    Zm_ = Zm;
    Ze_ = nt2_ / (Ts_ * Zm_);
    recomputeScatteringMatrices();
  }

  // ─── Electric port adaptation ───────────────────────────────────────────
  // Ze = nt^2 / (Ts * Zm) — adapts electric port to magnetic port
  float adaptElectricPort() const { return Ze_; }

  float getElectricPortResistance() const { return Ze_; }
  float getMagneticPortResistance() const { return Zm_; }

  // ─── Scattering: electric -> magnetic direction ─────────────────────────
  // Given incident electric wave ae, compute reflected wave + magnetic output
  float scatterElecToMag(float ae) {
    // ME scattering with memory elements
    // The scattering incorporates the unit-delay memory waves
    const float n_ratio = static_cast<float>(nt_) * Ts_;

    // Compute magnetic incident wave from electric port
    float am = n_ratio * ae + am_memory_;
    return am;
  }

  // ─── Scattering: magnetic -> electric direction ─────────────────────────
  float scatterMagToElec(float bm) {
    // Reflect magnetic wave back to electric domain
    const float inv_n_ratio = 1.0f / (static_cast<float>(nt_) * Ts_);
    float be = inv_n_ratio * bm + bm_memory_;
    return be;
  }

  // ─── Full scattering (both directions simultaneously) ───────────────────
  struct ScatterResult {
    float be; // Reflected electric wave
    float bm; // Reflected magnetic wave
  };

  ScatterResult scatterFull(float ae, float am) {
    ScatterResult result;

    // 2x2 scattering using pre-computed ME scattering matrices
    // [be]   [S_ME(0,0)  S_ME(0,1)] [ae]   [S_mem(0,0)  S_mem(0,1)] [am_prev]
    // [bm] = [S_ME(1,0)  S_ME(1,1)] [am] + [S_mem(1,0)  S_mem(1,1)] [bm_prev]

    result.be = S_ME_(0, 0) * ae + S_ME_(0, 1) * am + S_mem_(0, 0) * am_prev_ +
                S_mem_(0, 1) * bm_prev_;

    result.bm = S_ME_(1, 0) * ae + S_ME_(1, 1) * am + S_mem_(1, 0) * am_prev_ +
                S_mem_(1, 1) * bm_prev_;

    // Store current waves for next time step memory
    ae_current_ = ae;
    am_current_ = am;
    be_current_ = result.be;
    bm_current_ = result.bm;

    return result;
  }

  // ─── Commit memory (call after HSIM convergence) ────────────────────────
  void commitMemory() {
    am_prev_ = am_current_;
    bm_prev_ = bm_current_;
    am_memory_ = am_current_;
    bm_memory_ = bm_current_;
  }

  // ─── Reset ──────────────────────────────────────────────────────────────
  void reset() {
    am_prev_ = 0.0f;
    bm_prev_ = 0.0f;
    am_memory_ = 0.0f;
    bm_memory_ = 0.0f;
    ae_current_ = 0.0f;
    am_current_ = 0.0f;
    be_current_ = 0.0f;
    bm_current_ = 0.0f;
  }

  int getNumTurns() const { return nt_; }

private:
  int nt_ = 1;                 // Number of turns
  float Ts_ = 1.0f / 44100.0f; // Sampling period
  float Ze_ = 1.0f;            // Electric port resistance
  float Zm_ = 1.0f;            // Magnetic port resistance
  float nt2_ = 1.0f;           // nt^2 cached

  // Memory waves (unit delays for ME coupling)
  float am_prev_ = 0.0f;
  float bm_prev_ = 0.0f;
  float am_memory_ = 0.0f;
  float bm_memory_ = 0.0f;

  // Current waves (before commit)
  float ae_current_ = 0.0f;
  float am_current_ = 0.0f;
  float be_current_ = 0.0f;
  float bm_current_ = 0.0f;

  // Pre-computed scattering matrices
  Mat2f S_ME_;  // Instantaneous ME scattering
  Mat2f S_mem_; // Memory contribution scattering

  void recomputeScatteringMatrices() {
    // ME junction scattering matrices derived from:
    //   V = nt * dPhi/dt  and  F = nt * I
    // In wave domain with port resistances Ze and Zm:

    const float ntTs = static_cast<float>(nt_) * Ts_;
    const float den = Ze_ + nt2_ / (Ts_ * Ts_) * Zm_;

    if (std::abs(den) < kEpsilonF)
      return;

    // Simplified 2x2 scattering (derived from multi-port adaptor theory)
    const float k1 = 2.0f * Ze_ / (Ze_ + nt2_ * Zm_ / (Ts_ * Ts_) + kEpsilonF);
    const float k2 = 2.0f * nt2_ * Zm_ /
                     ((Ze_ + nt2_ * Zm_ / (Ts_ * Ts_) + kEpsilonF) * Ts_ * Ts_);

    S_ME_(0, 0) = 1.0f - k1;
    S_ME_(0, 1) = k1 * ntTs;
    S_ME_(1, 0) = k2 / ntTs;
    S_ME_(1, 1) = 1.0f - k2;

    // Memory scattering (contributions from previous time step)
    // From trapezoidal rule discretization of the coupling laws:
    // be[n] = S_ME(0,0)*ae[n] + S_ME(0,1)*am[n] + S_mem(0,0)*am[n-1] +
    // S_mem(0,1)*bm[n-1] This manages the magnetic energy storage equivalent in
    // the discrete wave domain.
    S_mem_(0, 0) = k1;
    S_mem_(0, 1) = k1 * ntTs;
    S_mem_(1, 0) = -k2 / ntTs;
    S_mem_(1, 1) = -k2;
  }
};

} // namespace transfo
