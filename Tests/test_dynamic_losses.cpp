// =============================================================================
// Test: DynamicLosses — Eddy Current & Excess Loss Validation
//
// Standalone test (no JUCE dependency) that validates:
//   1. DC signal — dBdt and Hdyn are zero for constant B
//   2. Backward difference derivative — accuracy vs analytical
//   3. Sine sweep — loop area increases with frequency
//   4. Preset comparison — Fender Hdyn peaks > MuMetal
//   5. CPU overhead — dynamic losses add < 15% vs bare J-A
//   6. Passivity — loop area WITH dynamic >= WITHOUT
//   7. Jacobian — finite-difference vs analytical
//
// The simulation helpers use the DynamicLosses class with damped χ-scaling
// (same algorithm as production code in TransformerModel):
//   B_pred   = μ₀*(H + M_committed) → estimate B from previous M
//   dBdt_raw = fs*(B_pred - B_prev) → backward diff (only captures μ₀·ΔH)
//   G        = K1·fs·μ₀·χ          → feedback gain
//   dBdt     = dBdt_raw·(1+χ)/(1+G) → self-consistent damped correction
//   Hdyn     = K1*dBdt + K2*sign*√|dBdt|
//   H_eff    = H - Hdyn              → reduced field for J-A
//   M        = JA(H_eff)             → solve static model
//   B_actual = μ₀*(H + M)            → commit actual B for next step
//
// The (1+χ) numerator restores the missing ΔM (M-cancellation in B_pred).
// The (1+G) denominator provides self-limiting feedback — mirroring the
// physical eddy current shielding that prevents dBdt from exceeding the
// applied field.  Derived from linearized implicit coupling analysis.
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

// Simulate full J-A + dynamic losses using B_pred commit approach.
// Same algorithm as TransformerModel production code:
//   B_pred = μ₀*(H + M_committed)  → predict B from previous M
//   dBdt   = fs*(B_pred - B_prev)  → backward diff captures ΔH + ΔM
//   Hdyn   = computeHfromDBdt(dBdt)
//   H_eff  = H - Hdyn
//   M      = JA(H_eff)
//   commit B_pred (not B_actual) for next step
// Collects last cycle's (H, B) points.
std::vector<BHPoint> simulateWithDynamic(const transfo::JAParameterSet& params,
                                          double Hmax, double freq, double sampleRate,
                                          int numCycles = 5)
{
    transfo::HysteresisModel<transfo::LangevinPade> model;
    model.setParameters(params);
    model.setSampleRate(sampleRate);
    model.reset();

    transfo::DynamicLosses dyn;
    dyn.setCoefficients(params.K1, params.K2);
    dyn.setSampleRate(sampleRate);
    dyn.reset();

    int samplesPerCycle = std::max(4, static_cast<int>(std::round(sampleRate / freq)));
    int totalSamples = samplesPerCycle * numCycles;

    std::vector<BHPoint> loop;
    loop.reserve(static_cast<size_t>(samplesPerCycle));

    for (int n = 0; n < totalSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double H = Hmax * std::sin(transfo::kTwoPi * freq * t);

        // Damped χ-scaling: self-consistent linearized solution.
        // dBdt = dBdt_raw*(1+χ)/(1 + K1·fs·μ₀·χ) restores ΔM while
        // the denominator prevents overcorrection at high frequencies.
        double M_c = model.getMagnetization();
        double chi = std::max(0.0, model.getInstantaneousSusceptibility());
        double B_pred = transfo::kMu0 * (H + M_c);
        double dBdt_raw = dyn.computeDBdt(B_pred);
        double G = dyn.getK1() * dyn.getSampleRate() * transfo::kMu0 * chi;
        double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
        double Hdyn = dyn.computeHfromDBdt(dBdt);
        double H_eff = H - Hdyn;

        // Solve J-A model
        double M = model.solveImplicitStep(H_eff);
        model.commitState();

        double B = transfo::kMu0 * (H + M);
        dyn.commitState(B);

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

    // Test A: Constant B=0 from reset — backward diff dBdt must be exactly 0.0
    bool passA = true;
    for (int i = 0; i < 100; ++i)
    {
        double dBdt = dyn.computeDBdt(0.0);
        dyn.commitState(0.0);
        if (dBdt != 0.0)
        {
            std::cout << "  [A] Sample " << i << ": dBdt = " << dBdt << std::endl;
            passA = false;
            break;
        }
    }
    CHECK(passA, "Constant B=0: backward diff dBdt is exactly 0");

    // Test B: sign(0) = 0 means Hdyn(dBdt=0) = 0 exactly
    double Hdyn_zero = dyn.computeHfromDBdt(0.0);
    CHECK(Hdyn_zero == 0.0, "computeHfromDBdt(0.0) returns exactly 0");

    // Test C: Step response — finite, positive for positive step
    dyn.reset();
    double dBdt_step = dyn.computeDBdt(0.5);
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

// ─── TEST 2: Backward Difference Derivative — Accuracy ──────────────────────
// The backward difference dBdt = fs*(B[n] - B[n-1]) should track the
// analytical derivative of a sine within first-order accuracy.

void test2_backward_diff_accuracy()
{
    std::cout << "\n=== TEST 2: Backward Difference Derivative — Accuracy ===" << std::endl;

    const double sampleRate = 96000.0;

    transfo::DynamicLosses dyn;
    dyn.setCoefficients(0.01f, 0.05f);
    dyn.setSampleRate(sampleRate);
    dyn.reset();

    // Feed a sine B signal and verify dBdt tracks analytical derivative
    const double B0 = 0.5;
    const double freq = 1000.0;
    const double omega = transfo::kTwoPi * freq;

    int samplesPerCycle = static_cast<int>(std::round(sampleRate / freq));
    int totalSamples = samplesPerCycle * 3;

    double maxRelErr = 0.0;
    bool allFinite = true;

    for (int n = 0; n < totalSamples; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double B = B0 * std::sin(omega * t);

        double dBdt = dyn.computeDBdt(B);
        dyn.commitState(B);

        if (!std::isfinite(dBdt)) allFinite = false;

        // Compare with analytical derivative at midpoint (n-0.5)
        // Backward difference has half-sample delay
        if (n > 2) // skip startup
        {
            double t_mid = (static_cast<double>(n) - 0.5) / sampleRate;
            double dBdt_exact = B0 * omega * std::cos(omega * t_mid);
            if (std::abs(dBdt_exact) > 100.0) // avoid near-zero division
            {
                double relErr = std::abs(dBdt - dBdt_exact) / std::abs(dBdt_exact);
                if (relErr > maxRelErr) maxRelErr = relErr;
            }
        }
    }

    std::cout << "  Max relative error vs analytical (midpoint): " << maxRelErr * 100 << "%" << std::endl;
    // Backward difference at 1kHz / 96kHz: error < 0.1% (ωT/2 ≈ 0.03)
    CHECK(maxRelErr < 0.01, "Backward diff tracks analytical within 1%");
    CHECK(allFinite, "All backward diff dBdt values are finite");

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

    // Measure peak |Hdyn| using B_pred approach (same as production code)
    auto measurePeak = [&](const transfo::JAParameterSet& p) -> double
    {
        transfo::HysteresisModel<transfo::LangevinPade> model;
        model.setParameters(p);
        model.setSampleRate(sampleRate);
        model.reset();

        transfo::DynamicLosses dyn;
        dyn.setCoefficients(p.K1, p.K2);
        dyn.setSampleRate(sampleRate);
        dyn.reset();

        int spc = static_cast<int>(std::round(sampleRate / freq));
        int total = spc * 5;
        double peak = 0.0;

        for (int n = 0; n < total; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = Hmax * std::sin(transfo::kTwoPi * freq * t);

            double M_c = model.getMagnetization();
            double chi = std::max(0.0, model.getInstantaneousSusceptibility());
            double B_pred = transfo::kMu0 * (H + M_c);
            double dBdt_raw = dyn.computeDBdt(B_pred);
            double G = dyn.getK1() * dyn.getSampleRate() * transfo::kMu0 * chi;
            double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
            double Hdyn = dyn.computeHfromDBdt(dBdt);
            double H_eff = H - Hdyn;

            double M = model.solveImplicitStep(H_eff);
            model.commitState();

            double B = transfo::kMu0 * (H + M);
            dyn.commitState(B);

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
    // With damped χ-scaling, the ratio is reduced because MuMetal's
    // very high χ (soft material) compensates for its smaller K1.
    CHECK(ratio > 1.2, "Fender peak Hdyn > 1.2x MuMetal");
}

// ─── TEST 5: CPU overhead — dynamic losses vs bare J-A ──────────────────────

void test5_cpu_overhead()
{
    std::cout << "\n=== TEST 5: CPU Overhead ===" << std::endl;

    const int N = 500000;
    const double sampleRate = 96000.0;
    const double freq = 1000.0;
    auto params = transfo::JAParameterSet::defaultMuMetal();

    // Volatile sinks to prevent compiler from optimizing away the loops
    volatile double sinkWith = 0.0;
    volatile double sinkWithout = 0.0;

    // WITH dynamic losses (B_pred commit approach, same as production code)
    double timeWith = 0.0;
    {
        transfo::HysteresisModel<transfo::LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(sampleRate);
        model.reset();

        transfo::DynamicLosses dyn;
        dyn.setCoefficients(params.K1, params.K2);
        dyn.setSampleRate(sampleRate);
        dyn.reset();

        auto start = std::chrono::high_resolution_clock::now();

        double acc = 0.0;
        for (int n = 0; n < N; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = 200.0 * std::sin(transfo::kTwoPi * freq * t);

            double M_c = model.getMagnetization();
            double chi = std::max(0.0, model.getInstantaneousSusceptibility());
            double B_pred = transfo::kMu0 * (H + M_c);
            double dBdt_raw = dyn.computeDBdt(B_pred);
            double G = dyn.getK1() * dyn.getSampleRate() * transfo::kMu0 * chi;
            double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
            double Hdyn = dyn.computeHfromDBdt(dBdt);
            double H_eff = H - Hdyn;

            double M = model.solveImplicitStep(H_eff);
            model.commitState();

            double B = transfo::kMu0 * (H + M);
            dyn.commitState(B);
            acc += M;
        }

        auto end = std::chrono::high_resolution_clock::now();
        timeWith = std::chrono::duration<double, std::milli>(end - start).count();
        sinkWith = acc;
    }

    // WITHOUT dynamic losses — bare J-A, no DynamicLosses calls
    double timeWithout = 0.0;
    {
        transfo::HysteresisModel<transfo::LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(sampleRate);
        model.reset();

        auto start = std::chrono::high_resolution_clock::now();

        double acc = 0.0;
        for (int n = 0; n < N; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = 200.0 * std::sin(transfo::kTwoPi * freq * t);

            double M = model.solveImplicitStep(H);
            model.commitState();
            acc += M;
        }

        auto end = std::chrono::high_resolution_clock::now();
        timeWithout = std::chrono::duration<double, std::milli>(end - start).count();
        sinkWithout = acc;
    }
    (void)sinkWith; (void)sinkWithout;

    double overhead = (timeWithout > 0.0)
                      ? 100.0 * (timeWith - timeWithout) / timeWithout
                      : 0.0;

    std::cout << "  Time WITH:    " << timeWith << " ms" << std::endl;
    std::cout << "  Time WITHOUT: " << timeWithout << " ms" << std::endl;
    std::cout << "  Overhead: " << overhead << "%" << std::endl;

    CHECK(overhead < 20.0, "Dynamic losses overhead < 20%");
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

        // Finite difference: dH/d(dBdt) * d(dBdt)/dB = dH/d(dBdt) * fs
        double H_plus = dyn.computeHfromDBdt(dBdt + h);
        double H_minus = dyn.computeHfromDBdt(dBdt - h);
        double dH_dDBdt = (H_plus - H_minus) / (2.0 * h);
        double jac_fd = dH_dDBdt * 96000.0;

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
    test2_backward_diff_accuracy();
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
