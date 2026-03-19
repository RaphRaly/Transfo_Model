// =============================================================================
// Test: NeveClassAPath — Neve Heritage Class-A amplifier WDF model validation
//
// Standalone test (no JUCE dependency) that validates the NeveClassAPath
// implementing the Neve Heritage 3-stage Class-A topology:
//   Q1 (BC184C) CE → Q2 (BC214C) CE → Q3 (BD139) EF output
//
// Test groups:
//   1.  Construction and configure
//   2.  Gain vs position (3 positions: 0, 5, 10)
//   3.  Signal passthrough (non-silent output)
//   4.  Gain increases with Rfb
//   5.  Frequency response shape
//   6.  Numerical stability
//   7.  processBlock consistency vs processSample
//   8.  Reset clears state
//   9.  Output impedance range
//  10.  Harmonics (informational DFT check)
//
// Pattern: standalone test, same CHECK macro as other tests in this project.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md, SPRINT_PLAN_PREAMP.md Sprint 3
// =============================================================================

#include "../core/include/core/preamp/NeveClassAPath.h"
#include "../core/include/core/preamp/GainTable.h"
#include "../core/include/core/model/PreampConfig.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

static constexpr double PI = 3.14159265358979323846;

// ---- Test framework ---------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

void CHECK(bool cond, const char* msg) {
    if (cond) { std::cout << "  PASS: " << msg << std::endl; g_pass++; }
    else      { std::cout << "  *** FAIL: " << msg << " ***" << std::endl; g_fail++; }
}

void CHECK_NEAR(double actual, double expected, double tol, const char* msg) {
    double err = std::abs(actual - expected);
    if (err <= tol) { std::cout << "  PASS: " << msg << " (err=" << err << ")" << std::endl; g_pass++; }
    else { std::cout << "  *** FAIL: " << msg << " (got=" << actual << ", expected=" << expected << ", err=" << err << ") ***" << std::endl; g_fail++; }
}

// ---- Helper: Goertzel single-bin magnitude ----------------------------------

static double goertzelMagnitude(const float* signal, int numSamples,
                                 double targetFreq, double sampleRate)
{
    const double k = targetFreq / sampleRate * static_cast<double>(numSamples);
    const double omega = 2.0 * PI * k / static_cast<double>(numSamples);
    const double coeff = 2.0 * std::cos(omega);

    double s0 = 0.0, s1 = 0.0, s2 = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        s0 = static_cast<double>(signal[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    double magnitude = std::sqrt(power) / (static_cast<double>(numSamples) / 2.0);
    return magnitude;
}

// ---- Helper: measure gain in dB via sine tone RMS ---------------------------

double measureGainDB(transfo::NeveClassAPath& path, float freq, float amplitude,
                     float sampleRate, int numCycles = 20)
{
    const int samplesPerCycle = static_cast<int>(sampleRate / freq);
    const int totalSamples = samplesPerCycle * numCycles;
    const int skipSamples = samplesPerCycle * 5;  // Skip transient

    double sumSqIn = 0.0, sumSqOut = 0.0;
    for (int i = 0; i < totalSamples; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        float input = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * t);
        float output = path.processSample(input);

        if (i >= skipSamples)
        {
            sumSqIn += static_cast<double>(input) * input;
            sumSqOut += static_cast<double>(output) * output;
        }
    }

    double rmsIn = std::sqrt(sumSqIn / (totalSamples - skipSamples));
    double rmsOut = std::sqrt(sumSqOut / (totalSamples - skipSamples));

    if (rmsIn < 1e-12) return -999.0;
    return 20.0 * std::log10(rmsOut / rmsIn);
}

// =============================================================================
// TEST 1 — Construction and configure
// =============================================================================

void test1_construction_and_configure()
{
    std::cout << "\n=== TEST 1: Construction and configure ===" << std::endl;

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;  // Default: BC184C / BC214C / BD139, Vcc=24V

    path.configure(config);
    path.prepare(96000.0f, 512);

    // Name should be "Neve Heritage"
    std::string name = path.getName();
    CHECK(name == "Neve Heritage",
          "getName() returns \"Neve Heritage\"");

    // Output impedance: emitter follower stage gives low Zout, typically ~11 Ohm
    float zOut = path.getOutputImpedance();
    std::cout << "    Output impedance: " << zOut << " Ohm" << std::endl;
    CHECK(zOut > 0.0f && zOut < 100.0f,
          "Output impedance > 0 and < 100 Ohm");
}

// =============================================================================
// TEST 2 — Gain vs position (3 positions tested)
// =============================================================================

void test2_gain_vs_position()
{
    std::cout << "\n=== TEST 2: Gain vs position (positions 0, 5, 10) ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float testFreq = 1000.0f;
    const float testAmplitude = 0.001f;  // 1 mV small signal

    // Positions to test: 0 (+10 dB), 5 (+30 dB), 10 (+50 dB)
    int positions[] = { 0, 5, 10 };

    for (int pos : positions)
    {
        transfo::NeveClassAPath path;
        transfo::NevePathConfig config;
        path.configure(config);
        path.prepare(sampleRate, 512);

        float rfb = transfo::GainTable::getRfb(pos);
        float expectedGainDB = transfo::GainTable::getGainDB(pos);

        path.setGain(rfb);
        path.reset();

        double measuredGainDB = measureGainDB(path, testFreq, testAmplitude, sampleRate);

        std::cout << "    Position " << pos << ": Rfb=" << rfb
                  << " Ohm, expected=" << expectedGainDB
                  << " dB, measured=" << measuredGainDB << " dB" << std::endl;

        // Relaxed tolerance: +/- 6 dB (WDF model with nonlinear transistors)
        double error = std::abs(measuredGainDB - static_cast<double>(expectedGainDB));
        std::string msg = "Gain at position " + std::to_string(pos)
                        + " within +/-6 dB of expected "
                        + std::to_string(expectedGainDB) + " dB";
        CHECK(error <= 6.0, msg.c_str());
    }
}

// =============================================================================
// TEST 3 — Signal passthrough (non-silent output)
// =============================================================================

void test3_signal_passthrough()
{
    std::cout << "\n=== TEST 3: Signal passthrough ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amplitude = 0.001f;  // 1 mV
    const int numSamples = 5000;

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;
    path.configure(config);
    path.prepare(sampleRate, 512);

    // Position 5: Rfb=1430, +30 dB
    path.setGain(transfo::GainTable::getRfb(5));
    path.reset();

    double sumSq = 0.0;
    bool hasNaN = false;
    bool hasInf = false;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        float input = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * t);
        float output = path.processSample(input);

        if (std::isnan(output)) hasNaN = true;
        if (std::isinf(output)) hasInf = true;
        sumSq += static_cast<double>(output) * output;
    }

    double rmsOut = std::sqrt(sumSq / numSamples);
    std::cout << "    Output RMS: " << rmsOut << std::endl;

    CHECK(rmsOut > 0.0, "Output RMS > 0 (path produces signal)");
    CHECK(!hasNaN && !hasInf, "Output contains no NaN/Inf");
}

// =============================================================================
// TEST 4 — Gain increases with Rfb
// =============================================================================

void test4_gain_increases_with_rfb()
{
    std::cout << "\n=== TEST 4: Gain increases with Rfb ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amplitude = 0.001f;  // 1 mV

    // Measure gain at position 0 (Rfb=100)
    transfo::NeveClassAPath pathLow;
    transfo::NevePathConfig config;
    pathLow.configure(config);
    pathLow.prepare(sampleRate, 512);
    pathLow.setGain(transfo::GainTable::getRfb(0));
    pathLow.reset();
    double gainLow = measureGainDB(pathLow, freq, amplitude, sampleRate);

    // Measure gain at position 10 (Rfb=14700)
    transfo::NeveClassAPath pathHigh;
    pathHigh.configure(config);
    pathHigh.prepare(sampleRate, 512);
    pathHigh.setGain(transfo::GainTable::getRfb(10));
    pathHigh.reset();
    double gainHigh = measureGainDB(pathHigh, freq, amplitude, sampleRate);

    double difference = gainHigh - gainLow;

    std::cout << "    Gain at position 0  (Rfb=100):   " << gainLow << " dB" << std::endl;
    std::cout << "    Gain at position 10 (Rfb=14700): " << gainHigh << " dB" << std::endl;
    std::cout << "    Difference: " << difference << " dB" << std::endl;

    CHECK(gainHigh > gainLow,
          "Gain at position 10 > gain at position 0 (higher Rfb = higher gain)");
    CHECK(difference > 10.0,
          "Gain difference > 10 dB (theoretical ~40 dB)");
}

// =============================================================================
// TEST 5 — Frequency response shape
// =============================================================================

void test5_frequency_response_shape()
{
    std::cout << "\n=== TEST 5: Frequency response shape ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float amplitude = 0.001f;  // 1 mV

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;
    path.configure(config);
    path.prepare(sampleRate, 512);

    // Position 5: +30 dB
    path.setGain(transfo::GainTable::getRfb(5));

    // Measure gain at 3 frequencies
    path.reset();
    double gain50 = measureGainDB(path, 50.0f, amplitude, sampleRate);

    path.reset();
    double gain1k = measureGainDB(path, 1000.0f, amplitude, sampleRate);

    path.reset();
    double gain10k = measureGainDB(path, 10000.0f, amplitude, sampleRate);

    std::cout << "    Gain @   50 Hz: " << gain50 << " dB" << std::endl;
    std::cout << "    Gain @ 1000 Hz: " << gain1k << " dB" << std::endl;
    std::cout << "    Gain @10000 Hz: " << gain10k << " dB" << std::endl;

    CHECK(gain1k > gain50 - 6.0,
          "Gain at 1 kHz > gain at 50 Hz - 6 dB (LF rolloff from coupling caps)");
    CHECK(gain1k > -999.0,
          "Gain at 1 kHz > -999 dB (signal passes through)");
}

// =============================================================================
// TEST 6 — Numerical stability
// =============================================================================

void test6_numerical_stability()
{
    std::cout << "\n=== TEST 6: Numerical stability ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const int numSamples = 5000;

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;
    path.configure(config);
    path.prepare(sampleRate, 512);
    path.setGain(transfo::GainTable::getRfb(5));
    path.reset();

    bool hasNaN = false;
    bool hasInf = false;
    float maxAbs = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        // Extreme input: +/- 1.0 V (way above normal transformer secondary levels)
        float input = 1.0f * std::sin(2.0f * static_cast<float>(PI) * freq * t);
        float output = path.processSample(input);

        if (std::isnan(output)) hasNaN = true;
        if (std::isinf(output)) hasInf = true;
        float absOut = std::abs(output);
        if (absOut > maxAbs) maxAbs = absOut;
    }

    std::cout << "    Max |output|: " << maxAbs << " V" << std::endl;

    CHECK(!hasNaN && !hasInf,
          "No NaN or Inf in output with extreme inputs");
    CHECK(maxAbs < 1000.0f,
          "Output is bounded (< 1000 V, not diverging)");
}

// =============================================================================
// TEST 7 — processBlock consistency
// =============================================================================

void test7_process_block_consistency()
{
    std::cout << "\n=== TEST 7: processBlock consistency ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amplitude = 0.001f;
    const int N = 256;

    // Generate input buffer
    std::vector<float> input(N);
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * t);
    }

    transfo::NevePathConfig config;

    // Path A: processSample one by one
    transfo::NeveClassAPath pathA;
    pathA.configure(config);
    pathA.prepare(sampleRate, N);
    pathA.setGain(transfo::GainTable::getRfb(5));
    pathA.reset();

    std::vector<float> outputSample(N);
    for (int i = 0; i < N; ++i)
        outputSample[i] = pathA.processSample(input[i]);

    // Path B: processBlock
    transfo::NeveClassAPath pathB;
    pathB.configure(config);
    pathB.prepare(sampleRate, N);
    pathB.setGain(transfo::GainTable::getRfb(5));
    pathB.reset();

    std::vector<float> outputBlock(N);
    pathB.processBlock(input.data(), outputBlock.data(), N);

    // Compare
    double maxDiff = 0.0;
    for (int i = 0; i < N; ++i)
    {
        double diff = std::abs(static_cast<double>(outputSample[i])
                             - static_cast<double>(outputBlock[i]));
        if (diff > maxDiff) maxDiff = diff;
    }

    std::cout << "    Max difference: " << maxDiff << std::endl;

    CHECK(maxDiff < 1e-6,
          "processSample and processBlock outputs match within 1e-6");
}

// =============================================================================
// TEST 8 — Reset clears state
// =============================================================================

void test8_reset_clears_state()
{
    std::cout << "\n=== TEST 8: Reset clears state ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amplitude = 0.001f;

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;
    path.configure(config);
    path.prepare(sampleRate, 512);
    path.setGain(transfo::GainTable::getRfb(5));
    path.reset();

    // Process 1000 samples of 1 kHz tone to build up state
    for (int i = 0; i < 1000; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        float input = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * t);
        path.processSample(input);
    }

    // Reset
    path.reset();

    // Process 100 samples of silence
    float lastOutput = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        lastOutput = path.processSample(0.0f);
    }

    std::cout << "    Output after reset + 100 silent samples: " << lastOutput << std::endl;

    CHECK(std::abs(lastOutput) < 0.01f,
          "Output after reset settles to near-zero (< 0.01)");
}

// =============================================================================
// TEST 9 — Output impedance range
// =============================================================================

void test9_output_impedance_range()
{
    std::cout << "\n=== TEST 9: Output impedance range ===" << std::endl;

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;
    path.configure(config);
    path.prepare(96000.0f, 512);

    // Test at multiple gain positions
    int positions[] = { 0, 3, 5, 8, 10 };

    for (int pos : positions)
    {
        path.setGain(transfo::GainTable::getRfb(pos));
        float zOut = path.getOutputImpedance();

        std::cout << "    Position " << pos << " (Rfb="
                  << transfo::GainTable::getRfb(pos)
                  << "): Zout = " << zOut << " Ohm" << std::endl;

        std::string msg = "Zout at position " + std::to_string(pos)
                        + " between 1 and 100 Ohm (EF stage)";
        CHECK(zOut >= 1.0f && zOut <= 100.0f, msg.c_str());
    }
}

// =============================================================================
// TEST 10 — Harmonics (informational DFT check)
// =============================================================================

void test10_harmonics()
{
    std::cout << "\n=== TEST 10: Harmonics (informational DFT check) ===" << std::endl;

    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amplitude = 0.01f;  // 10 mV moderate level
    const int warmupSamples = 2000;
    const int N = 8192;

    transfo::NeveClassAPath path;
    transfo::NevePathConfig config;
    path.configure(config);
    path.prepare(sampleRate, 512);
    path.setGain(transfo::GainTable::getRfb(5));
    path.reset();

    // Warmup phase
    for (int i = 0; i < warmupSamples; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        float input = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * t);
        path.processSample(input);
    }

    // Collect measurement samples
    std::vector<float> output(N);
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(warmupSamples + i) / sampleRate;
        float input = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * t);
        output[i] = path.processSample(input);
    }

    // Compute harmonic magnitudes via Goertzel
    double h1 = goertzelMagnitude(output.data(), N, 1000.0, sampleRate);
    double h2 = goertzelMagnitude(output.data(), N, 2000.0, sampleRate);
    double h3 = goertzelMagnitude(output.data(), N, 3000.0, sampleRate);

    std::cout << "    H1 (1 kHz): " << h1 << std::endl;
    std::cout << "    H2 (2 kHz): " << h2 << std::endl;
    std::cout << "    H3 (3 kHz): " << h3 << std::endl;

    if (h1 > 1e-12)
    {
        std::cout << "    H2/H1: " << (h2 / h1 * 100.0) << "%" << std::endl;
        std::cout << "    H3/H1: " << (h3 / h1 * 100.0) << "%" << std::endl;

        if (h2 > h3)
            std::cout << "    -> Even harmonics dominant (Class-A signature)" << std::endl;
        else
            std::cout << "    -> Odd harmonics dominant (unexpected for Class-A)" << std::endl;
    }

    // Noise floor threshold: any harmonic above 1e-10 is considered present
    double noiseFloor = 1e-10;
    bool harmonicsExist = (h2 > noiseFloor) || (h3 > noiseFloor);

    CHECK(harmonicsExist,
          "Harmonics exist (H2 > noise floor OR H3 > noise floor)");
    CHECK(h1 > noiseFloor,
          "Fundamental H1 is above noise floor (signal present)");
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "  Neve Heritage Class-A Path — Test Suite" << std::endl;
    std::cout << "  NeveClassAPath WDF model validation" << std::endl;
    std::cout << "================================================================" << std::endl;

    test1_construction_and_configure();
    test2_gain_vs_position();
    test3_signal_passthrough();
    test4_gain_increases_with_rfb();
    test5_frequency_response_shape();
    test6_numerical_stability();
    test7_process_block_consistency();
    test8_reset_clears_state();
    test9_output_impedance_range();
    test10_harmonics();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "================================================================" << std::endl;

    return g_fail;
}
