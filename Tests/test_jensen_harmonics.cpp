// =============================================================================
// Test: Jensen Harmonic Order Verification
//
// Validates harmonic structure for Jensen JT-115K-E (balanced transformer):
//   - Odd harmonics (H3, H5) should dominate over even (H2, H4) due to
//     centrosymmetric B-H curve of a balanced, center-tapped transformer.
//   - THD must be nonzero (model produces distortion) and bounded.
//
// Uses the full TransformerModel in Realtime mode with Goertzel-based
// harmonic extraction from test_common.h.
//
// Coverage item: Harmonic order verification (odd > even for balanced)
// =============================================================================

#include "test_common.h"
#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/TransformerConfig.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"

#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace transfo;

// ---- Helpers ----------------------------------------------------------------

// Measure individual harmonic magnitudes H1..H5 via Goertzel
struct HarmonicLevels {
    double h1 = 0.0, h2 = 0.0, h3 = 0.0, h4 = 0.0, h5 = 0.0;
    double thdPercent = 0.0;
};

static HarmonicLevels measureHarmonics(const float* signal, int numSamples,
                                        double fundFreq, double sampleRate)
{
    HarmonicLevels hl;
    hl.h1 = test::goertzelMagnitude(signal, numSamples, fundFreq * 1.0, sampleRate);
    hl.h2 = test::goertzelMagnitude(signal, numSamples, fundFreq * 2.0, sampleRate);
    hl.h3 = test::goertzelMagnitude(signal, numSamples, fundFreq * 3.0, sampleRate);
    hl.h4 = test::goertzelMagnitude(signal, numSamples, fundFreq * 4.0, sampleRate);
    hl.h5 = test::goertzelMagnitude(signal, numSamples, fundFreq * 5.0, sampleRate);
    hl.thdPercent = test::computeTHD(signal, numSamples, fundFreq, sampleRate, 8);
    return hl;
}

static double toDB(double linear)
{
    return 20.0 * std::log10(linear + 1e-30);
}

// ---- Test 1: JT-115K-E odd harmonic dominance (Realtime) --------------------

void test_jt115ke_odd_dominance_realtime()
{
    std::printf("\n=== Jensen JT-115K-E Harmonic Order — Realtime (CPWL) ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 8192;
    const float freq = 1000.0f;
    // +6 dBFS = amplitude ~2.0 -> enough to drive into saturation
    const float amplitude = 2.0f;

    // Heap-allocate to avoid stack overflow (TransformerModel is very large)
    auto model = std::make_unique<TransformerModel<CPWLLeaf>>();
    model->setConfig(TransformerConfig::Jensen_JT115KE());
    model->setProcessingMode(ProcessingMode::Realtime);
    model->setInputGain(0.0f);
    model->setOutputGain(0.0f);
    model->setMix(1.0f);
    model->prepareToPlay(sampleRate, 512);

    const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

    // Warmup (process in 512-sample chunks to respect maxBlockSize)
    {
        std::vector<float> warmIn(4096), warmOut(4096);
        for (int i = 0; i < 4096; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        for (int offset = 0; offset < 4096; offset += 512)
            model->processBlock(warmIn.data() + offset, warmOut.data() + offset, 512);
    }

    // Process measurement block
    std::vector<float> input(numSamples), output(numSamples);
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

    // Process in 512-sample blocks
    for (int offset = 0; offset < numSamples; offset += 512)
    {
        int blockSize = std::min(512, numSamples - offset);
        model->processBlock(input.data() + offset, output.data() + offset, blockSize);
    }

    auto hl = measureHarmonics(output.data(), numSamples, 1000.0, 44100.0);

    std::printf("  H1 = %.2f dB  (%.6f)\n", toDB(hl.h1), hl.h1);
    std::printf("  H2 = %.2f dB  (%.6f)\n", toDB(hl.h2), hl.h2);
    std::printf("  H3 = %.2f dB  (%.6f)\n", toDB(hl.h3), hl.h3);
    std::printf("  H4 = %.2f dB  (%.6f)\n", toDB(hl.h4), hl.h4);
    std::printf("  H5 = %.2f dB  (%.6f)\n", toDB(hl.h5), hl.h5);
    std::printf("  THD = %.4f%%\n", hl.thdPercent);

    // For a balanced/centrosymmetric transformer, odd harmonics dominate
    double h3_dB = toDB(hl.h3);
    double h2_dB = toDB(hl.h2);
    double h5_dB = toDB(hl.h5);
    double h4_dB = toDB(hl.h4);

    CHECK(h3_dB > h2_dB + 6.0,
        "H3 > H2 + 6 dB (odd dominance, 3rd vs 2nd)");
    CHECK(h5_dB > h4_dB + 6.0,
        "H5 > H4 + 6 dB (odd dominance, 5th vs 4th)");
    CHECK(hl.thdPercent > 0.01,
        "THD > 0.01% (model produces measurable distortion)");
    CHECK(hl.thdPercent < 35.0,
        "THD < 35% (bounded saturation at +6 dBFS)");
}

// ---- Test 2: JT-115K-E odd dominance (Artistic / J-A) ----------------------

void test_jt115ke_odd_dominance_Artistic()
{
    std::printf("\n=== Jensen JT-115K-E Harmonic Order — Artistic (J-A) ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 8192;
    const float freq = 1000.0f;
    const float amplitude = 2.0f;

    // Heap-allocate to avoid stack overflow
    auto model = std::make_unique<TransformerModel<JilesAthertonLeaf<LangevinPade>>>();
    model->setConfig(TransformerConfig::Jensen_JT115KE());
    model->setProcessingMode(ProcessingMode::Artistic);
    model->setInputGain(0.0f);
    model->setOutputGain(0.0f);
    model->setMix(1.0f);
    model->prepareToPlay(sampleRate, 512);

    const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

    // Warmup (process in 512-sample chunks to respect maxBlockSize)
    {
        std::vector<float> warmIn(4096), warmOut(4096);
        for (int i = 0; i < 4096; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        for (int offset = 0; offset < 4096; offset += 512)
            model->processBlock(warmIn.data() + offset, warmOut.data() + offset, 512);
    }

    // Process measurement block
    std::vector<float> input(numSamples), output(numSamples);
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

    for (int offset = 0; offset < numSamples; offset += 512)
    {
        int blockSize = std::min(512, numSamples - offset);
        model->processBlock(input.data() + offset, output.data() + offset, blockSize);
    }

    auto hl = measureHarmonics(output.data(), numSamples, 1000.0, 44100.0);

    std::printf("  H1 = %.2f dB  (%.6f)\n", toDB(hl.h1), hl.h1);
    std::printf("  H2 = %.2f dB  (%.6f)\n", toDB(hl.h2), hl.h2);
    std::printf("  H3 = %.2f dB  (%.6f)\n", toDB(hl.h3), hl.h3);
    std::printf("  H4 = %.2f dB  (%.6f)\n", toDB(hl.h4), hl.h4);
    std::printf("  H5 = %.2f dB  (%.6f)\n", toDB(hl.h5), hl.h5);
    std::printf("  THD = %.4f%%\n", hl.thdPercent);

    double h3_dB = toDB(hl.h3);
    double h2_dB = toDB(hl.h2);
    double h5_dB = toDB(hl.h5);
    double h4_dB = toDB(hl.h4);

    CHECK(h3_dB > h2_dB + 6.0,
        "Artistic: H3 > H2 + 6 dB (odd dominance)");
    CHECK(h5_dB > h4_dB + 6.0,
        "Artistic: H5 > H4 + 6 dB (odd dominance)");
    CHECK(hl.thdPercent > 0.01,
        "Artistic: THD > 0.01%");
    CHECK(hl.thdPercent < 35.0,
        "Artistic: THD < 35% (bounded saturation at +6 dBFS)");
}

// ---- Test 3: THD increases with drive level ---------------------------------

void test_thd_vs_drive()
{
    std::printf("\n=== Jensen JT-115K-E — THD vs Drive Level ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 8192;
    const float freq = 1000.0f;

    // Heap-allocate to avoid stack overflow
    auto model = std::make_unique<TransformerModel<CPWLLeaf>>();
    model->setConfig(TransformerConfig::Jensen_JT115KE());
    model->setProcessingMode(ProcessingMode::Realtime);
    model->setInputGain(0.0f);
    model->setOutputGain(0.0f);
    model->setMix(1.0f);

    const float drives[] = {0.1f, 0.5f, 1.0f, 2.0f};
    const int numDrives = 4;
    double thdValues[4] = {};

    for (int d = 0; d < numDrives; ++d)
    {
        model->reset();
        model->prepareToPlay(sampleRate, 512);

        const float amplitude = drives[d];
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        // Warmup (process in 512-sample chunks to respect maxBlockSize)
        {
            std::vector<float> warmIn(4096), warmOut(4096);
            for (int i = 0; i < 4096; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            for (int offset = 0; offset < 4096; offset += 512)
                model->processBlock(warmIn.data() + offset, warmOut.data() + offset, 512);
        }

        // Measurement
        std::vector<float> input(numSamples), output(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

        for (int offset = 0; offset < numSamples; offset += 512)
        {
            int blockSize = std::min(512, numSamples - offset);
            model->processBlock(input.data() + offset, output.data() + offset, blockSize);
        }

        thdValues[d] = test::computeTHD(output.data(), numSamples, 1000.0, 44100.0, 8);
        std::printf("  amplitude=%.2f: THD=%.4f%%\n", amplitude, thdValues[d]);
    }

    // THD should generally increase with drive level
    // We check that the highest drive has more THD than the lowest
    CHECK(thdValues[3] > thdValues[0],
        "THD at +6 dBFS > THD at -20 dBFS (THD increases with drive)");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  Jensen Harmonics Test Suite\n");
    std::printf("================================================================\n");

    test_jt115ke_odd_dominance_realtime();
    test_jt115ke_odd_dominance_Artistic();
    test_thd_vs_drive();

    test::printSummary("test_jensen_harmonics");
    return (test::g_fail() > 0) ? 1 : 0;
}
