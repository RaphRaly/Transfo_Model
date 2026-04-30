#pragma once

// =============================================================================
// Constants — SI and numerical constants for the transformer model.
//
// All physical constants in SI units.
// Numerical epsilons calibrated for single-precision WDF hot path
// and double-precision identification cold path.
// =============================================================================

#include <cmath>
#include <cstdint>

namespace transfo {

// ─── SI Constants ───────────────────────────────────────────────────────────

constexpr double kMu0          = 1.2566370614359173e-6;  // Permeability of free space (H/m)
constexpr double kPi           = 3.14159265358979323846;
constexpr double kTwoPi        = 2.0 * kPi;
constexpr float  kMu0f         = 1.2566371e-6f;
constexpr float  kPif          = 3.14159265f;
constexpr float  kTwoPif       = 2.0f * kPif;

// ─── Numerical Tolerances ───────────────────────────────────────────────────

constexpr float  kEpsilonF     = 1e-7f;       // Float near-zero threshold
constexpr double kEpsilonD     = 1e-12;        // Double near-zero threshold
constexpr float  kADAAEpsilon  = 1e-5f;        // ADAA fallback threshold |dx|
constexpr int    kMaxNRIter    = 20;            // Max Newton-Raphson iterations (WDF nonlinear leaves)
constexpr int    kDefaultAdaptationInterval = 16; // Z adaptation interval (samples)

// ─── Audio Constants ────────────────────────────────────────────────────────

constexpr float  kDefaultSampleRate = 44100.0f;
constexpr int    kOversamplingArtistic = 4;     // Default OS factor for Artistic mode
[[deprecated("Use kOversamplingArtistic")]]
constexpr int    kOversamplingPhysical = kOversamplingArtistic; // deprecated compatibility alias
constexpr int    kMaxBlockSize  = 2048;

// ─── dBu / dBv Conversion ───────────────────────────────────────────────────

constexpr double kDBuRefVrms   = 0.7746;        // 0 dBu = 0.7746 Vrms
constexpr double kDBvRefVrms   = 1.0;            // 0 dBv = 1.0 Vrms

// ─── Inline Helpers ─────────────────────────────────────────────────────────

inline float dBtoLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }
inline float linearTodB(float lin) { return 20.0f * std::log10(lin + kEpsilonF); }

/// Differentiable soft saturation: maps (-inf,+inf) to (-limit, +limit).
/// Replaces std::clamp inside implicit Newton solves where the Jacobian
/// must remain non-zero (hard clamp has zero derivative at the rails).
inline float softSat(float x, float limit)
{
    return limit * std::tanh(x / limit);
}

/// Derivative of softSat: d/dx [L * tanh(x/L)] = 1 - tanh^2(x/L).
inline float softSatDeriv(float x, float limit)
{
    const float t = std::tanh(x / limit);
    return 1.0f - t * t;
}

/// Bilinear frequency prewarp: maps an analog cutoff frequency fHz so that
/// the BLT-discretized filter matches the analog response exactly at fHz.
/// Without prewarping, the bilinear transform compresses frequencies toward
/// Nyquist, causing sample-rate-dependent filter behaviour.
///
///   fc_warped = (fs / pi) * tan(pi * fHz / fs)
///
/// For fHz << fs, fc_warped ~ fHz (negligible correction).
/// For fHz near Nyquist, the correction is significant.
inline float prewarpHz(float fHz, float fs)
{
    return (fs / kPif) * std::tan(kPif * fHz / fs);
}

} // namespace transfo
