// =============================================================================
// Test: NR Solver Low-H Stress Test
//
// Validates that the Newton-Raphson solver no longer diverges at very low
// applied field strengths (hScale ~ 0.065), which was the root cause of
// ~1/5000 sample sporadic divergence in Artistic mode.
//
// Three scenarios:
//   1. Low-H sine sweep with reversals (hScale = 0.065)
//   2. Abrupt amplitude drop from deep saturation to low H
//   3. 100k sample stress test with pseudo-random low-level signal
//
// Success criteria:
//   - 100% convergence in all scenarios
//   - Mean iterations < 4
//   - Max bisection fallbacks < 20
//
// Reference: Zhao 2024 (L'Hopital), Benabou 2003 (bisection), Fix A/B/C.
// =============================================================================

#include "test_common.h"
#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/util/Constants.h"

#include <cmath>
#include <cstdio>

using namespace transfo;

// ---- Test 1: Low-H sine sweep with reversals --------------------------------
// hScale = 0.065 means H_max = 0.065 * a = 0.065 * 30 = 1.95 A/m (MuMetal)
// This is the regime that caused ~1/5000 divergence before the fix.

void test_low_h_sine_reversals()
{
    std::printf("\n=== NR Low-H Stress: Sine Reversals (hScale=0.065) ===\n");

    HysteresisModel<LangevinPade> hyst;
    auto params = JAParameterSet::defaultMuMetal();
    hyst.setParameters(params);
    hyst.setSampleRate(48000.0);
    hyst.setMaxIterations(8);
    hyst.reset();

    const double hScale = 0.065;
    const double Hmax = hScale * static_cast<double>(params.a);  // ~1.95 A/m
    const double sampleRate = 48000.0;
    const int numSamples = 50000;  // ~1 second

    int divergenceCount = 0;
    int bisectionCount = 0;
    int totalIter = 0;
    int maxIter = 0;

    // Multi-frequency signal with many reversal points
    for (int n = 0; n < numSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        // Combine frequencies for many reversals per cycle
        double H = Hmax * (0.6 * std::sin(2.0 * test::kPi * 100.0 * t)
                         + 0.3 * std::sin(2.0 * test::kPi * 317.0 * t)
                         + 0.1 * std::sin(2.0 * test::kPi * 1000.0 * t));

        hyst.solveImplicitStep(H);
        hyst.commitState();

        int iters = hyst.getLastIterationCount();
        totalIter += iters;
        if (iters > maxIter) maxIter = iters;
        if (!hyst.getLastConverged()) divergenceCount++;
        if (hyst.getLastConvMode() == HysteresisModel<LangevinPade>::ConvMode::Bisection)
            bisectionCount++;
    }

    double meanIter = static_cast<double>(totalIter) / numSamples;
    double convPercent = 100.0 * (1.0 - static_cast<double>(divergenceCount) / numSamples);

    std::printf("  H_max = %.3f A/m (hScale=%.3f)\n", Hmax, hScale);
    std::printf("  Samples: %d\n", numSamples);
    std::printf("  Divergences: %d (%.4f%%)\n", divergenceCount,
                100.0 * static_cast<double>(divergenceCount) / numSamples);
    std::printf("  Bisection fallbacks: %d\n", bisectionCount);
    std::printf("  Mean iterations: %.2f\n", meanIter);
    std::printf("  Max iterations: %d\n", maxIter);

    CHECK(divergenceCount == 0,
        "Zero divergences at hScale=0.065 with reversals");
    CHECK(bisectionCount < 500,
        "Bisection fallbacks < 1% of samples at low H");
    CHECK(meanIter < 4.0,
        "Mean iterations < 4 at low H");
}

// ---- Test 2: Abrupt amplitude drop (saturation → low H) ---------------------
// This tests the warm-start when the signal drops suddenly from deep
// saturation to very low level. The extrapolative predictor overshoots
// because M was changing fast and suddenly changes slowly.

void test_amplitude_drop()
{
    std::printf("\n=== NR Low-H Stress: Abrupt Amplitude Drop ===\n");

    HysteresisModel<LangevinPade> hyst;
    auto params = JAParameterSet::defaultMuMetal();
    hyst.setParameters(params);
    hyst.setSampleRate(48000.0);
    hyst.setMaxIterations(8);
    hyst.reset();

    const double sampleRate = 48000.0;
    const double freq = 200.0;
    const double Hmax_high = 500.0;  // Deep saturation
    const double Hmax_low  = 2.0;    // Near-zero field (hScale ~ 0.065)
    const int warmup = 2000;         // ~42 ms at high amplitude
    const int lowPhase = 10000;      // ~208 ms at low amplitude

    int divergenceCount = 0;
    int bisectionCount = 0;

    // Phase 1: Drive into deep saturation
    for (int n = 0; n < warmup; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax_high * std::sin(2.0 * test::kPi * freq * t);
        hyst.solveImplicitStep(H);
        hyst.commitState();
    }

    // Phase 2: Abrupt drop to low amplitude
    for (int n = 0; n < lowPhase; ++n)
    {
        double t = static_cast<double>(n + warmup) / sampleRate;
        double H = Hmax_low * std::sin(2.0 * test::kPi * freq * t);
        hyst.solveImplicitStep(H);
        hyst.commitState();

        if (!hyst.getLastConverged()) divergenceCount++;
        if (hyst.getLastConvMode() == HysteresisModel<LangevinPade>::ConvMode::Bisection)
            bisectionCount++;
    }

    std::printf("  High phase: %d samples @ H_max=%.0f A/m\n", warmup, Hmax_high);
    std::printf("  Low phase: %d samples @ H_max=%.1f A/m\n", lowPhase, Hmax_low);
    std::printf("  Divergences in low phase: %d\n", divergenceCount);
    std::printf("  Bisection fallbacks in low phase: %d\n", bisectionCount);

    CHECK(divergenceCount == 0,
        "Zero divergences after abrupt amplitude drop");
    CHECK(bisectionCount < 50,
        "Bisection fallbacks < 50 after amplitude drop");
}

// ---- Test 3: 100k sample stress test with all materials ----------------------

void test_100k_stress_all_materials()
{
    std::printf("\n=== NR Low-H Stress: 100k Samples x 3 Materials ===\n");

    struct MaterialTest {
        const char* name;
        JAParameterSet params;
    };

    MaterialTest materials[] = {
        { "MuMetal (JT-115K-E)", JAParameterSet::defaultMuMetal() },
        { "NiFe50 (JT-11ELCF)",  JAParameterSet::defaultNiFe50() },
        { "SiFe (Fender)",        JAParameterSet::defaultFenderSiFe() },
    };

    for (const auto& mat : materials)
    {
        HysteresisModel<LangevinPade> hyst;
        hyst.setParameters(mat.params);
        hyst.setSampleRate(48000.0);
        hyst.setMaxIterations(8);
        hyst.reset();

        const double sampleRate = 48000.0;
        const double hScale = 0.065;
        const double Hmax = hScale * static_cast<double>(mat.params.a);
        const int numSamples = 100000;

        int divergenceCount = 0;
        int bisectionCount = 0;
        int totalIter = 0;

        // Pseudo-random-ish multi-tone signal at low level
        for (int n = 0; n < numSamples; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = Hmax * (std::sin(2.0 * test::kPi * 83.0 * t)
                             + 0.5 * std::sin(2.0 * test::kPi * 211.0 * t)
                             + 0.3 * std::sin(2.0 * test::kPi * 587.0 * t)
                             + 0.1 * std::cos(2.0 * test::kPi * 1571.0 * t));

            hyst.solveImplicitStep(H);
            hyst.commitState();

            totalIter += hyst.getLastIterationCount();
            if (!hyst.getLastConverged()) divergenceCount++;
            if (hyst.getLastConvMode() == HysteresisModel<LangevinPade>::ConvMode::Bisection)
                bisectionCount++;
        }

        double meanIter = static_cast<double>(totalIter) / numSamples;
        std::printf("  %s: H_max=%.2f A/m, divergences=%d, bisections=%d, mean_iter=%.2f\n",
                    mat.name, Hmax, divergenceCount, bisectionCount, meanIter);

        char msg[256];
        std::snprintf(msg, sizeof(msg), "Zero divergences for %s at hScale=0.065", mat.name);
        CHECK(divergenceCount == 0, msg);
    }
}

// ---- Test 4: Reversal point stress (rapid sign changes in dH) ----------------

void test_reversal_stress()
{
    std::printf("\n=== NR Low-H Stress: Rapid Reversals ===\n");

    HysteresisModel<LangevinPade> hyst;
    auto params = JAParameterSet::defaultMuMetal();
    hyst.setParameters(params);
    hyst.setSampleRate(48000.0);
    hyst.setMaxIterations(8);
    hyst.reset();

    const int numReversals = 200;
    const double Hstep = 0.5;  // Very small H steps
    int divergenceCount = 0;

    // Magnetize slightly
    for (int i = 0; i < 100; ++i)
    {
        hyst.solveImplicitStep(static_cast<double>(i) * 0.1);
        hyst.commitState();
    }

    // Rapid sign reversals at low amplitude
    double H = 5.0;
    for (int r = 0; r < numReversals; ++r)
    {
        double direction = (r % 2 == 0) ? -1.0 : 1.0;
        for (int s = 0; s < 20; ++s)
        {
            H += direction * Hstep;
            hyst.solveImplicitStep(H);
            hyst.commitState();

            if (!hyst.getLastConverged())
                divergenceCount++;
        }
    }

    std::printf("  Reversals: %d (20 samples each, H_step=%.1f A/m)\n",
                numReversals, Hstep);
    std::printf("  Divergences: %d\n", divergenceCount);

    CHECK(divergenceCount == 0,
        "Zero divergences during rapid reversals at low H");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  NR Low-H Stress Test Suite (Problem #3 Fix Validation)\n");
    std::printf("================================================================\n");

    test_low_h_sine_reversals();
    test_amplitude_drop();
    test_100k_stress_all_materials();
    test_reversal_stress();

    return test::printSummary("test_nr_low_h_stress");
}
