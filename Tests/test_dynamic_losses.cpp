// =============================================================================
// Test: DynamicLosses — Eddy Current & Excess Loss Validation
//
// Standalone test (no JUCE dependency) that validates:
//   1. DC signal — dBdt and Hdyn are zero for constant B
//   2. Bilinear derivative — trapezoidal consistency property
//   3. Sine sweep — loop area increases with frequency
//   4. Preset comparison — Fender Hdyn peaks > MuMetal
//   5. CPU overhead — dynamic losses add < 15% vs bare J-A
//   6. Passivity — loop area WITH dynamic >= WITHOUT
//   7. Jacobian — finite-difference vs analytical
//
// NOTE: The simulation helpers use backward difference for dB/dt instead of
// the bilinear derivative. The bilinear derivative has a pole at z=-1 and
// requires the iterative WDF/HSIM solver for stability. In a single-pass
// feedback loop (as used in these tests), the Nyquist oscillation from the
// bilinear transform destabilizes the coupling. Backward difference is
// first-order accurate and stable for single-pass testing.
//
// IMPORTANT: dB/dt is computed from *committed* B values with a one-sample
// delay: dBdt[n] = fs * (B[n-1] - B[n-2]). This ensures the magnetization
// contribution is included in the derivative (both B values contain their
// respective solved M). Using a predicted B_pred = μ₀*(H + M_{n-1}) would
// cancel M in the difference and underestimate Hdyn.
//
// All tests use the v3 core/ headers — no legacy Source/ code.
// =============================================================================

#include "../core/include/core/magnetics/DynamicLosses.h"
#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/util/Constants.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <string>
#include <algorithm>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

void CHECK(bool cond, const char* msg)
{
    if (cond)
    {
        std::cout << "  PASS: " << msg << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "  *** FAIL: " << msg << " ***" << std::endl;
        g_fail++;
    }
}

struct BHPoint { double H; double B; };

// Compute Hdyn using backward difference: dBdt = fs * (B[n] - B[n-1]).
// Stable for single-pass feedback (no Nyquist pole unlike bilinear).
static double computeHdyn_backwardDiff(double K1, double K2,
                                        double B_curr, double B_prev, double fs)
{
    double dBdt = fs * (B_curr - B_prev);
    double absdBdt = std::abs(dBdt);
    double sign_dBdt = (dBdt > 0.0) ? 1.0 : (dBdt < 0.0) ? -1.0 : 0.0;
    double H_eddy = K1 * dBdt;
    double H_excess = K2 * sign_dBdt * std::sqrt(absdBdt);
    return H_eddy + H_excess;
}

// Simulate full J-A + dynamic losses using backward difference.
// Collects last cycle's (H, B) points.
std::vector<BHPoint> simulateWithDynamic(const transfo::JAParameterSet& params,
                                          double Hmax, double freq, double sampleRate,
                                          int numCycles = 5)
{
    transfo::HysteresisModel<transfo::LangevinPade> model;
    model.setParameters(params);
    model.setSampleRate(sampleRate);
    model.reset();

    double K1 = static_cast<double>(params.K1);
    double K2 = static_cast<double>(params.K2);

    int samplesPerCycle = std::max(4, static_cast<int>(std::round(sampleRate / freq)));
    int totalSamples = samplesPerCycle * numCycles;

    std::vector<BHPoint> loop;
    loop.reserve(static_cast<size_t>(samplesPerCycle));

    double B_prev = 0.0;       // B[n-1] committed (includes solved M)
    double B_prev_prev = 0.0;  // B[n-2] committed

    for (int n = 0; n < totalSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(transfo::kTwoPi * freq * t);

        // One-sample-delayed dBdt from committed B values (includes M)
        double Hdyn = computeHdyn_backwardDiff(K1, K2, B_prev, B_prev_prev, sampleRate);
        double H_eff = H - Hdyn;

        // Solve J-A model
        double M = model.solveImplicitStep(H_eff);
        model.commitState();

        double B = transfo::kMu0 * (H + M);
        B_prev_prev = B_prev;
        B_prev = B;

        if (n >= samplesPerCycle * (numCycles - 1))
            loop.push_back({H, B});
    }
    return loop;
}

// Simulate J-A only (no dynamic losses).
std::vector<BHPoint> simulateWithoutDynamic(const transfo::JAParameterSet& params,
                                             double Hmax, double freq, double sampleRate,
                                             int numCycles = 5)
{
    transfo::HysteresisModel<transfo::LangevinPade> model;
    model.setParameters(params);
    model.setSampleRate(sampleRate);
    model.reset();

    int samplesPerCycle = std::max(4, static_cast<int>(std::round(sampleRate / freq)));
    int totalSamples = samplesPerCycle * numCycles;

    std::vector<BHPoint> loop;
    loop.reserve(static_cast<size_t>(samplesPerCycle));

    for (int n = 0; n < totalSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(transfo::kTwoPi * freq * t);

        double M = model.solveImplicitStep(H);
        model.commitState();

        double B = transfo::kMu0 * (H + M);

        if (n >= samplesPerCycle * (numCycles - 1))
            loop.push_back({H, B});
    }
    return loop;
}

// Shoelace formula for polygon area.
double computeLoopArea(const std::vector<BHPoint>& loop)
{
    double area = 0.0;
    int n = static_cast<int>(loop.size());
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        area += loop[i].H * loop[j].B - loop[j].H * loop[i].B;
    }
    return std::abs(area) / 2.0;
}

// ─── TEST 1: DC signal — dBdt and Hdyn must be zero ─────────────────────────

void test1_dc_signal()
{
    std::cout << "\n=== TEST 1: DC Signal — Zero Dynamic Loss ===" << std::endl;

    transfo::DynamicLosses dyn;
    dyn.setCoefficients(0.01f, 0.05f);
    dyn.setSampleRate(48000.0);
    dyn.reset();

    // Test A: Constant B=0 from reset — bilinear dBdt must be exactly 0.0
    bool passA = true;
    for (int i = 0; i < 100; ++i)
    {
        double dBdt = dyn.computeBilinearDBdt(0.0);
        dyn.commitState(0.0);
        if (dBdt != 0.0)
        {
            std::cout << "  [A] Sample " << i << ": dBdt = " << dBdt << std::endl;
            passA = false;
            break;
        }
    }
    CHECK(passA, "Constant B=0: bilinear dBdt is exactly 0");

    // Test B: sign(0) = 0 means Hdyn(dBdt=0) = 0 exactly
    double Hdyn_zero = dyn.computeHfromDBdt(0.0);
    CHECK(Hdyn_zero == 0.0, "computeHfromDBdt(0.0) returns exactly 0");

    // Test C: Step response — finite, positive for positive step
    dyn.reset();
    double dBdt_step = dyn.computeBilinearDBdt(0.5);
    double Hdyn_step = dyn.computeHfromDBdt(dBdt_step);
    dyn.commitState(0.5);
    bool passC = std::isfinite(Hdyn_step) && Hdyn_step > 0.0;
    std::cout << "  Step 0->0.5: dBdt=" << dBdt_step
              << ", Hdyn=" << Hdyn_step << std::endl;
    CHECK(passC, "Step response: finite positive Hdyn");

    // Test D: Negative coefficients are clamped to 0
    transfo::DynamicLosses dyn2;
    dyn2.setCoefficients(-0.01f, -0.05f);
    CHECK(!dyn2.isEnabled(), "Negative K1/K2 are clamped: isEnabled() = false");
}

// ─── TEST 2: Bilinear derivative — trapezoidal consistency ──────────────────
// The bilinear derivative y[n] and trapezoidal integral must be consistent:
//   x[n] = x[n-1] + (T/2)*(y[n] + y[n-1])
// where y[n] = computeBilinearDBdt(x[n]) and T = 1/fs.

void test2_bilinear_consistency()
{
    std::cout << "\n=== TEST 2: Bilinear Derivative — Trapezoidal Consistency ===" << std::endl;

    const double sampleRate = 96000.0;
    const double T = 1.0 / sampleRate;

    transfo::DynamicLosses dyn;
    dyn.setCoefficients(0.01f, 0.05f);
    dyn.setSampleRate(sampleRate);
    dyn.reset();

    // Feed a sine B signal and verify the trapezoidal identity holds
    const double B0 = 0.5;
    const double freq = 1000.0;
    const double omega = transfo::kTwoPi * freq;

    int samplesPerCycle = static_cast<int>(std::round(sampleRate / freq));
    int totalSamples = samplesPerCycle * 3;

    double B_prev = 0.0;
    double dBdt_prev = 0.0;
    double maxErr = 0.0;
    bool allFinite = true;

    for (int n = 0; n < totalSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double B = B0 * std::sin(omega * t);

        double dBdt = dyn.computeBilinearDBdt(B);
        dyn.commitState(B);

        if (n > 0)
        {
            // Trapezoidal identity: B[n] = B[n-1] + (T/2)*(dBdt[n] + dBdt[n-1])
            double B_reconstructed = B_prev + (T / 2.0) * (dBdt + dBdt_prev);
            double err = std::abs(B_reconstructed - B);
            if (err > maxErr) maxErr = err;
        }

        if (!std::isfinite(dBdt)) allFinite = false;

        B_prev = B;
        dBdt_prev = dBdt;
    }

    std::cout << "  Max trapezoidal reconstruction error: " << maxErr << std::endl;
    CHECK(maxErr < 1e-10, "Trapezoidal identity holds (error < 1e-10)");
    CHECK(allFinite, "All bilinear dBdt values are finite");

    // Also test: computeHfromDBdt produces correct field magnitudes
    // For known dBdt=1000 T/s, K1=0.01, K2=0.05:
    // H_eddy = 0.01 * 1000 = 10 A/m
    // H_excess = 0.05 * sqrt(1000) = 1.581 A/m
    // H_total = 11.581 A/m
    double H_test = dyn.computeHfromDBdt(1000.0);
    double H_expected = 0.01 * 1000.0 + 0.05 * std::sqrt(1000.0);
    double relErr = std::abs(H_test - H_expected) / H_expected;
    std::cout << "  H(dBdt=1000): computed=" << H_test << ", expected=" << H_expected
              << ", relErr=" << relErr * 100 << "%" << std::endl;
    CHECK(relErr < 1e-6, "computeHfromDBdt matches analytical at dBdt=1000");

    // Negative dBdt: H should be negative
    double H_neg = dyn.computeHfromDBdt(-1000.0);
    CHECK(H_neg < 0.0 && std::abs(H_neg + H_expected) / H_expected < 1e-6,
          "computeHfromDBdt(-1000) = -H(+1000) (antisymmetric)");
}

// ─── TEST 3: Sine sweep — loop area increases with frequency ────────────────

void test3_sine_sweep()
{
    std::cout << "\n=== TEST 3: Sine Sweep — Loop Area vs Frequency ===" << std::endl;

    auto params = transfo::JAParameterSet::defaultSiFe();
    double freqs[] = {50.0, 100.0, 500.0, 1000.0, 5000.0};
    int numFreqs = 5;
    double Hmax = 300.0;
    double sampleRate = 96000.0;

    std::vector<double> areas;
    areas.reserve(numFreqs);

    for (int i = 0; i < numFreqs; ++i)
    {
        auto loop = simulateWithDynamic(params, Hmax, freqs[i], sampleRate, 5);
        double area = computeLoopArea(loop);
        areas.push_back(area);

        std::cout << "  f=" << freqs[i] << " Hz: loop area = " << area << std::endl;
    }

    // Strict monotonicity
    bool monotonic = true;
    for (int i = 1; i < numFreqs; ++i)
    {
        if (areas[static_cast<size_t>(i)] <= areas[static_cast<size_t>(i - 1)])
        {
            std::cout << "  Violation: area[" << freqs[i] << "] = " << areas[static_cast<size_t>(i)]
                      << " <= area[" << freqs[i - 1] << "] = " << areas[static_cast<size_t>(i - 1)] << std::endl;
            monotonic = false;
        }
    }
    CHECK(monotonic, "Loop area strictly increases with frequency");

    // Sanity: high freq / low freq ratio — the static hysteresis dominates,
    // so the dynamic contribution adds a moderate but non-zero area increase.
    // A ratio > 1.05 confirms the dynamic term is working without demanding
    // an unrealistic amplification over the dominant static loop.
    double ratio = areas.back() / areas.front();
    std::cout << "  Area ratio (5kHz / 50Hz) = " << ratio << "x" << std::endl;
    CHECK(ratio > 1.05, "5kHz loop area > 1.05x the 50Hz loop area");
}

// ─── TEST 4: Preset comparison — Fender > MuMetal ───────────────────────────

void test4_preset_comparison()
{
    std::cout << "\n=== TEST 4: Preset Comparison — Fender vs MuMetal ===" << std::endl;

    double freq = 1000.0;
    double Hmax = 500.0;
    double sampleRate = 96000.0;

    auto fenderParams = transfo::JAParameterSet::defaultFenderSiFe();
    auto muMetalParams = transfo::JAParameterSet::defaultMuMetal();

    // Measure peak |Hdyn| using one-sample-delayed dBdt from committed B
    auto measurePeak = [&](const transfo::JAParameterSet& p) -> double
    {
        transfo::HysteresisModel<transfo::LangevinPade> model;
        model.setParameters(p);
        model.setSampleRate(sampleRate);
        model.reset();

        double K1 = static_cast<double>(p.K1);
        double K2 = static_cast<double>(p.K2);

        int spc = static_cast<int>(std::round(sampleRate / freq));
        int total = spc * 5;
        double peak = 0.0;
        double B_prev = 0.0;
        double B_prev_prev = 0.0;

        for (int n = 0; n < total; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = Hmax * std::sin(transfo::kTwoPi * freq * t);

            // One-sample-delayed dBdt from committed B values (includes M)
            double Hdyn = computeHdyn_backwardDiff(K1, K2, B_prev, B_prev_prev, sampleRate);
            double H_eff = H - Hdyn;

            double M = model.solveImplicitStep(H_eff);
            model.commitState();

            double B = transfo::kMu0 * (H + M);
            B_prev_prev = B_prev;
            B_prev = B;

            if (n >= spc * 4)
                peak = std::max(peak, std::abs(Hdyn));
        }
        return peak;
    };

    double peakFender = measurePeak(fenderParams);
    double peakMuMetal = measurePeak(muMetalParams);

    std::cout << "  Fender peak |Hdyn|  = " << peakFender << std::endl;
    std::cout << "  MuMetal peak |Hdyn| = " << peakMuMetal << std::endl;

    // Sanity: peaks must be physically meaningful (not just rounding noise)
    CHECK(peakFender > 1.0, "Fender peak Hdyn > 1 A/m (meaningful)");
    CHECK(peakMuMetal > 0.01, "MuMetal peak Hdyn > 0.01 A/m (meaningful)");

    double ratio = (peakMuMetal > 0.0) ? (peakFender / peakMuMetal) : 0.0;
    std::cout << "  Ratio = " << ratio << "x" << std::endl;
    CHECK(ratio > 3.0, "Fender peak Hdyn > 3x MuMetal");
}

// ─── TEST 5: CPU overhead — dynamic losses vs bare J-A ──────────────────────

void test5_cpu_overhead()
{
    std::cout << "\n=== TEST 5: CPU Overhead ===" << std::endl;

    const int N = 500000;
    const double sampleRate = 96000.0;
    const double freq = 1000.0;
    auto params = transfo::JAParameterSet::defaultMuMetal();
    double K1 = static_cast<double>(params.K1);
    double K2 = static_cast<double>(params.K2);

    // WITH dynamic losses (one-sample-delayed dBdt from committed B)
    double timeWith = 0.0;
    {
        transfo::HysteresisModel<transfo::LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(sampleRate);
        model.reset();

        double B_prev = 0.0;
        double B_prev_prev = 0.0;
        auto start = std::chrono::high_resolution_clock::now();

        for (int n = 0; n < N; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = 200.0 * std::sin(transfo::kTwoPi * freq * t);

            double Hdyn = computeHdyn_backwardDiff(K1, K2, B_prev, B_prev_prev, sampleRate);
            double H_eff = H - Hdyn;

            double M = model.solveImplicitStep(H_eff);
            model.commitState();

            double B = transfo::kMu0 * (H + M);
            B_prev_prev = B_prev;
            B_prev = B;
        }

        auto end = std::chrono::high_resolution_clock::now();
        timeWith = std::chrono::duration<double, std::milli>(end - start).count();
    }

    // WITHOUT dynamic losses — bare J-A, no DynamicLosses calls
    double timeWithout = 0.0;
    {
        transfo::HysteresisModel<transfo::LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(sampleRate);
        model.reset();

        auto start = std::chrono::high_resolution_clock::now();

        for (int n = 0; n < N; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = 200.0 * std::sin(transfo::kTwoPi * freq * t);

            double M = model.solveImplicitStep(H);
            model.commitState();
        }

        auto end = std::chrono::high_resolution_clock::now();
        timeWithout = std::chrono::duration<double, std::milli>(end - start).count();
    }

    double overhead = (timeWithout > 0.0)
                      ? 100.0 * (timeWith - timeWithout) / timeWithout
                      : 0.0;

    std::cout << "  Time WITH:    " << timeWith << " ms" << std::endl;
    std::cout << "  Time WITHOUT: " << timeWithout << " ms" << std::endl;
    std::cout << "  Overhead: " << overhead << "%" << std::endl;

    CHECK(overhead < 15.0, "Dynamic losses overhead < 15%");
}

// ─── TEST 6: Passivity — loop area WITH >= WITHOUT ──────────────────────────
// Dynamic losses add dissipation, so the B-H loop should be wider (more area).

void test6_passivity()
{
    std::cout << "\n=== TEST 6: Passivity — Dynamic Losses Widen B-H Loop ===" << std::endl;

    double sampleRate = 96000.0;
    double Hmax = 300.0;
    double freqs[] = {100.0, 500.0, 1000.0, 5000.0};
    int numFreqs = 4;

    auto params = transfo::JAParameterSet::defaultSiFe();

    bool allPass = true;
    for (int fi = 0; fi < numFreqs; ++fi)
    {
        double freq = freqs[fi];
        auto loopWith = simulateWithDynamic(params, Hmax, freq, sampleRate, 5);
        auto loopWithout = simulateWithoutDynamic(params, Hmax, freq, sampleRate, 5);

        double areaWith = computeLoopArea(loopWith);
        double areaWithout = computeLoopArea(loopWithout);

        std::cout << "  f=" << freq << " Hz: with=" << areaWith
                  << ", without=" << areaWithout << std::endl;

        // With dynamic losses should produce more (or equal) loss
        if (areaWith < areaWithout * 0.99) // 1% tolerance
        {
            std::cout << "  *** Passivity violation at " << freq << " Hz" << std::endl;
            allPass = false;
        }
    }

    CHECK(allPass, "Loop area WITH dynamic >= WITHOUT at all frequencies");
}

// ─── TEST 7: Jacobian — finite-difference check ─────────────────────────────

void test7_jacobian()
{
    std::cout << "\n=== TEST 7: Jacobian — Analytical vs Finite Difference ===" << std::endl;

    transfo::DynamicLosses dyn;
    dyn.setCoefficients(0.01f, 0.05f);
    dyn.setSampleRate(96000.0);
    dyn.reset();

    // Test at several dBdt values (avoid near-zero where sqrt cusp causes issues)
    double testDBdt[] = {-50000.0, -1000.0, -10.0, 10.0, 1000.0, 50000.0};
    int numTests = 6;

    bool allPass = true;
    const double h = 1e-4;  // perturbation for FD

    for (int i = 0; i < numTests; ++i)
    {
        double dBdt = testDBdt[i];

        // Analytical Jacobian
        double jac_analytical = dyn.computeJacobian(dBdt);

        // Finite difference: dH/d(dBdt) * d(dBdt)/dB = dH/d(dBdt) * 2*fs
        double H_plus = dyn.computeHfromDBdt(dBdt + h);
        double H_minus = dyn.computeHfromDBdt(dBdt - h);
        double dH_dDBdt = (H_plus - H_minus) / (2.0 * h);
        double jac_fd = dH_dDBdt * 2.0 * 96000.0;

        double relErr = std::abs(jac_analytical - jac_fd) / std::abs(jac_analytical);

        std::cout << "  dBdt=" << dBdt << ": analytical=" << jac_analytical
                  << ", FD=" << jac_fd << ", relErr=" << relErr * 100 << "%" << std::endl;

        if (relErr > 0.01)
            allPass = false;
    }

    CHECK(allPass, "Jacobian matches finite difference within 1%");
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "  DynamicLosses Test Suite (v3 core/)" << std::endl;
    std::cout << "================================================================" << std::endl;

    test1_dc_signal();
    test2_bilinear_consistency();
    test3_sine_sweep();
    test4_preset_comparison();
    test5_cpu_overhead();
    test6_passivity();
    test7_jacobian();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_fail > 0) ? 1 : 0;
}
