// =============================================================================
// Test: ODT Differentiation — Heritage (Neve Class-A) vs Modern (JE-990)
//
// Validates harmonic signature, THD, level matching, and click-free switching
// between the two amplifier topologies in the dual-topology preamp model.
//
// Known limitations documented by these tests:
//   - JE-990 H2 > H3: VAS (single-ended PNP) dominates distortion signature
//     → H2 dominant until ClassAB crossover model is enhanced (Sprint B)
//   - Heritage–Modern level delta: within spec after T2 default fix (Sprint A.2)
//
// Test groups:
//   1. Heritage H2 dominance (Class-A even-harmonic signature)    [PASS]
//   2. Modern H3 dominance (push-pull odd-harmonic signature)     [FAIL — known limitation, see KNOWN_LIMITATIONS.md]
//   3. Heritage vs Modern A/B comparison                          [partial FAIL — H2/H3 ratio, Sprint B target]
//   4. THD vs gain sweep (monotonicity)                           [PASS]
//   5. Full-chain level match                                     [PASS — fixed by VAS topology correction]
//   6. Mode switch click-free (no NaN, no click)                  [PASS]
//
// Reference: SPRINT_PLAN_PREAMP.md, ANALYSE_ET_DESIGN_Rev2.md Annexe B
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

using namespace transfo;

// =============================================================================
// Constants
// =============================================================================

static constexpr float  kSampleRate   = 44100.0f;
static constexpr int    kMaxBlock     = 512;
static constexpr int    kWarmup       = 4096;
static constexpr int    kMeasure      = 8192;
static constexpr double kFreq         = 1000.0;
static constexpr float  kAmplitude    = 0.01f;   // ~-40 dBFS, ~-10 dBu mic level
static constexpr int    kGainPos      = 5;        // +26 dB amplifier gain

// =============================================================================
// Static helpers — signal generation, warmup, measurement
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

/// Create and configure a PreampModel, returning it heap-allocated.
static std::unique_ptr<PreampModel<CPWLLeaf>> createModel(int path, int gainPos)
{
    auto model = std::make_unique<PreampModel<CPWLLeaf>>();
    auto cfg = PreampConfig::DualTopology();
    model->setConfig(cfg);
    model->prepareToPlay(kSampleRate, kMaxBlock);
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
                    double freq, float amplitude)
{
    std::vector<float> warmIn(static_cast<size_t>(numSamples));
    std::vector<float> warmOut(static_cast<size_t>(numSamples));
    generateSine(warmIn.data(), numSamples, freq, amplitude, kSampleRate);
    processInChunks(model, warmIn.data(), warmOut.data(), numSamples);
}

/// Convert linear magnitude to dB. Returns -999 for silence.
static double toDB(double mag)
{
    return (mag > 1e-30) ? 20.0 * std::log10(mag) : -999.0;
}

/// Check a buffer for NaN or Inf. Returns true if any found.
static bool hasNaNOrInf(const float* buf, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (std::isnan(buf[i]) || std::isinf(buf[i]))
            return true;
    }
    return false;
}

/// Measure harmonics H1-H3 and THD for a given path configuration.
/// Returns: {h1, h2, h3, thd%}
struct HarmonicResult
{
    double h1     = 0.0;
    double h2     = 0.0;
    double h3     = 0.0;
    double thdPct = 0.0;
};

static HarmonicResult measureHarmonics(int path, int gainPos,
                                        double freq, float amplitude)
{
    auto model = createModel(path, gainPos);

    // Warmup
    warmup(*model, kWarmup, freq, amplitude);

    // Measurement
    std::vector<float> input(static_cast<size_t>(kMeasure));
    std::vector<float> output(static_cast<size_t>(kMeasure));
    generateSine(input.data(), kMeasure, freq, amplitude, kSampleRate, kWarmup);
    processInChunks(*model, input.data(), output.data(), kMeasure);

    HarmonicResult r;
    r.h1 = test::goertzelMagnitude(output.data(), kMeasure, freq, kSampleRate);
    r.h2 = test::goertzelMagnitude(output.data(), kMeasure, freq * 2.0, kSampleRate);
    r.h3 = test::goertzelMagnitude(output.data(), kMeasure, freq * 3.0, kSampleRate);
    r.thdPct = test::computeTHD(output.data(), kMeasure, freq, kSampleRate, 8);

    return r;
}

/// Print harmonic analysis results.
static void printHarmonics(const char* label, const HarmonicResult& r)
{
    std::printf("    %s: H1=%.2f dB  H2=%.2f dB  H3=%.2f dB  "
                "H2/H3=%.2f dB  THD=%.4f%%\n",
                label,
                toDB(r.h1), toDB(r.h2), toDB(r.h3),
                toDB(r.h2) - toDB(r.h3),
                r.thdPct);
}

// =============================================================================
// TEST 1: Heritage H2 > H3 (Class-A even-harmonic signature)
// =============================================================================

static void test_heritage_h2_dominance()
{
    std::printf("\n--- Test 1: Heritage H2 > H3 (Class-A signature) ---\n");

    HarmonicResult r = measureHarmonics(0, kGainPos, kFreq, kAmplitude);
    printHarmonics("Heritage", r);

    CHECK(r.h1 > 1e-10, "Heritage H1 is non-zero (signal passes through)");
    CHECK(r.h2 > r.h3,
          "Heritage: H2 > H3 (Class-A even-harmonic signature)");
    CHECK(r.thdPct > 0.0,
          "Heritage produces measurable harmonic distortion");

    std::printf("    H2-H3 delta: %.2f dB  (positive = H2-dominant)\n",
                toDB(r.h2) - toDB(r.h3));
}

// =============================================================================
// TEST 2: Modern H3 > H2 (push-pull odd-harmonic signature)
//         Expected to FAIL — documents known JE-990 H2>H3 bug
// =============================================================================

static void test_modern_h3_dominance()
{
    std::printf("\n--- Test 2: Modern H3 > H2 (push-pull signature) ---\n");
    std::printf("    NOTE: Known limitation — VAS H2 dominance (see KNOWN_LIMITATIONS.md).\n");
    std::printf("          Expected to FAIL until ClassAB crossover model (Sprint B).\n");

    HarmonicResult r = measureHarmonics(1, kGainPos, kFreq, kAmplitude);
    printHarmonics("Modern  ", r);

    CHECK(r.h1 > 1e-10, "Modern H1 is non-zero (signal passes through)");
    CHECK(r.h3 > r.h2,
          "Modern: H3 > H2 (push-pull odd-harmonic signature)");
    CHECK(r.thdPct > 0.0,
          "Modern produces measurable harmonic distortion");

    std::printf("    H3-H2 delta: %.2f dB  (positive = H3-dominant, expected)\n",
                toDB(r.h3) - toDB(r.h2));
}

// =============================================================================
// TEST 3: Heritage vs Modern A/B comparison
//         One CHECK expected to FAIL (Modern H2/H3 ratio bug)
// =============================================================================

static void test_heritage_vs_modern_ab()
{
    std::printf("\n--- Test 3: Heritage vs Modern A/B Comparison ---\n");

    HarmonicResult rH = measureHarmonics(0, kGainPos, kFreq, kAmplitude);
    HarmonicResult rM = measureHarmonics(1, kGainPos, kFreq, kAmplitude);

    printHarmonics("Heritage", rH);
    printHarmonics("Modern  ", rM);

    // THD should differ between the two topologies
    double thdDiff = std::abs(rH.thdPct - rM.thdPct);
    std::printf("    THD difference: %.4f%%\n", thdDiff);

    CHECK(thdDiff > 0.1,
          "Paths produce different THD (|THD_heritage - THD_modern| > 0.1)");

    // Heritage should be H2-dominant
    double ratioH = (rH.h3 > 1e-30) ? rH.h2 / rH.h3 : 0.0;
    std::printf("    Heritage H2/H3 ratio: %.3f (expect > 1.0)\n", ratioH);
    CHECK(ratioH > 1.0,
          "Heritage H2/H3 ratio > 1.0 (even-harmonic dominance)");

    // Modern should be H3-dominant (will FAIL due to bug)
    double ratioM = (rM.h3 > 1e-30) ? rM.h2 / rM.h3 : 0.0;
    std::printf("    Modern  H2/H3 ratio: %.3f (expect < 1.0)\n", ratioM);
    std::printf("    NOTE: Known limitation — VAS H2 dominance masks ClassAB H3.\n");
    std::printf("          Expected to FAIL until ClassAB crossover model (Sprint B).\n");
    CHECK(ratioM < 1.0,
          "Modern H2/H3 ratio < 1.0 (odd-harmonic dominance)");
}

// =============================================================================
// TEST 4: THD vs gain sweep (monotonicity for both paths)
// =============================================================================

static void test_thd_vs_gain_sweep()
{
    std::printf("\n--- Test 4: THD vs Gain Sweep ---\n");

    static const int kPositions[]   = { 0, 3, 5, 7, 10 };
    static const int kNumPositions  = 5;
    static const char* kPathNames[] = { "Heritage", "Modern " };

    double thdTable[2][5] = {};  // [path][posIdx]

    for (int path = 0; path < 2; ++path)
    {
        std::printf("    %s path:\n", kPathNames[path]);
        std::printf("    %-10s %-12s %-12s\n", "Position", "Gain (dB)", "THD (%)");
        std::printf("    %-10s %-12s %-12s\n", "--------", "---------", "-------");

        for (int p = 0; p < kNumPositions; ++p)
        {
            int pos = kPositions[p];
            HarmonicResult r = measureHarmonics(path, pos, kFreq, kAmplitude);
            thdTable[path][p] = r.thdPct;

            std::printf("    %-10d %-12.1f %-12.4f\n",
                        pos, static_cast<double>(GainTable::getGainDB(pos)),
                        r.thdPct);
        }
        std::printf("\n");
    }

    // Check monotonicity: THD at highest gain > THD at lowest gain
    CHECK(thdTable[0][kNumPositions - 1] > thdTable[0][0],
          "Heritage: THD at gain 10 > THD at gain 0 (monotone increase)");
    CHECK(thdTable[1][kNumPositions - 1] > thdTable[1][0],
          "Modern: THD at gain 10 > THD at gain 0 (monotone increase)");

    // Heritage should have higher THD than Modern at position 5
    // (Class-A saturates harder than feedback-linearized push-pull)
    // Note: This may fail due to the JE-990 gain bug. Soft assertion with a note.
    double heritageTHD5 = thdTable[0][2];  // position 5 is index 2
    double modernTHD5   = thdTable[1][2];
    std::printf("    Position 5 comparison: Heritage=%.4f%%  Modern=%.4f%%\n",
                heritageTHD5, modernTHD5);
    std::printf("    NOTE: Heritage > Modern THD check may fail if JE-990 level\n");
    std::printf("          bug causes different effective drive levels.\n");
    CHECK(heritageTHD5 > modernTHD5,
          "Heritage THD > Modern THD at position 5 (Class-A less linearized)");
}

// =============================================================================
// TEST 5: Full-chain level match (expected to FAIL — ~24 dB difference)
// =============================================================================

static void test_full_chain_level_match()
{
    std::printf("\n--- Test 5: Full-Chain Level Match ---\n");
    std::printf("    NOTE: This test documents a known bug (~24 dB level difference).\n");
    std::printf("          Expected to FAIL until JE-990 gain calibration fix.\n");

    // Heritage path
    auto modelH = createModel(0, kGainPos);
    warmup(*modelH, kWarmup, kFreq, kAmplitude);

    std::vector<float> input(static_cast<size_t>(kMeasure));
    std::vector<float> outH(static_cast<size_t>(kMeasure));
    generateSine(input.data(), kMeasure, kFreq, kAmplitude, kSampleRate, kWarmup);
    processInChunks(*modelH, input.data(), outH.data(), kMeasure);

    double rmsH = test::computeRMS(outH.data(), kMeasure);

    // Modern path
    auto modelM = createModel(1, kGainPos);
    warmup(*modelM, kWarmup, kFreq, kAmplitude);

    std::vector<float> outM(static_cast<size_t>(kMeasure));
    generateSine(input.data(), kMeasure, kFreq, kAmplitude, kSampleRate, kWarmup);
    processInChunks(*modelM, input.data(), outM.data(), kMeasure);

    double rmsM = test::computeRMS(outM.data(), kMeasure);

    // Compute level difference
    double levelDiff_dB = 0.0;
    if (rmsH > 1e-30 && rmsM > 1e-30)
        levelDiff_dB = 20.0 * std::log10(rmsH / rmsM);

    std::printf("    Heritage RMS: %.6e (%.2f dB)\n", rmsH, toDB(rmsH));
    std::printf("    Modern   RMS: %.6e (%.2f dB)\n", rmsM, toDB(rmsM));
    std::printf("    Level diff:   %.2f dB (Heritage - Modern)\n", levelDiff_dB);

    CHECK(rmsH > 1e-10, "Heritage output is non-zero");
    CHECK(rmsM > 1e-10, "Modern output is non-zero");
    CHECK(std::abs(levelDiff_dB) < 3.0,
          "Heritage/Modern level match within +/-3 dB");
}

// =============================================================================
// TEST 6: Mode switch during audio (click-free)
// =============================================================================

static void test_mode_switch_clickfree()
{
    std::printf("\n--- Test 6: Mode Switch Click-Free ---\n");

    auto model = createModel(0, kGainPos);  // Start with Heritage

    // Total signal: warmup + before + transition1 + mid + transition2 + after
    static constexpr int kSegment    = 2048;
    static constexpr int kTransition = 1024;
    static constexpr int kTotalSamples =
        kWarmup + kSegment + kTransition + kSegment + kTransition + kSegment;

    // Generate continuous sine for the entire duration
    std::vector<float> input(static_cast<size_t>(kTotalSamples));
    std::vector<float> output(static_cast<size_t>(kTotalSamples));
    generateSine(input.data(), kTotalSamples, kFreq, kAmplitude, kSampleRate);

    int pos = 0;

    // Phase 1: Warmup (Heritage)
    processInChunks(*model, input.data() + pos, output.data() + pos, kWarmup);
    pos += kWarmup;

    // Phase 2: Measure Heritage (before switch)
    int beforeStart = pos;
    processInChunks(*model, input.data() + pos, output.data() + pos, kSegment);
    pos += kSegment;
    double rmsBefore = test::computeRMS(output.data() + beforeStart, kSegment);

    // Phase 3: Switch to Modern, process transition
    model->setPath(1);
    int trans1Start = pos;
    processInChunks(*model, input.data() + pos, output.data() + pos, kTransition);
    pos += kTransition;

    // Phase 4: Process mid segment (Modern fully active)
    processInChunks(*model, input.data() + pos, output.data() + pos, kSegment);
    pos += kSegment;

    // Phase 5: Switch back to Heritage, process transition
    model->setPath(0);
    int trans2Start = pos;
    processInChunks(*model, input.data() + pos, output.data() + pos, kTransition);
    pos += kTransition;

    // Phase 6: Measure Heritage (after round-trip)
    int afterStart = pos;
    processInChunks(*model, input.data() + pos, output.data() + pos, kSegment);
    double rmsAfter = test::computeRMS(output.data() + afterStart, kSegment);

    // Check 1: No NaN anywhere
    bool anyNaN = hasNaNOrInf(output.data(), kTotalSamples);
    CHECK(!anyNaN, "No NaN/Inf in any sample during mode switching");

    // Check 2: No click during transitions (max sample-to-sample diff < 0.1)
    double maxDelta1 = 0.0;
    for (int i = trans1Start + 1; i < trans1Start + kTransition; ++i)
    {
        double delta = std::abs(
            static_cast<double>(output[static_cast<size_t>(i)]) -
            static_cast<double>(output[static_cast<size_t>(i - 1)]));
        if (delta > maxDelta1) maxDelta1 = delta;
    }

    double maxDelta2 = 0.0;
    for (int i = trans2Start + 1; i < trans2Start + kTransition; ++i)
    {
        double delta = std::abs(
            static_cast<double>(output[static_cast<size_t>(i)]) -
            static_cast<double>(output[static_cast<size_t>(i - 1)]));
        if (delta > maxDelta2) maxDelta2 = delta;
    }

    double maxDelta = std::max(maxDelta1, maxDelta2);
    std::printf("    Max sample-to-sample delta during switch: %.6f\n", maxDelta);
    CHECK(maxDelta < 0.1,
          "No click during switch (max sample-to-sample diff < 0.1)");

    // Check 3: RMS returns to within 20% after round-trip
    std::printf("    RMS before: %.6e\n", rmsBefore);
    std::printf("    RMS after:  %.6e\n", rmsAfter);
    double rmsTolerance = rmsBefore * 0.2;
    std::printf("    Tolerance (20%%): %.6e\n", rmsTolerance);
    CHECK_NEAR(rmsAfter, rmsBefore, rmsTolerance,
               "RMS returns to +/-20% after round-trip switch");
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::printf("================================================================\n");
    std::printf("  ODT Differentiation Test Suite\n");
    std::printf("  Heritage (Neve Class-A) vs Modern (JE-990)\n");
    std::printf("  Gain position %d  |  Amplifier gain: %.1f dB\n",
                kGainPos, static_cast<double>(GainTable::getGainDB(kGainPos)));
    std::printf("================================================================\n");

    test_heritage_h2_dominance();
    test_modern_h3_dominance();
    test_heritage_vs_modern_ab();
    test_thd_vs_gain_sweep();
    test_full_chain_level_match();
    test_mode_switch_clickfree();

    std::printf("\n");
    test::printSummary("test_odt_differentiation");
    return (test::g_fail() > 0) ? 1 : 0;
}
