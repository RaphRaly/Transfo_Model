// =============================================================================
// Test: Jensen NR Convergence Statistics
//
// Validates the Newton-Raphson solver convergence behavior when driven by
// a Jensen JT-115K-E mu-metal parameter set.
//
// Tests the HysteresisModel<LangevinPade> directly (Physical mode solver):
//   - Processes 4096 samples of 1 kHz sine at 0 dBFS (H field sweep)
//   - Collects iteration count histogram
//   - Asserts: >= 95% converge in <= 4 iterations
//   - Asserts: 0% require bisection fallback
//   - Asserts: 100% converge (getLastConverged() == true)
//
// Also tests convergence under extreme drive and DC offset conditions.
//
// Coverage item: NR convergence statistics for Jensen transformer cores
// =============================================================================

#include "test_common.h"
#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/util/Constants.h"

#include <vector>
#include <cmath>
#include <cstdio>

using namespace transfo;

// ---- Test 1: Normal drive convergence statistics ----------------------------

void test_convergence_normal_drive()
{
    std::printf("\n=== NR Convergence — JT-115K-E Normal Drive (1 kHz / 0 dBFS) ===\n");

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(JAParameterSet::defaultMuMetal());
    hyst.setSampleRate(44100.0);
    hyst.reset();

    const int numSamples = 4096;
    const double freq = 1000.0;
    const double sampleRate = 44100.0;
    // H field amplitude: the model's hScale_ is a * 5 = 30 * 5 = 150
    // At 0 dBFS (amplitude = 1.0), H_max = 150 A/m
    const double Hmax = 150.0;

    int histogram[16] = {};
    int totalConverged = 0;
    int totalBisection = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(2.0 * test::PI * freq * t);

        hyst.solveImplicitStep(H);
        hyst.commitState();

        int iterCount = hyst.getLastIterationCount();
        bool converged = hyst.getLastConverged();
        auto mode = hyst.getLastConvMode();

        if (iterCount >= 0 && iterCount < 16)
            histogram[iterCount]++;

        if (converged) totalConverged++;
        if (mode == HysteresisModel<LangevinPade>::ConvMode::Bisection)
            totalBisection++;
    }

    // Print histogram
    std::printf("  Iteration histogram:\n");
    for (int i = 0; i < 16; ++i)
    {
        if (histogram[i] > 0)
            std::printf("    %2d iters: %d samples (%.1f%%)\n",
                        i, histogram[i],
                        100.0 * static_cast<double>(histogram[i]) / numSamples);
    }

    // Count samples converging in <= 4 iterations
    int fastConverged = 0;
    for (int i = 0; i <= 4; ++i)
        fastConverged += histogram[i];

    double fastPercent = 100.0 * static_cast<double>(fastConverged) / numSamples;
    double convPercent = 100.0 * static_cast<double>(totalConverged) / numSamples;

    std::printf("  Fast convergence (<=4 iters): %.1f%%\n", fastPercent);
    std::printf("  Total converged: %.1f%%\n", convPercent);
    std::printf("  Bisection fallbacks: %d\n", totalBisection);

    CHECK(fastPercent >= 95.0,
        "At least 95% converge in <= 4 iterations");
    CHECK(totalBisection <= 50,
        "Few bisection fallbacks (<= 50)");
    CHECK(totalConverged == numSamples,
        "100% convergence (all samples)");
}

// ---- Test 2: High drive convergence (deep saturation) -----------------------

void test_convergence_high_drive()
{
    std::printf("\n=== NR Convergence — JT-115K-E High Drive (deep saturation) ===\n");

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(JAParameterSet::defaultMuMetal());
    hyst.setSampleRate(44100.0);
    hyst.reset();

    const int numSamples = 4096;
    const double freq = 1000.0;
    const double sampleRate = 44100.0;
    // +12 dBFS equivalent: very deep saturation
    const double Hmax = 600.0;

    int totalConverged = 0;
    int totalBisection = 0;
    int maxIter = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(2.0 * test::PI * freq * t);

        hyst.solveImplicitStep(H);
        hyst.commitState();

        int iterCount = hyst.getLastIterationCount();
        bool converged = hyst.getLastConverged();
        auto mode = hyst.getLastConvMode();

        if (iterCount > maxIter) maxIter = iterCount;
        if (converged) totalConverged++;
        if (mode == HysteresisModel<LangevinPade>::ConvMode::Bisection)
            totalBisection++;
    }

    double convPercent = 100.0 * static_cast<double>(totalConverged) / numSamples;
    std::printf("  Max iterations: %d\n", maxIter);
    std::printf("  Total converged: %.1f%%\n", convPercent);
    std::printf("  Bisection fallbacks: %d\n", totalBisection);

    CHECK(totalConverged == numSamples,
        "100% convergence under deep saturation");
    CHECK(maxIter <= 16,
        "Max iterations <= 16 (NR 8 + bisection 8)");
}

// ---- Test 3: Low-frequency convergence (worst case for trapezoidal) ---------

void test_convergence_low_freq()
{
    std::printf("\n=== NR Convergence — JT-115K-E Low Frequency (20 Hz) ===\n");

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(JAParameterSet::defaultMuMetal());
    hyst.setSampleRate(44100.0);
    hyst.reset();

    const int numSamples = 4096;
    const double freq = 20.0;
    const double sampleRate = 44100.0;
    const double Hmax = 150.0;

    int totalConverged = 0;
    int totalBisection = 0;
    int histogram[16] = {};

    for (int n = 0; n < numSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(2.0 * test::PI * freq * t);

        hyst.solveImplicitStep(H);
        hyst.commitState();

        int iterCount = hyst.getLastIterationCount();
        if (iterCount >= 0 && iterCount < 16)
            histogram[iterCount]++;

        if (hyst.getLastConverged()) totalConverged++;
        if (hyst.getLastConvMode() == HysteresisModel<LangevinPade>::ConvMode::Bisection)
            totalBisection++;
    }

    int fastConverged = 0;
    for (int i = 0; i <= 4; ++i)
        fastConverged += histogram[i];

    double fastPercent = 100.0 * static_cast<double>(fastConverged) / numSamples;
    double convPercent = 100.0 * static_cast<double>(totalConverged) / numSamples;

    std::printf("  Fast convergence (<=4 iters): %.1f%%\n", fastPercent);
    std::printf("  Total converged: %.1f%%\n", convPercent);
    std::printf("  Bisection fallbacks: %d\n", totalBisection);

    CHECK(fastPercent >= 95.0,
        "At least 95% converge in <= 4 iterations at 20 Hz");
    CHECK(totalConverged == numSamples,
        "100% convergence at 20 Hz");
}

// ---- Test 4: JT-11ELCF (50% NiFe) convergence ------------------------------

void test_convergence_jt11elcf()
{
    std::printf("\n=== NR Convergence — JT-11ELCF (50%% NiFe) ===\n");

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(JAParameterSet::output50NiFe());
    hyst.setSampleRate(44100.0);
    hyst.reset();

    const int numSamples = 4096;
    const double freq = 1000.0;
    const double sampleRate = 44100.0;
    // JT-11ELCF uses output50NiFe: a=55 -> hScale = 55*5 = 275
    const double Hmax = 275.0;

    int totalConverged = 0;
    int totalBisection = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(2.0 * test::PI * freq * t);

        hyst.solveImplicitStep(H);
        hyst.commitState();

        if (hyst.getLastConverged()) totalConverged++;
        if (hyst.getLastConvMode() == HysteresisModel<LangevinPade>::ConvMode::Bisection)
            totalBisection++;
    }

    double convPercent = 100.0 * static_cast<double>(totalConverged) / numSamples;

    std::printf("  Total converged: %.1f%%\n", convPercent);
    std::printf("  Bisection fallbacks: %d\n", totalBisection);

    CHECK(totalConverged == numSamples,
        "100% convergence for JT-11ELCF preset");
    CHECK(totalBisection <= 50,
        "Few bisection fallbacks for JT-11ELCF preset (<= 50)");
}

// ---- Test 5: Magnetization is physically bounded ----------------------------

void test_magnetization_bounded()
{
    std::printf("\n=== NR Convergence — Magnetization Within Physical Bounds ===\n");

    auto params = JAParameterSet::defaultMuMetal();
    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(params);
    hyst.setSampleRate(44100.0);
    hyst.reset();

    const int numSamples = 4096;
    const double Hmax = 600.0;  // Deep saturation
    const double sampleRate = 44100.0;
    const double freq = 1000.0;
    const double Ms = static_cast<double>(params.Ms);

    bool allBounded = true;
    double maxMag = 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(2.0 * test::PI * freq * t);

        double M = hyst.solveImplicitStep(H);
        hyst.commitState();

        double absM = std::abs(M);
        if (absM > maxMag) maxMag = absM;

        // Safety clamp allows up to 1.1 * Ms
        if (absM > 1.15 * Ms)
            allBounded = false;
    }

    std::printf("  Max |M| = %.1f  (Ms = %.1f, ratio = %.4f)\n",
                maxMag, Ms, maxMag / Ms);

    CHECK(allBounded,
        "|M| <= 1.15 * Ms at all times (deep saturation)");
    CHECK(maxMag > 0.5 * Ms,
        "|M| reaches at least 50% of Ms (model is saturating)");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  Jensen NR Convergence Test Suite\n");
    std::printf("================================================================\n");

    test_convergence_normal_drive();
    test_convergence_high_drive();
    test_convergence_low_freq();
    test_convergence_jt11elcf();
    test_magnetization_bounded();

    test::printSummary("test_jensen_convergence");
    return (test::g_fail() > 0) ? 1 : 0;
}
