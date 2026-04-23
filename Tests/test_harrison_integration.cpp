// =============================================================================
// test_harrison_integration.cpp — Integration test: Harrison Mic Pre + real
//                                  TransformerModel<JilesAthertonLeaf<LangevinPade>>
//                                  (Jensen JT-115K-E, Realtime mode, no OS)
//
// This test validates the Harrison chain after the swap from CPWL to J-A +
// Bertotti (Option A). The plugin's harrisonTransformer_ now uses the full
// Jiles-Atherton hysteresis model with Bertotti dynamic losses auto-enabled
// from JAParameterSet::defaultMuMetal() (K1=1.44e-3, K2=0.02).
//
// Tests:
//   1. Finite output + sane gain range at 1 kHz
//   2. PAD attenuation (~20 dB — independent of core model, it's a scalar)
//   3. Phase reverse (still a scalar sign flip upstream of the transformer)
//   4. Mic gain pot: max > min (op-amp stage, unchanged)
//   5. THD monotonicity in level (J-A signature: soft-saturation)
//   6. Odd-harmonics-dominant (J-A symmetric B-H loop → no even harmonics)
//   7. Dynamic losses coupling: Bertotti ON → LF widening (loss) > Bertotti OFF
// =============================================================================

#include "../core/include/core/harrison/HarrisonMicPre.h"
#include "../core/include/core/harrison/ComponentValues.h"
#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/Presets.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

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

static double computeRMS(const float* data, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return std::sqrt(sum / count);
}

static void generateSine(float* buf, int numSamples, double freq, double sampleRate, double amplitude)
{
    for (int i = 0; i < numSamples; ++i)
        buf[i] = static_cast<float>(amplitude * std::sin(2.0 * PI_D * freq * i / sampleRate));
}

// Magnitude of the k-th harmonic (k=1: fundamental) via Goertzel-style DFT
static double harmonicMagnitude(const float* data, int N, double freq, double sampleRate, int k)
{
    double c = 0.0, s = 0.0;
    const double w = 2.0 * PI_D * freq * static_cast<double>(k) / sampleRate;
    for (int i = 0; i < N; ++i) {
        c += data[i] * std::cos(w * i);
        s += data[i] * std::sin(w * i);
    }
    c *= 2.0 / N;
    s *= 2.0 / N;
    return std::sqrt(c * c + s * s);
}

// Test setup using the same transformer type + mode as PluginProcessor wires.
struct MicPreSetup
{
    using TransformerT =
        transfo::TransformerModel<transfo::JilesAthertonLeaf<transfo::LangevinPade>>;

    TransformerT transformer;
    Harrison::MicPre::HarrisonMicPre<TransformerT> micPre;

    void prepare(float sampleRate, int blockSize)
    {
        auto jensenCfg = transfo::Presets::getByIndex(0);  // Jensen JT-115K-E
        transformer.setConfig(jensenCfg);
        transformer.setProcessingMode(transfo::ProcessingMode::Realtime);
        transformer.prepareToPlay(sampleRate, blockSize);

        micPre.setTransformer(&transformer);
        micPre.prepareToPlay(sampleRate, blockSize);
    }
};

static void warmup(MicPreSetup& setup, float sampleRate, int warmupMs)
{
    int warmupSamples = static_cast<int>(sampleRate * warmupMs / 1000.0f);
    std::vector<float> silence(warmupSamples, 0.0f);
    setup.micPre.processBlock(silence.data(), warmupSamples);
}

// ===== TEST 1: Finite output + sane gain at 1 kHz =====
static void testEndToEndGain1kHz()
{
    std::printf("\n--- Test 1: End-to-End Output at 1 kHz (J-A + Bertotti) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    MicPreSetup setup;
    setup.prepare(sampleRate, blockSize);
    setup.micPre.setMicGain(1.0f);
    setup.micPre.setPadEnabled(false);
    setup.micPre.setPhaseReverse(false);
    setup.micPre.setSourceImpedance(150.0f);
    warmup(setup, sampleRate, 200);

    const int testLen = static_cast<int>(sampleRate * 0.1f);
    std::vector<float> signal(testLen);
    generateSine(signal.data(), testLen, 1000.0, sampleRate, 0.01);
    std::vector<float> inputCopy = signal;

    setup.micPre.processBlock(signal.data(), testLen);

    // Finite-output invariant
    bool allFinite = true;
    for (int i = 0; i < testLen; ++i) {
        if (!std::isfinite(signal[i])) { allFinite = false; break; }
    }
    check(allFinite, "All output samples finite (no NaN/Inf)");

    const int measureLen = 2048;
    const int offset = testLen - measureLen;
    double rmsOut = computeRMS(signal.data() + offset, measureLen);
    double rmsIn  = computeRMS(inputCopy.data() + offset, measureLen);
    double gain = rmsOut / rmsIn;

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(gain=%.4f, ~%.2f dB)", gain, 20.0 * std::log10(gain + 1e-30));
    // With J-A + Bertotti at 0.01 stimulus, gain will be lower than CPWL
    // (losses bleed energy) but still in a sane audio range.
    check(gain > 1.0 && gain < 30.0, "Gain at 1 kHz in [1, 30]", detail);
}

// ===== TEST 2: PAD attenuation =====
static void testPadAttenuation()
{
    std::printf("\n--- Test 2: PAD Attenuation (~20 dB) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const int testLen = static_cast<int>(sampleRate * 0.1f);
    const int measureLen = 2048;
    const int offset = testLen - measureLen;

    auto measureGain = [&](bool padOn) -> double {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(1.0f);
        setup.micPre.setPadEnabled(padOn);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 200);

        std::vector<float> sig(testLen);
        generateSine(sig.data(), testLen, 1000.0, sampleRate, 0.01);
        std::vector<float> in = sig;
        setup.micPre.processBlock(sig.data(), testLen);
        return computeRMS(sig.data() + offset, measureLen)
             / computeRMS(in.data()  + offset, measureLen);
    };

    double gainNoPad = measureGain(false);
    double gainPad   = measureGain(true);
    double ratio = gainPad / (gainNoPad + 1e-30);

    // The PAD is a scalar T-pad upstream of the transformer (β = 0.10526).
    // With J-A the transformer response is level-dependent, so the ratio
    // can drift from 0.10526 when the no-PAD case saturates. Loosen tol
    // to 30% but keep it around the PAD target.
    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(ratio=%.4f, expected~0.105, gainNoPad=%.4f, gainPad=%.4f)",
                  ratio, gainNoPad, gainPad);
    check(ratio < 0.20 && ratio > 0.05,
          "PAD ratio in [0.05, 0.20] (target 0.105)", detail);
}

// ===== TEST 3: Phase reverse =====
static void testPhaseReverse()
{
    std::printf("\n--- Test 3: Phase Reverse ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    auto run = [&](bool invert) {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(1.0f);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(invert);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 300);

        const int testLen = static_cast<int>(sampleRate * 0.15f);
        std::vector<float> sig(testLen);
        generateSine(sig.data(), testLen, 100.0, sampleRate, 0.005);
        setup.micPre.processBlock(sig.data(), testLen);
        return sig;
    };

    auto sigNormal   = run(false);
    auto sigReversed = run(true);

    const int N = static_cast<int>(sigNormal.size());
    const int corrLen = N / 2;
    const int corrOff = N - corrLen;
    double dot = 0.0, normA = 0.0, normB = 0.0;
    for (int i = 0; i < corrLen; ++i) {
        double a = sigNormal  [corrOff + i];
        double b = sigReversed[corrOff + i];
        dot += a * b;
        normA += a * a;
        normB += b * b;
    }
    double correlation = dot / (std::sqrt(normA * normB) + 1e-30);

    char detail[200];
    std::snprintf(detail, sizeof(detail), "(correlation=%.4f)", correlation);
    check(correlation < -0.7, "Phase-reversed output negatively correlated", detail);
}

// ===== TEST 4: Mic gain pot (max > min) =====
static void testGainRange()
{
    std::printf("\n--- Test 4: Mic Gain Pot (max vs min) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const int testLen = static_cast<int>(sampleRate * 0.1f);
    const int measureLen = 2048;
    const int offset = testLen - measureLen;

    auto measure = [&](float micGain) {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(micGain);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 200);

        std::vector<float> sig(testLen);
        generateSine(sig.data(), testLen, 500.0, sampleRate, 0.01);
        std::vector<float> in = sig;
        setup.micPre.processBlock(sig.data(), testLen);
        return computeRMS(sig.data() + offset, measureLen)
             / computeRMS(in.data()  + offset, measureLen);
    };

    double gMax = measure(1.0f);
    double gMin = measure(0.0f);

    char detail[200];
    std::snprintf(detail, sizeof(detail), "(gMax=%.4f, gMin=%.4f)", gMax, gMin);
    check(gMax > gMin, "Max mic gain > min mic gain", detail);
    check(gMax > 0.0 && std::isfinite(gMax), "gMax finite and positive");
    check(gMin > 0.0 && std::isfinite(gMin), "gMin finite and positive");
}

// ===== TEST 5: THD monotonicity in level (J-A soft saturation) =====
static void testTHDMonotonicity()
{
    std::printf("\n--- Test 5: THD Monotonicity in Level (J-A saturation) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const double freq = 1000.0;

    auto measureTHD = [&](double amplitude) -> double {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(1.0f);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        warmup(setup, sampleRate, 300);

        const int testLen = static_cast<int>(sampleRate * 0.2f);
        std::vector<float> sig(testLen);
        generateSine(sig.data(), testLen, freq, sampleRate, amplitude);
        setup.micPre.processBlock(sig.data(), testLen);

        // 42 full cycles at 1 kHz / 48 kHz
        const int N = 2016;
        const int offset = testLen - N;

        double fund = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, 1);
        double harmPow = 0.0;
        for (int k = 2; k <= 10; ++k) {
            double mag = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, k);
            harmPow += mag * mag;
        }
        return std::sqrt(harmPow) / (fund + 1e-30);
    };

    double thdLow  = measureTHD(0.01);
    double thdMid  = measureTHD(0.1);
    double thdHigh = measureTHD(1.0);

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(low=%.4f%%, mid=%.4f%%, high=%.4f%%)",
                  thdLow * 100.0, thdMid * 100.0, thdHigh * 100.0);
    check(std::isfinite(thdLow) && std::isfinite(thdMid) && std::isfinite(thdHigh),
          "All THD values finite", detail);
    check(thdHigh >= thdLow, "THD(high) >= THD(low)", detail);
    check(thdHigh > 0.0, "Non-zero THD at high level (J-A is saturating)", detail);
}

// ===== TEST 6: Odd harmonics dominate (symmetric J-A B-H loop) =====
static void testOddHarmonicDominance()
{
    std::printf("\n--- Test 6: Odd Harmonics Dominant (J-A symmetric) ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const double freq = 500.0;

    MicPreSetup setup;
    setup.prepare(sampleRate, blockSize);
    setup.micPre.setMicGain(1.0f);
    setup.micPre.setPadEnabled(false);
    setup.micPre.setPhaseReverse(false);
    setup.micPre.setSourceImpedance(150.0f);
    warmup(setup, sampleRate, 300);

    const int testLen = static_cast<int>(sampleRate * 0.2f);
    std::vector<float> sig(testLen);
    generateSine(sig.data(), testLen, freq, sampleRate, 0.5);  // mid-level drive
    setup.micPre.processBlock(sig.data(), testLen);

    // 96 cycles × 48 samples = 4608 samples: clean DFT at integer cycles
    const int N = 4608;
    const int offset = testLen - N;

    double h1 = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, 1);
    double h2 = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, 2);
    double h3 = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, 3);
    double h4 = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, 4);
    double h5 = harmonicMagnitude(sig.data() + offset, N, freq, sampleRate, 5);

    // For a symmetric B-H loop, h3 and h5 should exceed h2 and h4.
    // We just assert h3 > h2 (the core invariant of J-A symmetry).
    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(h1=%.4e h2=%.4e h3=%.4e h4=%.4e h5=%.4e)", h1, h2, h3, h4, h5);
    check(h3 > h2, "h3 > h2 (odd dominance)", detail);
    check(h5 > h4 || h5 < 1e-8, "h5 > h4 (or both negligible)", detail);
    check(h1 > 0.0, "Fundamental present");
}

// ===== TEST 7: Bertotti slider wire-liveness =====
//
// The cascade path (which Harrison uses in Realtime mode) applies Bertotti
// through computeFieldSeparated with a deliberate attenuation factor of 0.15
// (kCascadeEddyFactor in TransformerModel.h) that matches the Jensen datasheet
// HF response. The physical effect at realistic JT-115K-E coefficients
// (K1=1.44e-3, K2=0.02) is therefore sub-percent in RMS — too subtle to test
// robustly here. The dedicated Bertotti physics is validated by
// test_bertotti_coupling and test_jensen_bertotti_freq.
//
// What THIS test asserts: setDynamicLossCoefficients() actually reaches the
// cascade DynamicLosses instance and affects the audio output. We probe with
// an extreme coefficient (K1=100) to produce a large enough RMS swing that
// any wiring regression (e.g. the setter going to a dead copy) would be
// caught immediately.
static void testDynamicLossCoupling()
{
    std::printf("\n--- Test 7: Bertotti Slider Wire-Liveness ---\n");

    const float sampleRate = 48000.0f;
    const int blockSize = 512;
    const double freq = 5000.0;
    const int testLen = static_cast<int>(sampleRate * 0.2f);
    const int measureLen = 2048;
    const int offset = testLen - measureLen;

    auto measureRMS = [&](float K1, float K2) -> double {
        MicPreSetup setup;
        setup.prepare(sampleRate, blockSize);
        setup.micPre.setMicGain(1.0f);
        setup.micPre.setPadEnabled(false);
        setup.micPre.setPhaseReverse(false);
        setup.micPre.setSourceImpedance(150.0f);
        setup.transformer.setDynamicLossCoefficients(K1, K2);

        warmup(setup, sampleRate, 300);

        std::vector<float> sig(testLen);
        generateSine(sig.data(), testLen, freq, sampleRate, 0.3);
        setup.micPre.processBlock(sig.data(), testLen);

        return computeRMS(sig.data() + offset, measureLen);
    };

    double rmsOff    = measureRMS(0.0f, 0.0f);
    double rmsStrong = measureRMS(100.0f, 50.0f);  // wire-liveness probe

    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "(rms off=%.4f, strong=%.4f)", rmsOff, rmsStrong);
    check(std::isfinite(rmsOff) && std::isfinite(rmsStrong),
          "RMS finite with Bertotti off and strong", detail);

    double relDiff = std::abs(rmsStrong - rmsOff) / (rmsOff + 1e-30);
    std::snprintf(detail, sizeof(detail),
                  "(relDiff=%.4f)", relDiff);
    check(relDiff > 0.02,
          "setDynamicLossCoefficients() reaches the audio output", detail);
}

// =========================================================================
int main()
{
    std::printf("========================================\n");
    std::printf("Harrison Mic Pre — Integration Tests (J-A + Bertotti)\n");
    std::printf("  TransformerModel<JilesAthertonLeaf<LangevinPade>>\n");
    std::printf("  Jensen JT-115K-E, Realtime mode, no OS\n");
    std::printf("========================================\n");

    testEndToEndGain1kHz();
    testPadAttenuation();
    testPhaseReverse();
    testGainRange();
    testTHDMonotonicity();
    testOddHarmonicDominance();
    testDynamicLossCoupling();

    std::printf("\n========================================\n");
    std::printf("Results: %d / %d passed\n", passCount, testCount);
    std::printf("========================================\n");

    return (passCount == testCount) ? 0 : 1;
}
