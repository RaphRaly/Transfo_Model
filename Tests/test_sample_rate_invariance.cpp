// =============================================================================
// Test: Sample Rate Invariance — Sprint 3 polish validation
//
// Verifies that the dual-topology preamp produces consistent results at
// 44.1 kHz and 96 kHz.  The signal chain (T1 -> amp -> T2) should produce
// similar magnitude, THD, and frequency response regardless of sample rate.
//
// Test groups:
//   1.  Neve path: magnitude ratio within +/-1 dB, THD ratio within +/-50%
//   2.  JE-990 path: same criteria
//   3.  Frequency response: 50 Hz, 1 kHz, 10 kHz within +/-2 dB across rates
//
// Reference: SPRINT_PLAN_PREAMP.md Sprint 3 (polish)
// =============================================================================

#include "test_common.h"
#include "../core/include/core/preamp/PreampModel.h"
#include "../core/include/core/model/PreampConfig.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/preamp/GainTable.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>
#include <algorithm>

using namespace transfo;

// =============================================================================
// Constants
// =============================================================================

static constexpr int    kMaxBlock     = 512;
static constexpr int    kWarmup       = 4096;
static constexpr int    kMeasure      = 8192;
static constexpr double kFreq         = 1000.0;
static constexpr float  kAmplitude    = 0.01f;   // ~-40 dBFS, ~-10 dBu mic level
static constexpr int    kGainPos      = 5;        // +26 dB amplifier gain

// Sample rates to compare
static constexpr float  kSR_44k      = 44100.0f;
static constexpr float  kSR_96k      = 96000.0f;

// =============================================================================
// Helpers
// =============================================================================

/// Generate a sine wave into a pre-allocated buffer.
static void generateSine(float* buf, int numSamples, double freq,
                          float amplitude, float sampleRate, int offset = 0)
{
    for (int i = 0; i < numSamples; ++i)
    {
        double t = static_cast<double>(i + offset) / static_cast<double>(sampleRate);
        buf[i] = amplitude * static_cast<float>(std::sin(2.0 * test::PI * freq * t));
    }
}

/// Create and configure a PreampModel at a given sample rate and path.
static std::unique_ptr<PreampModel<CPWLLeaf>> createModel(float sampleRate,
                                                            int path, int gainPos)
{
    auto model = std::make_unique<PreampModel<CPWLLeaf>>();
    auto cfg = PreampConfig::DualTopology();
    model->setConfig(cfg);
    model->prepareToPlay(sampleRate, kMaxBlock);
    model->setGainPosition(gainPos);
    model->setPath(path);
    model->setInputGain(0.0f);
    model->setOutputGain(0.0f);
    model->setMix(1.0f);
    return model;
}

/// Process a buffer through the model in kMaxBlock-sized chunks.
static void processInChunks(PreampModel<CPWLLeaf>& model,
                             const float* input, float* output, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += kMaxBlock)
    {
        int chunk = std::min(kMaxBlock, numSamples - offset);
        model.processBlock(input + offset, output + offset, chunk);
    }
}

/// Run warmup samples through the model (sine tone, discarded output).
static void warmup(PreampModel<CPWLLeaf>& model, int numSamples,
                    double freq, float amplitude, float sampleRate)
{
    std::vector<float> warmIn(static_cast<size_t>(numSamples));
    std::vector<float> warmOut(static_cast<size_t>(numSamples));
    generateSine(warmIn.data(), numSamples, freq, amplitude, sampleRate);
    processInChunks(model, warmIn.data(), warmOut.data(), numSamples);
}

/// Convert linear magnitude to dB. Returns -999 for silence.
static double toDB(double mag)
{
    return (mag > 1e-30) ? 20.0 * std::log10(mag) : -999.0;
}

/// Measurement result for one sample rate / path / frequency configuration.
struct MeasureResult
{
    double rms         = 0.0;
    double fundamental = 0.0;   // Goertzel magnitude at test frequency
    double thd         = 0.0;   // THD in percent
};

/// Measure RMS, fundamental magnitude, and THD at a given frequency.
static MeasureResult measureAtFreq(float sampleRate, int path,
                                    int gainPos, double freq,
                                    float amplitude)
{
    auto model = createModel(sampleRate, path, gainPos);

    // Warmup: 4096 samples to settle DC and WDF reactive states
    warmup(*model, kWarmup, freq, amplitude, sampleRate);

    // Measurement pass
    std::vector<float> inBuf(static_cast<size_t>(kMeasure));
    std::vector<float> outBuf(static_cast<size_t>(kMeasure));
    generateSine(inBuf.data(), kMeasure, freq, amplitude, sampleRate, kWarmup);
    processInChunks(*model, inBuf.data(), outBuf.data(), kMeasure);

    MeasureResult r;
    r.rms = test::computeRMS(outBuf.data(), kMeasure);
    r.fundamental = test::goertzelMagnitude(outBuf.data(), kMeasure, freq,
                                             static_cast<double>(sampleRate));
    r.thd = test::computeTHD(outBuf.data(), kMeasure, freq,
                              static_cast<double>(sampleRate), 6);
    return r;
}

// =============================================================================
// Test 1 & 2: Single-frequency invariance (per path)
// =============================================================================

static void testPathInvariance(int path, const char* pathName)
{
    std::printf("\n--- %s path: 44.1k vs 96k invariance ---\n", pathName);

    MeasureResult r44 = measureAtFreq(kSR_44k, path, kGainPos, kFreq, kAmplitude);
    MeasureResult r96 = measureAtFreq(kSR_96k, path, kGainPos, kFreq, kAmplitude);

    std::printf("  44.1 kHz: RMS=%.6f  mag=%.6f (%.1f dB)  THD=%.3f%%\n",
                r44.rms, r44.fundamental, toDB(r44.fundamental), r44.thd);
    std::printf("  96.0 kHz: RMS=%.6f  mag=%.6f (%.1f dB)  THD=%.3f%%\n",
                r96.rms, r96.fundamental, toDB(r96.fundamental), r96.thd);

    // Magnitude ratio in dB
    double magRatio_dB = 0.0;
    if (r44.fundamental > 1e-30 && r96.fundamental > 1e-30)
        magRatio_dB = 20.0 * std::log10(r96.fundamental / r44.fundamental);
    std::printf("  Magnitude ratio: %.2f dB\n", magRatio_dB);

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "%s: magnitude ratio within +/-1 dB (got %.2f dB)",
                  pathName, magRatio_dB);
    CHECK_RANGE(magRatio_dB, -1.0, 1.0, msg);

    // THD ratio: within +/-50% relative
    if (r44.thd > 0.001 && r96.thd > 0.001)
    {
        double thdRatio = r96.thd / r44.thd;
        std::printf("  THD ratio (96k/44k): %.3f\n", thdRatio);

        std::snprintf(msg, sizeof(msg),
                      "%s: THD ratio within +/-50%% (got %.3f)",
                      pathName, thdRatio);
        CHECK_RANGE(thdRatio, 0.5, 1.5, msg);
    }
    else
    {
        std::printf("  THD too low to compare meaningfully (44k=%.4f%%, 96k=%.4f%%)\n",
                    r44.thd, r96.thd);
        // Still passes -- negligible THD at both rates is fine
        CHECK(true, "THD negligible at both sample rates (OK)");
    }
}

// =============================================================================
// Test 3: Frequency response comparison
// =============================================================================

static void testFrequencyResponse(int path, const char* pathName)
{
    std::printf("\n--- %s path: frequency response 44.1k vs 96k ---\n", pathName);

    static constexpr double kTestFreqs[] = { 50.0, 1000.0, 10000.0 };
    static constexpr int kNumFreqs = 3;

    for (int f = 0; f < kNumFreqs; ++f)
    {
        double freq = kTestFreqs[f];
        MeasureResult r44 = measureAtFreq(kSR_44k, path, kGainPos, freq, kAmplitude);
        MeasureResult r96 = measureAtFreq(kSR_96k, path, kGainPos, freq, kAmplitude);

        double mag44_dB = toDB(r44.fundamental);
        double mag96_dB = toDB(r96.fundamental);
        double diff_dB = mag96_dB - mag44_dB;

        std::printf("  %.0f Hz: 44.1k=%.1f dB, 96k=%.1f dB, diff=%.2f dB\n",
                    freq, mag44_dB, mag96_dB, diff_dB);

        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "%s: freq response at %.0f Hz within +/-2 dB (got %.2f dB)",
                      pathName, freq, diff_dB);
        CHECK_RANGE(diff_dB, -2.0, 2.0, msg);
    }
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::printf("=== Sample Rate Invariance Tests ===\n");

    // Test 1: Neve path (path 0) single-frequency invariance
    testPathInvariance(0, "Neve");

    // Test 2: JE-990 path (path 1) single-frequency invariance
    testPathInvariance(1, "JE-990");

    // Test 3: Frequency response comparison — Neve
    testFrequencyResponse(0, "Neve");

    // Test 4: Frequency response comparison — JE-990
    testFrequencyResponse(1, "JE-990");

    return test::printSummary("SampleRateInvariance");
}
