// =============================================================================
// test_harrison_integration.cpp — Integration test: Harrison Mic Pre + real
//                                  TransformerModel<CPWLLeaf> (Jensen JT-115K-E)
//
// Tests:
//   1. End-to-end gain at 1 kHz (no PAD)
//   2. PAD attenuation (~20 dB)
//   3. Phase reverse (output inverted)
//   4. Gain range (max vs min mic gain)
//   5. Transformer saturation (THD increases with level)
//   6. Frequency response sweep
// =============================================================================

#include "../core/include/core/harrison/HarrisonMicPre.h"
#include "../core/include/core/harrison/ComponentValues.h"
#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/Presets.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <complex>
#include <algorithm>
#include <numeric>

static constexpr double PI_D = 3.14159265358979323846;

// ─── Helpers ────────────────────────────────────────────────────────────────

static int testCount = 0;
static int passCount = 0;

static void check(bool condition, const char* name, const char* detail = "")
{
    testCount++;
    if (condition) { passCount++; std::printf("  [PASS] %s %s\n", name, detail); }
    else           {              std::printf("  [FAIL] %s %s\n", name, detail); }
}

static void checkApprox(double actual, double expected, double tol, const char* name)
{
    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(actual=%.6f, expected=%.6f, tol=%.4e)", actual, expected, tol);
    check(std::abs(actual - expected) < tol, name, detail);
}

// Compute RMS of a contiguous range
static double computeRMS(const float* data, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return std::sqrt(sum / count);
}

// Generate sine tone into buffer
static void generateSine(float* buf, int numSamples, double freq, double sampleRate, double amplitude)
{
    for (int i = 0; i < numSamples; ++i)
        buf[i] = static_cast<float>(amplitude * std::sin(2.0 * PI_D * freq * i / sampleRate));
}

// Create and prepare a full mic pre with real transformer
struct MicPreSetup
{
    transfo::TransformerModel<transfo::CPWLLeaf> transformer;
    Harrison::MicPre::HarrisonMicPre<transfo::TransformerModel<transfo::CPWLLeaf>> micPre;

    void prepare(float sampleRate, int blockSize)
    {
        auto jensenCfg = transfo::Presets::getByIndex(0);  // Jensen JT-115K-E (1:10)
        transformer.setConfig(jensenCfg);
        transformer.setProcessingMode(transfo::ProcessingMode::Realtime);
        transformer.prepareToPlay(sampleRate, blockSize);

        micPre.setTransformer(&transformer);
        micPre.prepareToPlay(sampleRate, blockSize);
    }
};

// Warmup: run silence through the chain to settle transformer + smoothers
static void warmup(MicPreSetup& setup, float sampleRate, int warmupMs)
{
    int warmupSamples = static_cast<int>(sampleRate * warmupMs / 1000.0f);
    std::vector<float> silence(warmupSamples, 0.0f);
    setup.micPre.processBlock(silence.data(), warmupSamples);
}

// ===== TEST 1: End-to-end gain at 1 kHz (no PAD, max gain) =====
static void testEndToEndGain1kHz()
{
    std::printf("\n--- Test 1: End-to-End Gain at 1 kHz (no PAD) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    MicPreSetup setup;
    setup.prepare(sampleRate, blockSize);

    setup.micPre.setMicGain(1.0f);       // max gain (alpha=0)
    setup.micPre.setPadEnabled(false);
    setup.micPre.setPhaseReverse(false);
    setup.micPre.setSourceImpedance(150.0f);

    // Warmup: 200ms of silence
    warmup(setup, sampleRate, 200);

    // Stimulus: 0.01 amplitude 1 kHz sine for 100ms
    const int testLen = static_cast<int>(sampleRate * 0.1f); // 4800 samples
    std::vector<float> signal(testLen);
    generateSine(signal.data(), testLen, 1000.0, sampleRate, 0.01);

    // Keep a copy of the input for RMS calculation
    std::vector<float> inputCopy = signal;

    // Process
    setup.micPre.processBlock(signal.data(), testLen);

    // Measure RMS gain of last 2048 samples (steady state)
    const int measureLen = 2048;
    const int offset = testLen - measureLen;

    double rmsOut = computeRMS(signal.data() + offset, measureLen);
    double rmsIn  = computeRMS(inputCopy.data() + offset, measureLen);
    double measuredGain = rmsOut / rmsIn;

    // Expected: A_term * N * H_analog(1kHz, alpha=0)
    //   A_term = R100/(R100 + 75) = 6800/6875 ~ 0.98909
    //   N = 10 (Jensen 1:10)
    //   H_analog(1kHz, alpha=0) ~ 1.2687
    //   Total ~ 12.55
    const double A_term = 6800.0 / 6875.0;
    const double N = 10.0;
    const double H_1kHz = 1.2687;
    const double expectedGain = A_term * N * H_1kHz;

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(measured=%.4f, expected=%.4f, 5%% tol)", measuredGain, expectedGain);
    check(std::abs(measuredGain - expectedGain) / expectedGain < 0.05,
          "End-to-end gain at 1kHz within 5%", detail);

    std::snprintf(detail, sizeof(detail),
                  "(measured=%.2f dB, expected=%.2f dB)",
                  20.0 * std::log10(measuredGain), 20.0 * std::log10(expectedGain));
    check(measuredGain > 8.0 && measuredGain < 18.0,
          "Gain in sane range [8, 18]", detail);
}

// ===== TEST 2: PAD attenuation =====
static void testPadAttenuation()
{
    std::printf("\n--- Test 2: PAD Attenuation (~20 dB) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    // --- No PAD ---
    MicPreSetup setupNoPad;
    setupNoPad.prepare(sampleRate, blockSize);
    setupNoPad.micPre.setMicGain(1.0f);
    setupNoPad.micPre.setPadEnabled(false);
    setupNoPad.micPre.setPhaseReverse(false);
    setupNoPad.micPre.setSourceImpedance(150.0f);
    warmup(setupNoPad, sampleRate, 200);

    const int testLen = static_cast<int>(sampleRate * 0.1f);
    std::vector<float> sigNoPad(testLen);
    generateSine(sigNoPad.data(), testLen, 1000.0, sampleRate, 0.01);
    std::vector<float> inputCopy = sigNoPad;
    setupNoPad.micPre.processBlock(sigNoPad.data(), testLen);

    const int measureLen = 2048;
    const int offset = testLen - measureLen;
    double rmsNoPad = computeRMS(sigNoPad.data() + offset, measureLen);
    double rmsIn    = computeRMS(inputCopy.data() + offset, measureLen);
    double gainNoPad = rmsNoPad / rmsIn;

    // --- With PAD ---
    MicPreSetup setupPad;
    setupPad.prepare(sampleRate, blockSize);
    setupPad.micPre.setMicGain(1.0f);
    setupPad.micPre.setPadEnabled(true);
    setupPad.micPre.setPhaseReverse(false);
    setupPad.micPre.setSourceImpedance(150.0f);
    warmup(setupPad, sampleRate, 200);

    std::vector<float> sigPad(testLen);
    generateSine(sigPad.data(), testLen, 1000.0, sampleRate, 0.01);
    setupPad.micPre.processBlock(sigPad.data(), testLen);

    double rmsPad = computeRMS(sigPad.data() + offset, measureLen);
    double gainPad = rmsPad / rmsIn;

    double ratio = gainPad / gainNoPad;  // should be ~ 0.10526

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(ratio=%.6f, expected=%.6f, gainNoPad=%.4f, gainPad=%.4f)",
                  ratio, 0.10526, gainNoPad, gainPad);
    check(std::abs(ratio - 0.10526) / 0.10526 < 0.10,
          "PAD ratio ~ 0.10526 within 10%", detail);
}

// ===== TEST 3: Phase reverse =====
static void testPhaseReverse()
{
    std::printf("\n--- Test 3: Phase Reverse ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    // --- No phase reverse ---
    MicPreSetup setupNormal;
    setupNormal.prepare(sampleRate, blockSize);
    setupNormal.micPre.setMicGain(1.0f);
    setupNormal.micPre.setPadEnabled(false);
    setupNormal.micPre.setPhaseReverse(false);
    setupNormal.micPre.setSourceImpedance(150.0f);
    warmup(setupNormal, sampleRate, 300);

    // Use 20 Hz sine for slow wave (easy to see polarity)
    const int testLen = static_cast<int>(sampleRate * 0.15f); // 150ms = 3 cycles at 20Hz
    std::vector<float> sigNormal(testLen);
    generateSine(sigNormal.data(), testLen, 20.0, sampleRate, 0.005);
    setupNormal.micPre.processBlock(sigNormal.data(), testLen);

    // --- Phase reverse ---
    MicPreSetup setupReversed;
    setupReversed.prepare(sampleRate, blockSize);
    setupReversed.micPre.setMicGain(1.0f);
    setupReversed.micPre.setPadEnabled(false);
    setupReversed.micPre.setPhaseReverse(true);
    setupReversed.micPre.setSourceImpedance(150.0f);
    warmup(setupReversed, sampleRate, 300);

    std::vector<float> sigReversed(testLen);
    generateSine(sigReversed.data(), testLen, 20.0, sampleRate, 0.005);
    setupReversed.micPre.processBlock(sigReversed.data(), testLen);

    // Compute correlation of last half of signal (after settling)
    const int corrLen = testLen / 2;
    const int corrOffset = testLen - corrLen;
    double dotProduct = 0.0;
    double normA = 0.0, normB = 0.0;
    for (int i = 0; i < corrLen; ++i) {
        double a = static_cast<double>(sigNormal[corrOffset + i]);
        double b = static_cast<double>(sigReversed[corrOffset + i]);
        dotProduct += a * b;
        normA += a * a;
        normB += b * b;
    }
    double correlation = dotProduct / (std::sqrt(normA) * std::sqrt(normB) + 1e-30);

    char detail[200];
    std::snprintf(detail, sizeof(detail), "(correlation=%.6f, should be < -0.8)", correlation);
    check(correlation < -0.8, "Phase reversed output negatively correlated", detail);
}

// ===== TEST 4: Gain range (min vs max mic gain) =====
static void testGainRange()
{
    std::printf("\n--- Test 4: Gain Range (max vs min mic gain) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    auto measureGainAtFreq = [&](float micGain, double freq) -> double {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(micGain);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 200);

        const int testLen = static_cast<int>(sampleRate * 0.1f);
        std::vector<float> signal(testLen);
        generateSine(signal.data(), testLen, freq, sampleRate, 0.01);
        std::vector<float> inputCopy = signal;

        setup.micPre.processBlock(signal.data(), testLen);

        const int measureLen = 2048;
        const int offset = testLen - measureLen;
        double rmsOut = computeRMS(signal.data() + offset, measureLen);
        double rmsIn  = computeRMS(inputCopy.data() + offset, measureLen);
        return rmsOut / rmsIn;
    };

    double gainMax = measureGainAtFreq(1.0f, 500.0);  // alpha=0 -> max gain
    double gainMin = measureGainAtFreq(0.0f, 500.0);  // alpha=1 -> min gain

    // Max gain (alpha=0): ~A_term * 10 * 1.388 ~ 13.72
    // Min gain (alpha=1): ~A_term * 10 * 1.004 ~ 9.93
    const double A_term = 6800.0 / 6875.0;

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(gainMax=%.4f, gainMin=%.4f)", gainMax, gainMin);

    check(gainMax > gainMin, "Max gain > min gain", detail);

    // Check max gain in reasonable range (allow wide tolerance for transformer effects)
    check(gainMax > 8.0 && gainMax < 20.0,
          "Max gain in [8, 20]",
          (std::snprintf(detail, sizeof(detail), "(gainMax=%.4f)", gainMax), detail));

    // Check min gain in reasonable range
    check(gainMin > 5.0 && gainMin < 14.0,
          "Min gain in [5, 14]",
          (std::snprintf(detail, sizeof(detail), "(gainMin=%.4f)", gainMin), detail));

    // Ratio should be > 1.2 (max is notably higher)
    double ratio = gainMax / gainMin;
    check(ratio > 1.2,
          "Gain ratio max/min > 1.2",
          (std::snprintf(detail, sizeof(detail), "(ratio=%.4f)", ratio), detail));
}

// ===== TEST 5: Transformer saturation (THD increases with level) =====
static void testTransformerSaturation()
{
    std::printf("\n--- Test 5: Transformer Saturation (THD vs Level) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const double freq = 1000.0;

    // Measure THD at a given amplitude
    auto measureTHD = [&](double amplitude) -> double {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(1.0f);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 200);

        // 200ms of tone to reach steady state
        const int testLen = static_cast<int>(sampleRate * 0.2f); // 9600 samples
        std::vector<float> signal(testLen);
        generateSine(signal.data(), testLen, freq, sampleRate, amplitude);
        setup.micPre.processBlock(signal.data(), testLen);

        // Analyze last N samples (integer number of cycles for clean DFT)
        // At 1kHz, 48kHz SR: 48 samples per cycle. Use 2048 samples ~ 42.67 cycles
        // Better: use exactly 48*42 = 2016 samples for integer cycles
        const int N = 2016;  // 42 complete cycles of 1kHz at 48kHz
        const int offset = testLen - N;

        // DFT at fundamental frequency (Fourier coefficients)
        double fundCos = 0.0, fundSin = 0.0;
        for (int i = 0; i < N; ++i) {
            double phase = 2.0 * PI_D * freq * i / sampleRate;
            fundCos += static_cast<double>(signal[offset + i]) * std::cos(phase);
            fundSin += static_cast<double>(signal[offset + i]) * std::sin(phase);
        }
        fundCos *= 2.0 / N;
        fundSin *= 2.0 / N;

        // Reconstruct fundamental: a*cos(wt) + b*sin(wt)
        double totalPower = 0.0;
        double distortionPower = 0.0;
        for (int i = 0; i < N; ++i) {
            double sample = static_cast<double>(signal[offset + i]);
            double phase = 2.0 * PI_D * freq * i / sampleRate;
            double fundSample = fundCos * std::cos(phase) + fundSin * std::sin(phase);
            double diff = sample - fundSample;
            totalPower += sample * sample;
            distortionPower += diff * diff;
        }

        double fundPower = totalPower - distortionPower;
        if (fundPower < 1e-30) return 0.0;
        return std::sqrt(distortionPower / fundPower);
    };

    // Mu-metal CPWL in Artistic mode needs substantial drive to show THD.
    // Signal at transformer primary = amplitude × A_term ≈ amplitude × 0.989.
    double thdLow  = measureTHD(0.01);   // low level: ~0.01 at primary
    double thdHigh = measureTHD(1.0);    // high level: ~1.0 at primary (heavy drive)

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(THD_low=%.6f%%, THD_high=%.6f%%)",
                  thdLow * 100.0, thdHigh * 100.0);

    check(thdHigh > thdLow,
          "THD at high level > THD at low level", detail);

    // Both should be finite and non-negative
    check(thdLow >= 0.0 && std::isfinite(thdLow),
          "THD low is finite and non-negative");
    check(thdHigh >= 0.0 && std::isfinite(thdHigh),
          "THD high is finite and non-negative");
}

// ===== TEST 6: Frequency response sweep =====
static void testFrequencyResponseSweep()
{
    std::printf("\n--- Test 6: Frequency Response Sweep ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const double amplitude = 0.005;

    auto measureGainAtFreq = [&](double freq) -> double {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(1.0f);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 300);

        // Duration: enough for at least several cycles at lowest freq (20 Hz)
        // 300ms = 6 cycles at 20Hz
        const int testLen = static_cast<int>(sampleRate * 0.3f); // 14400 samples
        std::vector<float> signal(testLen);
        generateSine(signal.data(), testLen, freq, sampleRate, amplitude);
        std::vector<float> inputCopy = signal;

        setup.micPre.processBlock(signal.data(), testLen);

        // Measure RMS of last 2048 samples
        const int measureLen = 2048;
        const int offset = testLen - measureLen;
        double rmsOut = computeRMS(signal.data() + offset, measureLen);
        double rmsIn  = computeRMS(inputCopy.data() + offset, measureLen);
        return rmsOut / rmsIn;
    };

    double gain20Hz  = measureGainAtFreq(20.0);
    double gain100Hz = measureGainAtFreq(100.0);
    double gain1kHz  = measureGainAtFreq(1000.0);
    double gain10kHz = measureGainAtFreq(10000.0);
    double gain20kHz = measureGainAtFreq(20000.0);

    std::printf("    Gains: 20Hz=%.4f, 100Hz=%.4f, 1kHz=%.4f, 10kHz=%.4f, 20kHz=%.4f\n",
                gain20Hz, gain100Hz, gain1kHz, gain10kHz, gain20kHz);

    char detail[200];

    // Gain at 1kHz > gain at 20kHz (HF rolloff from gain stage)
    std::snprintf(detail, sizeof(detail),
                  "(gain1k=%.4f > gain20k=%.4f)", gain1kHz, gain20kHz);
    check(gain1kHz > gain20kHz, "1kHz gain > 20kHz gain (HF rolloff)", detail);

    // Gain at 100Hz > gain at 10kHz
    std::snprintf(detail, sizeof(detail),
                  "(gain100=%.4f > gain10k=%.4f)", gain100Hz, gain10kHz);
    check(gain100Hz > gain10kHz, "100Hz gain > 10kHz gain", detail);

    // All gains positive (no sign errors)
    check(gain20Hz > 0.0, "Gain at 20Hz positive",
          (std::snprintf(detail, sizeof(detail), "(%.4f)", gain20Hz), detail));
    check(gain100Hz > 0.0, "Gain at 100Hz positive",
          (std::snprintf(detail, sizeof(detail), "(%.4f)", gain100Hz), detail));
    check(gain1kHz > 0.0, "Gain at 1kHz positive",
          (std::snprintf(detail, sizeof(detail), "(%.4f)", gain1kHz), detail));
    check(gain10kHz > 0.0, "Gain at 10kHz positive",
          (std::snprintf(detail, sizeof(detail), "(%.4f)", gain10kHz), detail));
    check(gain20kHz > 0.0, "Gain at 20kHz positive",
          (std::snprintf(detail, sizeof(detail), "(%.4f)", gain20kHz), detail));

    // No catastrophic LF rolloff: gain at 20Hz > 0.5 * gain at 1kHz
    std::snprintf(detail, sizeof(detail),
                  "(gain20Hz=%.4f > 0.5*gain1kHz=%.4f)", gain20Hz, 0.5 * gain1kHz);
    check(gain20Hz > 0.5 * gain1kHz,
          "20Hz gain > 50% of 1kHz gain (no catastrophic LF rolloff)", detail);
}

// =========================================================================
int main()
{
    std::printf("========================================\n");
    std::printf("Harrison Mic Pre — Integration Tests\n");
    std::printf("  (real TransformerModel<CPWLLeaf>, Jensen JT-115K-E)\n");
    std::printf("========================================\n");

    testEndToEndGain1kHz();
    testPadAttenuation();
    testPhaseReverse();
    testGainRange();
    testTransformerSaturation();
    testFrequencyResponseSweep();

    std::printf("\n========================================\n");
    std::printf("Results: %d / %d passed\n", passCount, testCount);
    std::printf("========================================\n");

    return (passCount == testCount) ? 0 : 1;
}
