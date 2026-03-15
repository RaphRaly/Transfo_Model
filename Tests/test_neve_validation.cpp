// =============================================================================
// Test: Neve THD Validation — THD and frequency response tests for Neve presets
//
// Standalone test (no JUCE dependency) that validates the TransformerModel
// against real Marinair T1444 catalogue specifications (1972).
//
// Test groups:
//   1. THD validation: Neve 10468 at 40Hz, 500Hz, 1kHz, 10kHz
//   2. Frequency response: Neve 10468 ±tolerance 20Hz-20kHz
//   3. Comparative: Gapped vs ungapped, Neve vs Jensen material differences
//   4. Progressive saturation: THD monotonicity with increasing level
//
// Reference: Neve Drawing EDO 71/13 (22/3/72) + Marinair Type 1400-1500 Catalogue
//
// NOTE ON LEVELS AND THRESHOLDS:
//
// dBFS → dBu mapping (EBU R68 standard):
//   -18 dBFS = 0 dBu (nominal studio line level)
//    -8 dBFS = +10 dBu (near max operating level)
//    +2 dBFS = +20 dBu (transformer absolute max output)
//
// Our model maps digital amplitude to H field via hScale_ = a * 5.0.
// At 0 dBFS (amplitude 1.0), H = 5a = 400 A/m for NiFe50 — deep
// saturation corresponding to +18 dBu or higher. Test levels below
// are chosen to match the physical measurement conditions from the
// Marinair catalogue (nominal ≈ 0 dBu ≈ -18 dBFS).
//
// The Marinair T1444 catalogue specifies:
//   THD < 0.1% @ 40 Hz (max input +10 dB)
//   THD < 0.01% @ 500 Hz, 1 kHz, 10 kHz (nominal)
//   FR ±0.3 dB 20 Hz - 20 kHz
//
// The J-A numerical model is an approximation. The thresholds used here are
// RELAXED relative to the physical specs to account for:
//   - Simplified direct J-A bypass (no full HSIM WDF topology)
//   - Implicit NR solver numerical artifacts
//   - Absence of eddy current / anomalous loss modelling (K1=K2=0)
//   - CPWL/ADAA approximations in Realtime mode
//   - Air gap not modelled in direct J-A (only affects HSIM topology)
//
// Each test documents the physical spec and the relaxed model threshold.
//
// Pattern: same as test_plugin_integration.cpp (CHECK macro, main()).
// =============================================================================

#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/Presets.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/util/Constants.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <string>

static constexpr double PI = 3.14159265358979323846;

// ---- Test framework ---------------------------------------------------------

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

// ---- Type aliases -----------------------------------------------------------

using RealtimeModel = transfo::TransformerModel<transfo::CPWLLeaf>;

// =============================================================================
// Goertzel Algorithm — compute magnitude of a single frequency bin
// =============================================================================
// Returns the RMS amplitude of the signal at the target frequency.
// This is much more efficient than a full DFT when we only need a few bins.

static double goertzelMagnitude(const float* signal, int numSamples,
                                 double targetFreq, double sampleRate)
{
    // Goertzel algorithm: computes the DFT magnitude at a single frequency
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

    // Power at the frequency bin
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;

    // Convert to RMS amplitude: magnitude / (N/2)
    double magnitude = std::sqrt(power) / (static_cast<double>(numSamples) / 2.0);

    return magnitude;
}

// =============================================================================
// computeTHD — Total Harmonic Distortion via Goertzel
// =============================================================================
// Computes THD as: THD(%) = sqrt(H2^2 + H3^2 + ... + HN^2) / H1 * 100
//
// Parameters:
//   signal          : audio buffer to analyze
//   numSamples      : buffer length
//   fundamentalFreq : fundamental frequency in Hz
//   sampleRate      : sample rate in Hz
//   numHarmonics    : number of harmonics to include (2..N means harmonics 2 through N)

static double computeTHD(const float* signal, int numSamples,
                          double fundamentalFreq, double sampleRate,
                          int numHarmonics = 8)
{
    // Fundamental amplitude (H1)
    double h1 = goertzelMagnitude(signal, numSamples, fundamentalFreq, sampleRate);

    if (h1 < 1e-12)
        return 0.0; // Signal too weak to measure

    // Sum of squared harmonic amplitudes
    double sumHarmonicsSq = 0.0;

    for (int n = 2; n <= numHarmonics; ++n)
    {
        double harmonicFreq = fundamentalFreq * static_cast<double>(n);

        // Skip harmonics above Nyquist
        if (harmonicFreq >= sampleRate / 2.0)
            break;

        double hn = goertzelMagnitude(signal, numSamples, harmonicFreq, sampleRate);
        sumHarmonicsSq += hn * hn;
    }

    // THD in percent
    double thd = std::sqrt(sumHarmonicsSq) / h1 * 100.0;
    return thd;
}

// =============================================================================
// simulateTransformer — Run a TransformerModel and return the measurement buffer
// =============================================================================
// Creates a RealtimeModel (CPWLLeaf), configures it with the given preset,
// generates a sine at the specified frequency/amplitude, runs warm-up blocks
// to stabilize SmoothedValue ramps and hysteresis state, then returns the
// output of a single measurement block.
//
// Parameters:
//   cfg             : TransformerConfig preset to use
//   freq            : sine frequency in Hz
//   amplitudeDbFS   : input amplitude in dBFS (0 dBFS = amplitude 1.0)
//   sampleRate      : sample rate
//   numSamples      : measurement block size (should be large for THD accuracy)

static std::vector<float> simulateTransformer(transfo::TransformerConfig cfg,
                                               float freq,
                                               float amplitudeDbFS,
                                               float sampleRate,
                                               int numSamples)
{
    // Zero out dynamic loss coefficients — this test validates the static
    // J-A model only (Marinair T1444 specs assume quasi-static operation).
    cfg.material.K1 = 0.0f;
    cfg.material.K2 = 0.0f;

    RealtimeModel model;
    model.setProcessingMode(transfo::ProcessingMode::Realtime);
    model.setConfig(cfg);
    model.prepareToPlay(sampleRate, numSamples);
    model.setInputGain(0.0f);   // 0 dB — amplitude is encoded in the signal
    model.setOutputGain(0.0f);  // 0 dB
    model.setMix(1.0f);         // 100% wet

    float amplitude = std::pow(10.0f, amplitudeDbFS / 20.0f);

    std::vector<float> input(static_cast<size_t>(numSamples));
    std::vector<float> output(static_cast<size_t>(numSamples));

    // Generate phase-continuous sine across all blocks (warm-up + measurement)
    int totalWarmupSamples = 0;
    const int warmupBlocks = 5;

    // --- Warm-up: 5 blocks to stabilize SmoothedValue ramps + hysteresis state
    for (int b = 0; b < warmupBlocks; ++b)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            int sampleIndex = totalWarmupSamples + i;
            input[static_cast<size_t>(i)] = amplitude *
                std::sin(2.0f * static_cast<float>(PI) * freq *
                         static_cast<float>(sampleIndex) / sampleRate);
        }
        model.processBlock(input.data(), output.data(), numSamples);
        totalWarmupSamples += numSamples;
    }

    // --- Measurement block
    for (int i = 0; i < numSamples; ++i)
    {
        int sampleIndex = totalWarmupSamples + i;
        input[static_cast<size_t>(i)] = amplitude *
            std::sin(2.0f * static_cast<float>(PI) * freq *
                     static_cast<float>(sampleIndex) / sampleRate);
    }
    model.processBlock(input.data(), output.data(), numSamples);

    return output;
}

// ---- Helper: RMS of a buffer ------------------------------------------------

static double rms(const float* data, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return std::sqrt(sum / std::max(n, 1));
}

// =============================================================================
// 1. THD Validation Tests — Neve 10468 (Marinair T1444 specs)
// =============================================================================

void test1_thd_neve_10468()
{
    std::cout << "\n=== TEST 1: THD Validation — Neve 10468 (Marinair T1444 specs) ===" << std::endl;

    auto cfg = transfo::Presets::Neve_1073_Input();
    const float sampleRate = 96000.0f;  // High SR for better harmonic resolution
    const int N = 65536;                // Large block for Goertzel precision

    // -------------------------------------------------------------------------
    // 1a. THD @ 40 Hz, near max input (-10 dBFS ≈ +8 dBu)
    //
    // Physical spec (Marinair T1444): THD < 0.1% @ 40 Hz at max input (+10 dB)
    // Relaxed model threshold:        THD < 8.0%
    //
    // At -10 dBFS, H_peak ≈ 126 A/m (L(1.6) ≈ 0.46 — moderate saturation).
    // The J-A model overestimates THD vs the physical transformer due to
    // simplified direct bypass and no eddy current losses.
    // -------------------------------------------------------------------------
    {
        auto output = simulateTransformer(cfg, 40.0f, -10.0f, sampleRate, N);
        double thd = computeTHD(output.data(), N, 40.0, sampleRate, 8);

        std::cout << "    Neve 10468 THD @ 40 Hz, -10 dBFS (~+8 dBu): " << thd << "%" << std::endl;
        std::cout << "    (Physical spec: < 0.1%, model threshold: < 8.0%)" << std::endl;

        std::string msg = "Neve 10468 THD @ 40 Hz < 8.0% (relaxed from physical 0.1%), measured="
                          + std::to_string(thd) + "%";
        CHECK(thd < 8.0, msg.c_str());
    }

    // -------------------------------------------------------------------------
    // 1b. THD @ 500 Hz, nominal level (-20 dBFS ≈ 0 dBu)
    //
    // Physical spec (Marinair T1444): THD < 0.01%
    // Relaxed model threshold:        THD < 1.0%
    // -------------------------------------------------------------------------
    {
        auto output = simulateTransformer(cfg, 500.0f, -20.0f, sampleRate, N);
        double thd = computeTHD(output.data(), N, 500.0, sampleRate, 8);

        std::cout << "    Neve 10468 THD @ 500 Hz, -20 dBFS (~0 dBu): " << thd << "%" << std::endl;
        std::cout << "    (Physical spec: < 0.01%, model threshold: < 1.0%)" << std::endl;

        std::string msg = "Neve 10468 THD @ 500 Hz < 1.0% (relaxed from physical 0.01%), measured="
                          + std::to_string(thd) + "%";
        CHECK(thd < 1.0, msg.c_str());
    }

    // -------------------------------------------------------------------------
    // 1c. THD @ 1 kHz, nominal level (-20 dBFS ≈ 0 dBu)
    //
    // Physical spec (Marinair T1444): THD < 0.01%
    // Relaxed model threshold:        THD < 1.0%
    // -------------------------------------------------------------------------
    {
        auto output = simulateTransformer(cfg, 1000.0f, -20.0f, sampleRate, N);
        double thd = computeTHD(output.data(), N, 1000.0, sampleRate, 8);

        std::cout << "    Neve 10468 THD @ 1 kHz, -20 dBFS (~0 dBu): " << thd << "%" << std::endl;
        std::cout << "    (Physical spec: < 0.01%, model threshold: < 1.0%)" << std::endl;

        std::string msg = "Neve 10468 THD @ 1 kHz < 1.0% (relaxed from physical 0.01%), measured="
                          + std::to_string(thd) + "%";
        CHECK(thd < 1.0, msg.c_str());
    }

    // -------------------------------------------------------------------------
    // 1d. THD @ 10 kHz, nominal level (-20 dBFS ≈ 0 dBu)
    //
    // Physical spec (Marinair T1444): THD < 0.01%
    // Relaxed model threshold:        THD < 1.0%
    // -------------------------------------------------------------------------
    {
        auto output = simulateTransformer(cfg, 10000.0f, -20.0f, sampleRate, N);
        double thd = computeTHD(output.data(), N, 10000.0, sampleRate, 8);

        std::cout << "    Neve 10468 THD @ 10 kHz, -20 dBFS (~0 dBu): " << thd << "%" << std::endl;
        std::cout << "    (Physical spec: < 0.01%, model threshold: < 1.0%)" << std::endl;

        std::string msg = "Neve 10468 THD @ 10 kHz < 1.0% (relaxed from physical 0.01%), measured="
                          + std::to_string(thd) + "%";
        CHECK(thd < 1.0, msg.c_str());
    }
}

// =============================================================================
// 2. Frequency Response Tests — Neve 10468
// =============================================================================
// Physical spec (Marinair T1444): FR ±0.3 dB, 20 Hz - 20 kHz
// Relaxed model threshold:        FR ±3.0 dB
//
// The model uses simplified HP/LP filters for source impedance and load
// damping. The actual frequency response shape depends on Lp, L_leakage,
// source Z, and load Z — which are modelled but not perfectly calibrated.
// We relax to ±3 dB to catch gross errors while accepting model approximation.

void test2_frequency_response_neve_10468()
{
    std::cout << "\n=== TEST 2: Frequency Response — Neve 10468 ===" << std::endl;

    auto cfg = transfo::Presets::Neve_1073_Input();
    const float sampleRate = 96000.0f;
    const int N = 65536;
    const float testAmplitudeDb = -6.0f; // Moderate level to avoid saturation

    // Measure RMS at different frequencies
    struct FreqPoint {
        float freq;
        double rmsDb;
    };

    std::vector<FreqPoint> points;
    float testFreqs[] = { 20.0f, 100.0f, 1000.0f, 5000.0f, 10000.0f, 20000.0f };

    double refRmsDb = 0.0; // Will be set to the 1 kHz measurement

    for (float freq : testFreqs)
    {
        auto output = simulateTransformer(cfg, freq, testAmplitudeDb, sampleRate, N);

        // Measure RMS of the output
        double r = rms(output.data(), N);
        double db = (r > 1e-15) ? 20.0 * std::log10(r) : -300.0;

        points.push_back({freq, db});

        if (freq == 1000.0f)
            refRmsDb = db;
    }

    std::cout << "    Reference (1 kHz) RMS: " << refRmsDb << " dBFS" << std::endl;

    // Physical spec: ±0.3 dB
    // Relaxed model threshold: ±3.0 dB (accounting for filter approximations)
    const double toleranceDb = 3.0;

    for (const auto& pt : points)
    {
        double deviation = pt.rmsDb - refRmsDb;
        std::cout << "    " << pt.freq << " Hz: " << pt.rmsDb << " dBFS ("
                  << ((deviation >= 0) ? "+" : "") << deviation << " dB vs 1kHz)" << std::endl;

        std::string msg = "FR @ " + std::to_string(static_cast<int>(pt.freq))
                          + " Hz within +/-" + std::to_string(toleranceDb)
                          + " dB of 1kHz (physical spec: +/-0.3 dB), deviation="
                          + std::to_string(deviation) + " dB";
        CHECK(std::abs(deviation) < toleranceDb, msg.c_str());
    }
}

// =============================================================================
// 3. Comparative Tests — Gapped vs Ungapped, Neve vs Jensen
// =============================================================================

void test3_comparative()
{
    std::cout << "\n=== TEST 3: Comparative Tests ===" << std::endl;

    const float sampleRate = 96000.0f;
    const int N = 65536;

    // -------------------------------------------------------------------------
    // 3a. Neve Output gapped (LI1166) vs ungapped (LO2567 Hot)
    //
    // MODEL LIMITATION: The direct J-A bypass does not use core geometry
    // (air gap, effective length). Both presets use identical material
    // (defaultNiFe50) and identical winding config, so the hysteresis
    // produces identical output. When the full HSIM WDF topology is
    // implemented, the air gap reluctance will linearize the B-H curve
    // and this test should verify gapped THD < ungapped THD.
    //
    // For now, we validate that both presets produce reasonable THD.
    // -------------------------------------------------------------------------
    {
        auto cfgGapped   = transfo::Presets::Neve_1073_Output();   // LI1166, gapped
        auto cfgUngapped = transfo::Presets::Neve_LO2567_Hot();    // LO2567, ungapped

        float testLevel = -10.0f; // -10 dBFS ≈ +8 dBu (moderate drive)

        auto outGapped   = simulateTransformer(cfgGapped,   1000.0f, testLevel, sampleRate, N);
        auto outUngapped = simulateTransformer(cfgUngapped, 1000.0f, testLevel, sampleRate, N);

        double thdGapped   = computeTHD(outGapped.data(),   N, 1000.0, sampleRate, 8);
        double thdUngapped = computeTHD(outUngapped.data(), N, 1000.0, sampleRate, 8);

        std::cout << "    LI1166 (gapped)  THD @ 1kHz -10dBFS: " << thdGapped << "%" << std::endl;
        std::cout << "    LO2567 (ungapped) THD @ 1kHz -10dBFS: " << thdUngapped << "%" << std::endl;

        // Both should produce measurable but not extreme THD at this level
        bool bothReasonable = (thdGapped < 15.0) && (thdUngapped < 15.0) &&
                              (thdGapped > 0.01) && (thdUngapped > 0.01);
        CHECK(bothReasonable,
              "Gapped and ungapped both produce reasonable THD (0.01-15%)");

        if (std::abs(thdGapped - thdUngapped) < 1e-6)
            std::cout << "    -> NOTE: Identical THD (expected — direct J-A bypass ignores air gap)" << std::endl;
        else if (thdGapped < thdUngapped)
            std::cout << "    -> Gapped has lower THD (expected physical behavior)" << std::endl;
        else
            std::cout << "    -> Ungapped has lower THD" << std::endl;
    }

    // -------------------------------------------------------------------------
    // 3b. Jensen JT-115K-E THD vs Neve 10468 THD
    //
    // Jensen uses mu-metal (80% NiFe) -> higher permeability, lower coercivity
    // Neve uses NiFe 50% (Radiometal) -> harder saturation, more "punchy"
    //
    // At the same nominal level, the two materials should produce different
    // harmonic signatures.
    // -------------------------------------------------------------------------
    {
        auto cfgJensen = transfo::Presets::Jensen_JT115KE();
        auto cfgNeve   = transfo::Presets::Neve_1073_Input();

        float testLevel = 0.0f; // 0 dBFS

        auto outJensen = simulateTransformer(cfgJensen, 1000.0f, testLevel, sampleRate, N);
        auto outNeve   = simulateTransformer(cfgNeve,   1000.0f, testLevel, sampleRate, N);

        double thdJensen = computeTHD(outJensen.data(), N, 1000.0, sampleRate, 8);
        double thdNeve   = computeTHD(outNeve.data(),   N, 1000.0, sampleRate, 8);

        std::cout << "    Jensen JT-115K-E THD @ 1kHz 0dBFS: " << thdJensen << "%" << std::endl;
        std::cout << "    Neve 10468       THD @ 1kHz 0dBFS: " << thdNeve << "%" << std::endl;

        // The two use different JAParameterSets (defaultMuMetal vs defaultNiFe50)
        // so they MUST produce different THD values.
        bool areDifferent = std::abs(thdJensen - thdNeve) > 1e-6;
        CHECK(areDifferent,
              "Jensen (mu-metal) and Neve (NiFe 50%) produce different THD at same level");

        std::cout << "    Delta THD: " << std::abs(thdJensen - thdNeve) << "%" << std::endl;
    }
}

// =============================================================================
// 4. Progressive Saturation Test
// =============================================================================
// Physical behavior: THD must increase monotonically with input level.
// As the core approaches saturation, the B-H curve becomes more nonlinear,
// generating more harmonics.
//
// We test at 1 kHz with the Neve 10468 preset at levels from -20 to +15 dBFS.
// The THD values must form a monotonically increasing (or at least non-decreasing)
// sequence.

void test4_progressive_saturation()
{
    std::cout << "\n=== TEST 4: Progressive Saturation — THD vs Level ===" << std::endl;

    auto cfg = transfo::Presets::Neve_1073_Input();
    const float sampleRate = 96000.0f;
    const int N = 65536;
    const float freq = 1000.0f;

    float levels[] = { -20.0f, -10.0f, 0.0f, 6.0f, 10.0f, 15.0f };
    std::vector<double> thdValues;

    for (float level : levels)
    {
        auto output = simulateTransformer(cfg, freq, level, sampleRate, N);
        double thd = computeTHD(output.data(), N, static_cast<double>(freq), sampleRate, 8);
        thdValues.push_back(thd);

        std::cout << "    " << ((level >= 0) ? "+" : "") << level
                  << " dBFS: THD = " << thd << "%" << std::endl;
    }

    // Verify monotonic increase: each THD should be >= previous
    // We allow a small tolerance for numerical noise (0.001%)
    bool isMonotonic = true;
    for (size_t i = 1; i < thdValues.size(); ++i)
    {
        if (thdValues[i] < thdValues[i - 1] - 0.001)
        {
            isMonotonic = false;
            std::cout << "    *** Monotonicity violation: THD[" << i << "]="
                      << thdValues[i] << "% < THD[" << (i - 1) << "]="
                      << thdValues[i - 1] << "%" << std::endl;
        }
    }
    CHECK(isMonotonic, "THD increases monotonically with input level (physical behavior)");

    // Also verify that the highest level has significantly more THD than the lowest
    double thdRange = thdValues.back() - thdValues.front();
    std::cout << "    THD range (lowest to highest level): " << thdRange << "%" << std::endl;
    CHECK(thdRange > 0.001, "THD at +15 dBFS is measurably higher than at -20 dBFS");
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "  Neve THD Validation Test Suite" << std::endl;
    std::cout << "  Reference: Marinair T1444 Catalogue (1972)" << std::endl;
    std::cout << "================================================================" << std::endl;

    test1_thd_neve_10468();
    test2_frequency_response_neve_10468();
    test3_comparative();
    test4_progressive_saturation();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_fail > 0) ? 1 : 0;
}
