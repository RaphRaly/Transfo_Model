// =============================================================================
// Test: Flux Integrator (Problem #4 Fix Validation)
//
// Validates frequency-dependent saturation via leaky integrator/differentiator
// compander pair around the J-A nonlinearity.
//
// Test scenarios:
//   1. Linear gain flatness: integrate x differentiate = unity at all freqs
//   2. Frequency-dependent THD: more saturation at 50 Hz than 1 kHz
//   3. Calibration match: identical output at f_ref with/without integrator
//   4. DC stability: no drift after 100k samples of DC input
//   5. Frequency response of integrator alone: 1/f gain characteristic
//
// Reference: Faraday's law — B_peak = V/(2*pi*f*N*A), saturation proportional to 1/f.
// =============================================================================

#include "test_common.h"
#include "../core/include/core/magnetics/FluxIntegrator.h"
#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/magnetics/DynamicLosses.h"
#include "../core/include/core/util/Constants.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

using namespace transfo;

// Minimum warmup samples to let the leaky integrator settle (>= 5*tau).
// tau = 1/(2*pi*f_hp) in seconds; at 48kHz with f_hp=0.5Hz: tau~15279 samples.
static constexpr int kMinWarmupSamples = 80000;

static int warmupFor(double sampleRate, double freq)
{
    int samplesPerCycle = static_cast<int>(sampleRate / freq);
    return (std::max)(kMinWarmupSamples, samplesPerCycle * 5);
}

// ---- Test 1: Linear gain flatness (integrate x differentiate = 1) -----------

void test_linear_gain_flatness()
{
    std::printf("\n=== Flux Integrator: Linear Gain Flatness ===\n");

    const double sampleRate = 48000.0;
    const double f_ref = 1000.0;

    // Test at multiple frequencies.
    // The integrate(x) -> differentiate(y) product is algebraically = x at ALL z,
    // so this should pass even with short warmup. The product cancels exactly.
    double freqs[] = { 20.0, 50.0, 200.0, 1000.0, 5000.0, 15000.0 };
    const int numFreqs = 6;

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        FluxIntegrator fi_obj;
        fi_obj.configure(sampleRate, f_ref, 0.5);
        fi_obj.reset();

        double freq = freqs[fi];
        int samplesPerCycle = static_cast<int>(sampleRate / freq);
        int warmup = samplesPerCycle * 3;
        int measure = samplesPerCycle * 4;

        // Warmup
        for (int n = 0; n < warmup; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double x = std::sin(2.0 * test::kPi * freq * t);
            double integrated = fi_obj.integrate(x);
            fi_obj.differentiate(integrated);
        }

        // Measure: compute RMS of input and output
        double rmsIn = 0.0, rmsOut = 0.0;
        for (int n = 0; n < measure; ++n)
        {
            double t = static_cast<double>(n + warmup) / sampleRate;
            double x = std::sin(2.0 * test::kPi * freq * t);
            double integrated = fi_obj.integrate(x);
            double output = fi_obj.differentiate(integrated);
            rmsIn += x * x;
            rmsOut += output * output;
        }
        rmsIn = std::sqrt(rmsIn / measure);
        rmsOut = std::sqrt(rmsOut / measure);

        double gainRatio = rmsOut / (rmsIn + 1e-30);
        double gainErrorPercent = std::abs(gainRatio - 1.0) * 100.0;

        std::printf("  %6.0f Hz: gain = %.6f (error = %.4f%%)\n",
                    freq, gainRatio, gainErrorPercent);

        double tolerance = 0.5;
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "Gain flatness at %.0f Hz (%.4f%% < %.1f%%)",
            freq, gainErrorPercent, tolerance);
        CHECK(gainErrorPercent < tolerance, msg);
    }
}

// ---- Test 2: Frequency-dependent THD (more at 50 Hz than 1 kHz) ------------

void test_frequency_dependent_thd()
{
    std::printf("\n=== Flux Integrator: Frequency-Dependent THD ===\n");

    auto params = JAParameterSet::defaultMuMetal();
    const double sampleRate = 48000.0;
    const double f_ref = 1000.0;
    // hScale that produces moderate saturation at f_ref
    const double hScale = static_cast<double>(params.a) * 5.0;

    double testFreqs[] = { 50.0, 200.0, 1000.0, 5000.0 };
    double thds[4] = {};

    for (int fi = 0; fi < 4; ++fi)
    {
        double freq = testFreqs[fi];
        int samplesPerCycle = static_cast<int>(sampleRate / freq);
        int warmup = warmupFor(sampleRate, freq);
        int measure = samplesPerCycle * 4;

        HysteresisModel<LangevinPade> hyst;
        hyst.setParameters(params);
        hyst.setSampleRate(sampleRate);
        hyst.reset();

        FluxIntegrator fluxInt;
        fluxInt.configure(sampleRate, f_ref, 0.5);
        fluxInt.reset();

        // Warmup — run long enough for integrator to reach steady state
        for (int n = 0; n < warmup; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double V = 0.3 * std::sin(2.0 * test::kPi * freq * t);
            double V_flux = fluxInt.integrate(V);
            double H = V_flux * hScale;
            double M = hyst.solveImplicitStep(H);
            hyst.commitState();
            double B = 1.2566370614359173e-6 * (H + M);
            fluxInt.differentiate(B);
        }

        // Measure
        std::vector<float> output(measure);
        for (int n = 0; n < measure; ++n)
        {
            double t = static_cast<double>(n + warmup) / sampleRate;
            double V = 0.3 * std::sin(2.0 * test::kPi * freq * t);
            double V_flux = fluxInt.integrate(V);
            double H = V_flux * hScale;
            double M = hyst.solveImplicitStep(H);
            hyst.commitState();
            double B = 1.2566370614359173e-6 * (H + M);
            double out = fluxInt.differentiate(B);
            output[n] = static_cast<float>(out);
        }

        thds[fi] = test::computeTHD(output.data(), measure, freq, sampleRate, 8);
        std::printf("  %5.0f Hz: THD = %.3f%%\n", freq, thds[fi]);
    }

    // Core physics: saturation proportional to 1/f -> THD at 50 Hz >> THD at 1 kHz
    CHECK(thds[0] > thds[2],
        "THD at 50 Hz > THD at 1000 Hz (more saturation at low freq)");
    CHECK(thds[1] > thds[2],
        "THD at 200 Hz > THD at 1000 Hz (more saturation at low freq)");
    CHECK(thds[2] > thds[3],
        "THD at 1000 Hz > THD at 5000 Hz (less saturation at high freq)");

    // Quantitative: at 50 Hz, flux is 20x larger than at 1 kHz -> much more THD
    if (thds[2] > 0.001)
    {
        double ratio_50_to_1k = thds[0] / thds[2];
        std::printf("  THD ratio (50Hz/1kHz) = %.1f\n", ratio_50_to_1k);
        CHECK(ratio_50_to_1k > 2.0,
            "THD at 50 Hz is at least 2x higher than at 1 kHz");
    }
}

// ---- Test 3: Calibration match at f_ref ------------------------------------
//
// Root cause of original failure: the differentiation step in the compander
// multiplies the k-th harmonic amplitude by k (Faraday: EMF = N·dΦ/dt).
// This inherently increases THD in the output *voltage* compared to the
// flux waveform B. The "no integrator" path outputs B (flux-proportional);
// the integrator path outputs dΦ/dt (voltage). Comparing THD between these
// two compares different physical quantities — they MUST differ.
//
// Correct checks at f_ref:
//   (a) Fundamental amplitude transparency (Goertzel): gain ≈ 1
//   (b) RMS transparency: levels match within tolerance
//   (c) THD with integrator is bounded and > THD without (physically expected)
//   (d) Internal B waveform (pre-differentiation) has similar THD to no-integrator B

void test_calibration_match_at_fref()
{
    std::printf("\n=== Flux Integrator: Calibration Match at f_ref ===\n");

    auto params = JAParameterSet::defaultMuMetal();
    const double sampleRate = 48000.0;
    const double f_ref = 1000.0;
    const double hScale = static_cast<double>(params.a) * 5.0;
    const double freq = f_ref;

    int samplesPerCycle = static_cast<int>(sampleRate / freq);
    int warmup = warmupFor(sampleRate, freq);
    int measure = samplesPerCycle * 4;

    // ── Run WITHOUT flux integrator ──
    HysteresisModel<LangevinPade> hyst_ref;
    hyst_ref.setParameters(params);
    hyst_ref.setSampleRate(sampleRate);
    hyst_ref.reset();

    for (int n = 0; n < warmup; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double V = 0.2 * std::sin(2.0 * test::kPi * freq * t);
        double H = V * hScale;
        hyst_ref.solveImplicitStep(H);
        hyst_ref.commitState();
    }

    std::vector<float> out_ref(measure);
    for (int n = 0; n < measure; ++n)
    {
        double t = static_cast<double>(n + warmup) / sampleRate;
        double V = 0.2 * std::sin(2.0 * test::kPi * freq * t);
        double H = V * hScale;
        double M = hyst_ref.solveImplicitStep(H);
        hyst_ref.commitState();
        double B = 1.2566370614359173e-6 * (H + M);
        out_ref[n] = static_cast<float>(B);
    }

    // ── Run WITH flux integrator — capture both B (pre-diff) and output (post-diff) ──
    HysteresisModel<LangevinPade> hyst_fi;
    hyst_fi.setParameters(params);
    hyst_fi.setSampleRate(sampleRate);
    hyst_fi.reset();

    FluxIntegrator fluxInt;
    fluxInt.configure(sampleRate, f_ref, 0.5);
    fluxInt.reset();

    for (int n = 0; n < warmup; ++n)
    {
        double t = static_cast<double>(n) / sampleRate;
        double V = 0.2 * std::sin(2.0 * test::kPi * freq * t);
        double V_flux = fluxInt.integrate(V);
        double H = V_flux * hScale;
        double M = hyst_fi.solveImplicitStep(H);
        hyst_fi.commitState();
        double B = 1.2566370614359173e-6 * (H + M);
        fluxInt.differentiate(B);
    }

    std::vector<float> out_fi(measure);       // post-differentiation (voltage)
    std::vector<float> out_fi_B(measure);     // pre-differentiation (flux)
    for (int n = 0; n < measure; ++n)
    {
        double t = static_cast<double>(n + warmup) / sampleRate;
        double V = 0.2 * std::sin(2.0 * test::kPi * freq * t);
        double V_flux = fluxInt.integrate(V);
        double H = V_flux * hScale;
        double M = hyst_fi.solveImplicitStep(H);
        hyst_fi.commitState();
        double B = 1.2566370614359173e-6 * (H + M);
        out_fi_B[n] = static_cast<float>(B);
        double out = fluxInt.differentiate(B);
        out_fi[n] = static_cast<float>(out);
    }

    // ── Measurements ──
    double rms_ref = test::computeRMS(out_ref.data(), measure);
    double rms_fi = test::computeRMS(out_fi.data(), measure);
    double rmsError = std::abs(rms_fi - rms_ref) / (rms_ref + 1e-30) * 100.0;

    // Goertzel fundamental amplitude (more precise than RMS — isolates the tone)
    double fund_ref = test::goertzelMagnitude(out_ref.data(), measure, freq, sampleRate);
    double fund_fi = test::goertzelMagnitude(out_fi.data(), measure, freq, sampleRate);
    double fundError = std::abs(fund_fi - fund_ref) / (fund_ref + 1e-30) * 100.0;

    // THD of each path
    double thd_ref = test::computeTHD(out_ref.data(), measure, freq, sampleRate, 8);
    double thd_fi = test::computeTHD(out_fi.data(), measure, freq, sampleRate, 8);

    // Internal B waveform THD (pre-differentiation, should be close to no-integrator B)
    double thd_fi_B = test::computeTHD(out_fi_B.data(), measure, freq, sampleRate, 8);
    double thdBDiff = std::abs(thd_fi_B - thd_ref);

    std::printf("  RMS (no integrator)     = %.6e\n", rms_ref);
    std::printf("  RMS (with integrator)   = %.6e\n", rms_fi);
    std::printf("  RMS error               = %.2f%%\n", rmsError);
    std::printf("  Fundamental (no integ)  = %.6e\n", fund_ref);
    std::printf("  Fundamental (with integ)= %.6e\n", fund_fi);
    std::printf("  Fundamental error       = %.2f%%\n", fundError);
    std::printf("  THD (no integrator, B)  = %.3f%%\n", thd_ref);
    std::printf("  THD (integrator, B pre) = %.3f%%\n", thd_fi_B);
    std::printf("  THD (integrator, dB/dt) = %.3f%%\n", thd_fi);
    std::printf("  B-waveform THD diff     = %.3f%%\n", thdBDiff);
    std::printf("  THD ratio (dB/dt vs B)  = %.2fx (expected >1: differentiation amplifies harmonics)\n",
                thd_fi / (thd_ref + 1e-30));

    // (a) RMS amplitude transparency at f_ref
    CHECK(rmsError < 5.0,
        "RMS match at f_ref within 5% (amplitude transparent at calibration freq)");

    // (b) Fundamental amplitude transparency (Goertzel, more precise)
    CHECK(fundError < 5.0,
        "Fundamental amplitude match at f_ref within 5%");

    // (c) Output THD is bounded and physically reasonable
    CHECK(thd_fi < 50.0,
        "Output THD with integrator < 50% (no runaway distortion)");
    CHECK(thd_fi > 0.1,
        "Output THD with integrator > 0.1% (nonlinearity is active)");

    // (d) Differentiation amplifies harmonics: THD_output > THD_B (Faraday's law)
    CHECK(thd_fi > thd_ref,
        "Output THD > B-waveform THD (dPhi/dt amplifies harmonics by factor k)");

    // (e) Internal B waveform THD close to no-integrator B THD
    //     (both represent flux in the core — should agree if integrator is transparent)
    CHECK(thdBDiff < 10.0,
        "Internal B-waveform THD within 10% of no-integrator (flux-domain consistency)");
}

// ---- Test 4: DC stability (no drift) ----------------------------------------

void test_dc_stability()
{
    std::printf("\n=== Flux Integrator: DC Stability ===\n");

    FluxIntegrator fi;
    fi.configure(48000.0, 1000.0, 0.5);
    fi.reset();

    // Feed 100k samples of DC = 1.0
    double maxIntOutput = 0.0;
    for (int n = 0; n < 100000; ++n)
    {
        double out = fi.integrate(1.0);
        double absOut = std::abs(out);
        if (absOut > maxIntOutput) maxIntOutput = absOut;
    }

    // DC gain = normGain * Ts / (1 - pole)
    //         = 2pi*1000 / (2pi*0.5) = 2000  (approximately)
    std::printf("  Max integrator output after 100k DC samples: %.1f\n", maxIntOutput);
    CHECK(std::isfinite(maxIntOutput), "Integrator output is finite (no divergence)");
    CHECK(maxIntOutput < 3000.0, "DC gain bounded (leaky pole prevents infinite accumulation)");

    // Verify convergence (last two outputs nearly identical)
    double settled = fi.integrate(1.0);
    double prev = fi.integrate(1.0);
    double change = std::abs(settled - prev) / (std::abs(settled) + 1e-30);
    std::printf("  Convergence rate: %.2e (relative change per sample)\n", change);
    CHECK(change < 1e-4, "Integrator has converged (settled to steady state)");
}

// ---- Test 5: 1/f frequency response of integrator alone ---------------------

void test_integrator_frequency_response()
{
    std::printf("\n=== Flux Integrator: 1/f Frequency Response ===\n");

    const double sampleRate = 48000.0;
    const double f_ref = 1000.0;

    double freqs[] = { 50.0, 100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 };
    double gains[7] = {};

    for (int fi = 0; fi < 7; ++fi)
    {
        FluxIntegrator fluxInt;
        fluxInt.configure(sampleRate, f_ref, 0.5);
        fluxInt.reset();

        double freq = freqs[fi];
        int samplesPerCycle = static_cast<int>(sampleRate / freq);
        // Long warmup: >= 5*tau of leaky integrator to reach steady state.
        int warmup = warmupFor(sampleRate, freq);
        int measure = samplesPerCycle * 4;

        // Warmup
        for (int n = 0; n < warmup; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            fluxInt.integrate(std::sin(2.0 * test::kPi * freq * t));
        }

        // Measure: use Goertzel at the fundamental to get the sinusoidal amplitude
        // (avoids RMS being inflated by any residual DC transient)
        std::vector<float> buffer(measure);
        for (int n = 0; n < measure; ++n)
        {
            double t = static_cast<double>(n + warmup) / sampleRate;
            double x = std::sin(2.0 * test::kPi * freq * t);
            double y = fluxInt.integrate(x);
            buffer[n] = static_cast<float>(y);
        }

        // Goertzel gives peak amplitude of the sinusoidal component
        double ampOut = test::goertzelMagnitude(buffer.data(), measure, freq, sampleRate);
        double ampIn = 1.0;  // Input is unit amplitude sine
        gains[fi] = ampOut / ampIn;
        double expected = f_ref / freq;

        std::printf("  %6.0f Hz: gain = %.4f, expected ~%.4f (f_ref/f)\n",
                    freq, gains[fi], expected);
    }

    // At 1000 Hz (f_ref): gain should be ~1
    CHECK_NEAR(gains[3], 1.0, 0.05, "Integrator gain = 1.0 at f_ref (1000 Hz)");

    // 50 Hz: gain should be ~20 (1000/50)
    CHECK(gains[0] > 17.0, "Integrator gain > 17 at 50 Hz (expected ~20)");
    CHECK(gains[0] < 23.0, "Integrator gain < 23 at 50 Hz (expected ~20)");

    // 5000 Hz: gain should be ~0.2 (1000/5000)
    CHECK(gains[5] > 0.17, "Integrator gain > 0.17 at 5000 Hz (expected ~0.2)");
    CHECK(gains[5] < 0.23, "Integrator gain < 0.23 at 5000 Hz (expected ~0.2)");

    // Monotonically decreasing with frequency
    for (int fi = 0; fi < 6; ++fi)
    {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "Gain at %.0f Hz > gain at %.0f Hz (1/f monotonic)",
            freqs[fi], freqs[fi + 1]);
        CHECK(gains[fi] > gains[fi + 1], msg);
    }
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  Flux Integrator Test Suite (Problem #4 Validation)\n");
    std::printf("================================================================\n");

    test_linear_gain_flatness();
    test_frequency_dependent_thd();
    test_calibration_match_at_fref();
    test_dc_stability();
    test_integrator_frequency_response();

    return test::printSummary("test_flux_integrator");
}
