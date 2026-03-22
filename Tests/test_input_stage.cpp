// =============================================================================
// Test: InputStageWDF — Complete input circuit validation
//
// Validates the InputStageWDF (Sprint 2 of the preamp project) which wraps
// the transformer circuit with phantom supply, pad attenuator, termination
// network, and source impedance modeling.
//
// Test groups:
//   1.  Construction and configure (no crash)
//   2.  Zs_eff computation (SM57 / U87, phantom on/off, pad on/off)
//   3.  Signal passthrough (non-zero output, correct polarity)
//   4.  Gain measurement (1:10 vs 1:5 ratio)
//   5.  Pad attenuation
//   6.  Numerical stability (extreme inputs)
//   7.  processBlock consistency vs processSample
//   8.  Reset clears state
//   9.  Source impedance switching
//  10.  Turns ratio accessor
//
// Pattern: standalone test, same CHECK macro as other tests.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md, SPRINT_PLAN_PREAMP.md Sprint 2
// =============================================================================

#include "../core/include/core/preamp/InputStageWDF.h"
#include "../core/include/core/model/PreampConfig.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

using JALeaf = transfo::JilesAthertonLeaf<transfo::LangevinPade>;
using InputStage = transfo::InputStageWDF<JALeaf>;

static constexpr double PI = 3.14159265358979323846;

static int g_pass = 0;
static int g_fail = 0;

void CHECK(bool cond, const char* msg)
{
    if (cond) {
        std::cout << "  PASS: " << msg << std::endl;
        g_pass++;
    } else {
        std::cout << "  *** FAIL: " << msg << " ***" << std::endl;
        g_fail++;
    }
}

void CHECK_NEAR(double actual, double expected, double tol, const char* msg)
{
    double err = std::abs(actual - expected);
    if (err <= tol) {
        std::cout << "  PASS: " << msg << " (err=" << err << ")" << std::endl;
        g_pass++;
    } else {
        std::cout << "  *** FAIL: " << msg
                  << " -- expected " << expected << ", got " << actual
                  << " (err=" << err << ", tol=" << tol << ") ***" << std::endl;
        g_fail++;
    }
}

// ─── Helper: generate a 1kHz sine block ─────────────────────────────────────
static std::vector<float> generateSine(float amplitude, float freqHz,
                                       float sampleRate, int numSamples)
{
    std::vector<float> buf(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        buf[i] = static_cast<float>(amplitude * std::sin(2.0 * PI * freqHz * t));
    }
    return buf;
}

// ─── Helper: RMS of a buffer ─────────────────────────────────────────────────
static double computeRMS(const float* data, int n)
{
    double sum2 = 0.0;
    for (int i = 0; i < n; ++i)
        sum2 += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return std::sqrt(sum2 / n);
}

int main()
{
    using namespace transfo;

    std::cout << "=== InputStageWDF Tests ===" << std::endl;

    const float sampleRate = 44100.0f;

    // ── Test 1: Construction and configure (no crash) ────────────────────────
    std::cout << "\n--- Test 1: Construction and Configure ---" << std::endl;
    {
        InputStage stage;
        InputStageConfig config = PreampConfig::DualTopology().input;
        stage.prepare(sampleRate, config);
        stage.reset();
        CHECK(true, "InputStage prepare/reset without crash");
    }

    // ── Test 2: Zs_eff computation ───────────────────────────────────────────
    std::cout << "\n--- Test 2: Zs_eff Computation ---" << std::endl;
    {
        InputStage stage;
        InputStageConfig config = PreampConfig::DualTopology().input;

        // Case A: SM57 (Zmic=150), phantom ON, pad OFF
        // Zs_eff = 150 || 13620 (two 6810 Ohm in series) = 150*13620/(150+13620) ~ 148.37
        config.phantomEnabled = true;
        config.padEnabled = false;
        stage.prepare(sampleRate, config);
        stage.setSourceImpedance(150.0f);
        stage.setPadEnabled(false);
        float Zs_a = stage.getEffectiveSourceZ();
        std::cout << "    SM57 phantom ON, pad OFF: Zs_eff = " << Zs_a << std::endl;
        CHECK_NEAR(Zs_a, 148.37, 1.0, "SM57 phantom ON pad OFF: Zs_eff ~ 148.37");

        // Case B: SM57, phantom ON, pad ON
        // Zs_eff = 148.37 + 2*649 = 148.37 + 1298 ~ 1446.37
        stage.setPadEnabled(true);
        float Zs_b = stage.getEffectiveSourceZ();
        std::cout << "    SM57 phantom ON, pad ON:  Zs_eff = " << Zs_b << std::endl;
        CHECK_NEAR(Zs_b, 1446.37, 1.0, "SM57 phantom ON pad ON: Zs_eff ~ 1446.37");

        // Case C: U87 (Zmic=200), phantom ON, pad OFF
        // Zs_eff = 200 || 13620 = 200*13620/(200+13620) ~ 197.1
        stage.setPadEnabled(false);
        stage.setSourceImpedance(200.0f);
        float Zs_c = stage.getEffectiveSourceZ();
        std::cout << "    U87  phantom ON, pad OFF: Zs_eff = " << Zs_c << std::endl;
        CHECK_NEAR(Zs_c, 197.1, 1.0, "U87 phantom ON pad OFF: Zs_eff ~ 197.1");

        // Case D: SM57, phantom OFF, pad OFF
        // Zs_eff = 150 (just Zmic, no phantom shunt)
        config.phantomEnabled = false;
        config.padEnabled = false;
        stage.prepare(sampleRate, config);
        stage.setSourceImpedance(150.0f);
        float Zs_d = stage.getEffectiveSourceZ();
        std::cout << "    SM57 phantom OFF, pad OFF: Zs_eff = " << Zs_d << std::endl;
        CHECK_NEAR(Zs_d, 150.0, 1.0, "SM57 phantom OFF pad OFF: Zs_eff = 150");

        // Case E: SM57, phantom OFF, pad ON
        // Zs_eff = 150 + 2*649 = 1448
        stage.setPadEnabled(true);
        float Zs_e = stage.getEffectiveSourceZ();
        std::cout << "    SM57 phantom OFF, pad ON:  Zs_eff = " << Zs_e << std::endl;
        CHECK_NEAR(Zs_e, 1448.0, 1.0, "SM57 phantom OFF pad ON: Zs_eff = 1448");
    }

    // ── Test 3: Signal passthrough (non-zero output, correct polarity) ───────
    std::cout << "\n--- Test 3: Signal Passthrough ---" << std::endl;
    {
        InputStage stage;
        InputStageConfig config = PreampConfig::DualTopology().input;
        config.ratio = InputStageConfig::Ratio::X10;
        stage.prepare(sampleRate, config);
        stage.setSourceImpedance(150.0f);
        stage.reset();

        // 1kHz sine, 0.01V amplitude (typical mic level)
        auto input = generateSine(0.01f, 1000.0f, sampleRate, 1000);
        std::vector<float> output(1000);

        for (int i = 0; i < 1000; ++i)
            output[i] = stage.processSample(input[i]);

        // Check non-zero output (skip first few samples for settling)
        float maxOut = 0.0f;
        for (int i = 100; i < 1000; ++i)
            maxOut = std::max(maxOut, std::abs(output[i]));

        std::cout << "    Max output amplitude: " << maxOut << " V" << std::endl;
        CHECK(maxOut > 1e-6f, "Output is non-zero");

        // Polarity check: find first positive peak of input after settling,
        // verify the corresponding output is also positive (NPN transformer).
        bool polarityOK = false;
        for (int i = 200; i < 900; ++i) {
            if (input[i] > 0.005f && input[i] > input[i - 1] && input[i] >= input[i + 1]) {
                // Found a positive peak in input; check output in nearby window
                float outNearPeak = 0.0f;
                for (int j = std::max(i - 5, 0); j <= std::min(i + 5, 999); ++j)
                    outNearPeak = std::max(outNearPeak, output[j]);
                if (outNearPeak > 0.0f)
                    polarityOK = true;
                break;
            }
        }
        CHECK(polarityOK, "Correct polarity (positive input -> positive output)");
    }

    // ── Test 4: Gain measurement (1:10 vs 1:5) ──────────────────────────────
    // The J-A nonlinear transformer model includes magnetizing inductance,
    // leakage inductance, parasitic capacitances, and core hysteresis.
    // The actual voltage gain depends on the impedance match between source,
    // load, and the nonlinear core. With 13.7K termination (reflected 137Ω
    // to primary), the load is comparable to the source impedance (~148Ω),
    // causing significant voltage division. The leakage inductance (5mH)
    // adds further series impedance. We use a long settling time (0.5s)
    // and validate relative behavior (ratio difference ≈ 6 dB) plus
    // non-trivial output (gain > -20 dB).
    std::cout << "\n--- Test 4: Gain Measurement ---" << std::endl;
    {
        const int settleTime = 22050;  // 0.5s at 44.1kHz
        const int totalSamples = settleTime + 441; // + 10 cycles for measurement
        const int samplesPerCycle = 44; // 1kHz at 44.1kHz -> ~44 samples/cycle
        auto input = generateSine(0.01f, 1000.0f, sampleRate, totalSamples);

        // Measure input RMS of last full cycle
        double inputRMS = computeRMS(input.data() + totalSamples - samplesPerCycle,
                                     samplesPerCycle);

        // ── Ratio 1:10 ──
        float gain10_dB;
        {
            InputStage stage;
            InputStageConfig config = PreampConfig::DualTopology().input;
            config.ratio = InputStageConfig::Ratio::X10;
            stage.prepare(sampleRate, config);
            stage.setSourceImpedance(150.0f);
            stage.reset();

            std::vector<float> output(totalSamples);
            for (int i = 0; i < totalSamples; ++i)
                output[i] = stage.processSample(input[i]);

            double outputRMS = computeRMS(output.data() + totalSamples - samplesPerCycle,
                                          samplesPerCycle);
            gain10_dB = static_cast<float>(20.0 * std::log10(outputRMS / (inputRMS + 1e-30)));
            std::cout << "    Ratio 1:10 gain: " << gain10_dB << " dB" << std::endl;
        }

        // ── Ratio 1:5 ──
        float gain5_dB;
        {
            InputStage stage;
            InputStageConfig config = PreampConfig::DualTopology().input;
            config.ratio = InputStageConfig::Ratio::X5;
            stage.prepare(sampleRate, config);
            stage.setSourceImpedance(150.0f);
            stage.reset();

            std::vector<float> output(totalSamples);
            for (int i = 0; i < totalSamples; ++i)
                output[i] = stage.processSample(input[i]);

            double outputRMS = computeRMS(output.data() + totalSamples - samplesPerCycle,
                                          samplesPerCycle);
            gain5_dB = static_cast<float>(20.0 * std::log10(outputRMS / (inputRMS + 1e-30)));
            std::cout << "    Ratio 1:5  gain: " << gain5_dB << " dB" << std::endl;
        }

        // The absolute gain depends on the J-A model, loading, and leakage.
        // We verify: (a) non-trivial output (> -20 dB), (b) ratio difference ≈ 6 dB.
        CHECK(gain10_dB > -20.0f, "Ratio 1:10 gain > -20 dB (non-trivial output)");
        CHECK(gain5_dB > -20.0f,  "Ratio 1:5  gain > -20 dB (non-trivial output)");
        CHECK(gain10_dB > gain5_dB, "Ratio 1:10 gain > Ratio 1:5 gain");

        float gainDiff = gain10_dB - gain5_dB;
        std::cout << "    Gain difference (1:10 - 1:5): " << gainDiff << " dB (expected ~6 dB)"
                  << std::endl;
        CHECK(gainDiff >= 1.0f && gainDiff <= 9.0f,
              "Gain difference between ratios in [1, 9] dB");
    }

    // ── Test 5: Pad attenuation ──────────────────────────────────────────────
    std::cout << "\n--- Test 5: Pad Attenuation ---" << std::endl;
    {
        const int totalSamples = 4410;
        const int samplesPerCycle = 44;
        auto input = generateSine(0.01f, 1000.0f, sampleRate, totalSamples);

        // Without pad
        double rmsNoPad;
        {
            InputStage stage;
            InputStageConfig config = PreampConfig::DualTopology().input;
            config.padEnabled = false;
            stage.prepare(sampleRate, config);
            stage.setSourceImpedance(150.0f);
            stage.reset();

            std::vector<float> output(totalSamples);
            for (int i = 0; i < totalSamples; ++i)
                output[i] = stage.processSample(input[i]);

            rmsNoPad = computeRMS(output.data() + totalSamples - samplesPerCycle,
                                  samplesPerCycle);
            std::cout << "    RMS without pad: " << rmsNoPad << std::endl;
        }

        // With pad
        double rmsPad;
        {
            InputStage stage;
            InputStageConfig config = PreampConfig::DualTopology().input;
            config.padEnabled = true;
            stage.prepare(sampleRate, config);
            stage.setSourceImpedance(150.0f);
            stage.reset();

            std::vector<float> output(totalSamples);
            for (int i = 0; i < totalSamples; ++i)
                output[i] = stage.processSample(input[i]);

            rmsPad = computeRMS(output.data() + totalSamples - samplesPerCycle,
                                samplesPerCycle);
            std::cout << "    RMS with pad:    " << rmsPad << std::endl;
        }

        CHECK(rmsPad < rmsNoPad,
              "Pad ON produces lower output than pad OFF");
    }

    // ── Test 6: Numerical stability (extreme inputs) ─────────────────────────
    std::cout << "\n--- Test 6: Numerical Stability ---" << std::endl;
    {
        InputStage stage;
        InputStageConfig config = PreampConfig::DualTopology().input;
        stage.prepare(sampleRate, config);
        stage.setSourceImpedance(150.0f);
        stage.reset();

        float testValues[] = {
            0.0f,
            0.001f, -0.001f,
            0.01f, -0.01f,
            0.1f, -0.1f,
            1.0f, -1.0f
        };

        bool anyNaN = false;
        bool anyInf = false;

        // Single-sample test
        for (float v : testValues) {
            float out = stage.processSample(v);
            if (std::isnan(out)) anyNaN = true;
            if (std::isinf(out)) anyInf = true;
        }

        CHECK(!anyNaN, "No NaN for single-sample extreme inputs");
        CHECK(!anyInf, "No Inf for single-sample extreme inputs");

        // Sustained stability: 100 samples of each value
        anyNaN = false;
        anyInf = false;
        for (float v : testValues) {
            stage.reset();
            for (int i = 0; i < 100; ++i) {
                float out = stage.processSample(v);
                if (std::isnan(out)) anyNaN = true;
                if (std::isinf(out)) anyInf = true;
            }
        }

        CHECK(!anyNaN, "No NaN for sustained extreme inputs (100 samples each)");
        CHECK(!anyInf, "No Inf for sustained extreme inputs (100 samples each)");
    }

    // ── Test 7: processBlock consistency ─────────────────────────────────────
    std::cout << "\n--- Test 7: processBlock Consistency ---" << std::endl;
    {
        const int N = 256;
        auto input = generateSine(0.01f, 1000.0f, sampleRate, N);

        // Instance A: processSample one by one
        InputStage stageA;
        InputStageConfig configA = PreampConfig::DualTopology().input;
        stageA.prepare(sampleRate, configA);
        stageA.setSourceImpedance(150.0f);
        stageA.reset();

        std::vector<float> outA(N);
        for (int i = 0; i < N; ++i)
            outA[i] = stageA.processSample(input[i]);

        // Instance B: processBlock
        InputStage stageB;
        InputStageConfig configB = PreampConfig::DualTopology().input;
        stageB.prepare(sampleRate, configB);
        stageB.setSourceImpedance(150.0f);
        stageB.reset();

        std::vector<float> outB(N);
        stageB.processBlock(input.data(), outB.data(), N);

        // Compare outputs — should be bit-exact
        bool identical = true;
        double maxDiff = 0.0;
        for (int i = 0; i < N; ++i) {
            if (outA[i] != outB[i]) {
                identical = false;
                double diff = std::abs(static_cast<double>(outA[i]) -
                                       static_cast<double>(outB[i]));
                maxDiff = std::max(maxDiff, diff);
            }
        }

        std::cout << "    Bit-exact match: " << (identical ? "YES" : "NO")
                  << ", max diff: " << maxDiff << std::endl;
        CHECK(identical, "processBlock output is bit-exact with processSample");
    }

    // ── Test 8: Reset clears state ───────────────────────────────────────────
    std::cout << "\n--- Test 8: Reset Clears State ---" << std::endl;
    {
        InputStage stage;
        InputStageConfig config = PreampConfig::DualTopology().input;
        stage.prepare(sampleRate, config);
        stage.setSourceImpedance(150.0f);
        stage.reset();

        // Process a sine to build up state
        auto input = generateSine(0.01f, 1000.0f, sampleRate, 500);
        float lastOut = 0.0f;
        for (int i = 0; i < 500; ++i)
            lastOut = stage.processSample(input[i]);

        // Verify we have non-trivial output
        float maxBefore = 0.0f;
        for (int i = 400; i < 500; ++i) {
            float out = stage.processSample(input[i % 500]);
            maxBefore = std::max(maxBefore, std::abs(out));
        }
        CHECK(maxBefore > 1e-6f, "Non-zero output before reset");

        // Reset
        stage.reset();

        // Process one sample with zero input
        float outAfterReset = stage.processSample(0.0f);
        std::cout << "    Output after reset with zero input: " << outAfterReset << std::endl;
        CHECK(std::abs(outAfterReset) < 1e-4f,
              "Output near zero after reset (residual from reactive elements OK)");
    }

    // ── Test 9: Source impedance switching ────────────────────────────────────
    std::cout << "\n--- Test 9: Source Impedance Switching ---" << std::endl;
    {
        InputStage stage;
        InputStageConfig config = PreampConfig::DualTopology().input;
        config.phantomEnabled = true;
        config.padEnabled = false;
        stage.prepare(sampleRate, config);

        // SM57: Zmic = 150
        stage.setSourceImpedance(150.0f);
        float Zs_sm57 = stage.getEffectiveSourceZ();
        std::cout << "    SM57 (150 Ohm): Zs_eff = " << Zs_sm57 << std::endl;

        // U87: Zmic = 200
        stage.setSourceImpedance(200.0f);
        float Zs_u87 = stage.getEffectiveSourceZ();
        std::cout << "    U87  (200 Ohm): Zs_eff = " << Zs_u87 << std::endl;

        CHECK(Zs_u87 > Zs_sm57,
              "U87 (200 Ohm) has higher Zs_eff than SM57 (150 Ohm)");
        CHECK(std::abs(Zs_u87 - Zs_sm57) > 10.0f,
              "Impedance difference is meaningful (>10 Ohm)");
    }

    // ── Test 10: Turns ratio accessor ────────────────────────────────────────
    std::cout << "\n--- Test 10: Turns Ratio Accessor ---" << std::endl;
    {
        // Ratio 1:10
        {
            InputStage stage;
            InputStageConfig config = PreampConfig::DualTopology().input;
            config.ratio = InputStageConfig::Ratio::X10;
            stage.prepare(sampleRate, config);

            float n = stage.getTurnsRatio();
            std::cout << "    Ratio X10: getTurnsRatio() = " << n << std::endl;
            CHECK_NEAR(n, 10.0, 0.01, "getTurnsRatio() returns 10.0 for X10");
        }

        // Ratio 1:5
        {
            InputStage stage;
            InputStageConfig config = PreampConfig::DualTopology().input;
            config.ratio = InputStageConfig::Ratio::X5;
            stage.prepare(sampleRate, config);

            float n = stage.getTurnsRatio();
            std::cout << "    Ratio X5:  getTurnsRatio() = " << n << std::endl;
            CHECK_NEAR(n, 5.0, 0.01, "getTurnsRatio() returns 5.0 for X5");
        }
    }

    // ── Summary ──────────────────────────────────────────────────────────────
    std::cout << "\n=== InputStageWDF Results ===" << std::endl;
    std::cout << "  Total: " << (g_pass + g_fail) << "  Pass: " << g_pass
              << "  Fail: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
