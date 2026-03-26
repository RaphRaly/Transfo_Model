// =============================================================================
// test_common.h -- Shared test infrastructure for TWISTERION test suite
//
// Eliminates per-file duplication of:
//   - Pass/fail counters (g_pass, g_fail)
//   - Assertion macros (CHECK, CHECK_NEAR, CHECK_RANGE, TEST_ASSERT)
//   - Goertzel single-bin DFT
//   - THD measurement (with per-harmonic dB breakdown)
//   - RMS measurement
//   - dBFS / amplitude conversion helpers
//   - Test summary printing
//
// Usage:
//   #include "test_common.h"
//
//   int main() {
//       CHECK(true, "basic true");
//       CHECK_NEAR(3.14, 3.14159, 0.01, "pi approximation");
//       CHECK_RANGE(0.5, 0.0, 1.0, "unit range");
//       return test::printSummary("MyTest");
//   }
//
// All functions are header-only inline. Counters use Meyer's-singleton
// accessors for safe cross-TU sharing without requiring C++17 inline
// variables (compatible with MSVC 2019 /std:c++14 and above).
//
// No external dependencies beyond <cmath>, <cstdio>, <string>, <vector>,
// <algorithm>.
//
// Backward compatibility:
//   - CHECK / CHECK_NEAR / CHECK_RANGE / TEST_ASSERT are file-scope macros,
//     so existing call sites work without a test:: qualifier.
//   - goertzelMagnitude, computeTHD, computeRMS live inside namespace test
//     but can be pulled in with `using namespace test;`.
//   - The old function-style CHECK(bool, const char*) calls compile through
//     the macro, which forwards to test::check().
//   - printSummary returns int (0 = pass, 1 = fail) for use as main()'s
//     return value.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace test {

// ── Mathematical constants ─────────────────────────────────────────────────
static constexpr double kPi = 3.14159265358979323846;

// Legacy alias used by many existing test files.
static constexpr double PI = kPi;

// ── Global counters ────────────────────────────────────────────────────────
// Meyer's singleton accessors -- safe in pre-C++17 headers included from
// multiple TUs. Each test executable links to a single copy.

inline int& g_pass() { static int v = 0; return v; }
inline int& g_fail() { static int v = 0; return v; }

// ── Core assertion helpers (implementation) ────────────────────────────────

/// Boolean assertion with source location.
inline void check(bool condition, const char* msg,
                  const char* file, int line) {
    if (condition) {
        ++g_pass();
    } else {
        ++g_fail();
        std::printf("  FAIL [%s:%d]: %s\n", file, line, msg);
    }
}

/// Near-equality assertion for floating-point values.
inline void checkNear(double val, double expected, double tol,
                      const char* msg, const char* file, int line) {
    double err = std::abs(val - expected);
    if (err <= tol) {
        ++g_pass();
    } else {
        ++g_fail();
        std::printf("  FAIL [%s:%d]: %s "
                    "(got %.6g, expected %.6g +/-%.6g, err=%.6g)\n",
                    file, line, msg, val, expected, tol, err);
    }
}

/// Range assertion: val must be in [lo, hi].
inline void checkRange(double val, double lo, double hi,
                       const char* msg, const char* file, int line) {
    bool ok = (val >= lo && val <= hi);
    if (ok) {
        ++g_pass();
    } else {
        ++g_fail();
        std::printf("  FAIL [%s:%d]: %s "
                    "(got %.6g, expected [%.6g, %.6g])\n",
                    file, line, msg, val, lo, hi);
    }
}

// ── Test summary ───────────────────────────────────────────────────────────
// Call at the end of main(). Returns 0 on all-pass, 1 on any failure --
// suitable as main()'s return value.

inline int printSummary(const char* testName) {
    std::printf("\n=== %s: %d passed, %d failed ===\n",
                testName, g_pass(), g_fail());
    return (g_fail() > 0) ? 1 : 0;
}

// Overload with no argument for files that call printSummary() bare.
inline int printSummary() {
    return printSummary("Test Suite");
}

// ── Goertzel single-bin DFT ────────────────────────────────────────────────
// Returns the magnitude of frequency bin `freq` in signal `data` of length
// `N` sampled at `fs`.
//
// Scaling: 2 * |X[k]| / N  (peak amplitude of the sinusoidal component).
// This matches the convention used across the existing test suite (see e.g.
// test_neve_validation.cpp, test_output_stage.cpp, test_je990_path.cpp).

inline double goertzelMagnitude(const float* data, int N,
                                double freq, double fs) {
    const double k     = freq * static_cast<double>(N) / fs;
    const double w     = 2.0 * kPi * k / static_cast<double>(N);
    const double coeff = 2.0 * std::cos(w);

    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < N; ++i) {
        s0 = static_cast<double>(data[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    // Goertzel power: |X[k]|^2 = s1^2 + s2^2 - coeff*s1*s2
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt((std::max)(power, 0.0)) * 2.0 / static_cast<double>(N);
}

// Note: float arguments implicitly convert to double via the overload above.
// A separate float overload was removed to avoid MSVC C2666 ambiguity when
// call sites mix float and double arguments.

// ── THD measurement ────────────────────────────────────────────────────────

struct THDResult {
    double thdPercent     = 0.0;       ///< Total harmonic distortion in %
    double fundamentalDB  = -999.0;    ///< H1 level in dB (re: peak magnitude)
    double h1Mag          = 0.0;       ///< H1 linear magnitude (legacy compat)
    double harmonicMag[10] = {};       ///< H1..H10 linear magnitudes
    double harmonicDB[10]  = {         ///< H1..H10 levels in dB
        -999.0, -999.0, -999.0, -999.0, -999.0,
        -999.0, -999.0, -999.0, -999.0, -999.0
    };
};

/// Measure THD of `data` (length `N`) at fundamental `f0`, sample rate `fs`.
/// Examines harmonics H2 through H(`nHarmonics`+1).  Default nHarmonics=8
/// gives H2..H9 (matching the existing test_neve_validation convention).
inline THDResult measureTHD(const float* data, int N,
                            double f0, double fs,
                            int nHarmonics = 8) {
    THDResult r;
    double H1 = goertzelMagnitude(data, N, f0, fs);
    r.h1Mag = H1;
    r.harmonicMag[0] = H1;
    r.fundamentalDB = (H1 > 1e-30) ? 20.0 * std::log10(H1) : -999.0;
    r.harmonicDB[0] = r.fundamentalDB;

    double sumHk2 = 0.0;
    for (int k = 2; k <= nHarmonics + 1 && k <= 10; ++k) {
        double freq_k = f0 * static_cast<double>(k);
        if (freq_k >= fs / 2.0) break;   // respect Nyquist

        double Hk = goertzelMagnitude(data, N, freq_k, fs);
        r.harmonicMag[k - 1] = Hk;
        r.harmonicDB[k - 1] = (Hk > 1e-30) ? 20.0 * std::log10(Hk) : -999.0;
        sumHk2 += Hk * Hk;
    }

    r.thdPercent = (H1 > 1e-30) ? std::sqrt(sumHk2) / H1 * 100.0 : 0.0;
    return r;
}

// Note: float arguments implicitly convert to double via the overload above.
// A separate float overload was removed to avoid MSVC C2666 ambiguity.

/// Simpler THD function (returns only the THD percentage).
/// Direct replacement for the local `computeTHD()` found in several files.
inline double computeTHD(const float* signal, int numSamples,
                         double fundamentalFreq, double sampleRate,
                         int numHarmonics = 8) {
    double h1 = goertzelMagnitude(signal, numSamples, fundamentalFreq, sampleRate);
    if (h1 < 1e-12) return 0.0;

    double sumHarmonicsSq = 0.0;
    for (int n = 2; n <= numHarmonics; ++n) {
        double harmonicFreq = fundamentalFreq * static_cast<double>(n);
        if (harmonicFreq >= sampleRate / 2.0) break;
        double hn = goertzelMagnitude(signal, numSamples, harmonicFreq, sampleRate);
        sumHarmonicsSq += hn * hn;
    }

    return std::sqrt(sumHarmonicsSq) / h1 * 100.0;
}

// ── RMS measurement ────────────────────────────────────────────────────────

/// Compute RMS of a float buffer. Returns 0 for empty buffers.
inline double computeRMS(const float* data, int N) {
    if (N <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < N; ++i) {
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
    return std::sqrt(sum / static_cast<double>(N));
}

/// Legacy alias used in some test files (e.g. test_neve_validation.cpp).
inline double rms(const float* data, int N) {
    return computeRMS(data, N);
}

// ── dBFS / amplitude conversion helpers ────────────────────────────────────

/// Convert dBFS to linear amplitude (0 dBFS = 1.0).
inline float dBFSToAmplitude(float dBFS) {
    return std::pow(10.0f, dBFS / 20.0f);
}

/// Convert linear amplitude to dB (relative). Returns -999 for silence.
inline double amplitudeToDb(double amp) {
    return (amp > 1e-30) ? 20.0 * std::log10(amp) : -999.0;
}

} // namespace test

// ── File-scope assertion macros ────────────────────────────────────────────
// These are deliberately outside the namespace so that existing test files
// which call CHECK(...) without a test:: qualifier continue to compile.
//
// The macros capture __FILE__ and __LINE__ for precise failure reporting.
//
// CHECK(cond, msg)                -- boolean assertion
// CHECK_NEAR(val, exp, tol, msg)  -- floating-point near-equality
// CHECK_RANGE(val, lo, hi, msg)   -- floating-point range check
// TEST_ASSERT(cond, msg)          -- alias for CHECK (used by older tests)

#define CHECK(cond, msg) \
    test::check((cond), (msg), __FILE__, __LINE__)

#define CHECK_NEAR(val, exp, tol, msg) \
    test::checkNear(static_cast<double>(val), static_cast<double>(exp), \
                    static_cast<double>(tol), (msg), __FILE__, __LINE__)

#define CHECK_RANGE(val, lo, hi, msg) \
    test::checkRange(static_cast<double>(val), static_cast<double>(lo), \
                     static_cast<double>(hi), (msg), __FILE__, __LINE__)

#define TEST_ASSERT(cond, msg) \
    test::check(!!(cond), (msg), __FILE__, __LINE__)
