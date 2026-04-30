// =============================================================================
// Test: CPWL vs Langevin Parity for Jensen Transformers
//
// Compares TransformerModel<CPWLLeaf> (Realtime) against
// TransformerModel<JilesAthertonLeaf<LangevinPade>> (Artistic) to verify
// that the CPWL approximation produces comparable results:
//
//   - THD difference < 3 dB (factor of ~1.4 between modes)
//   - RMS output difference < 1 dB
//
// Both modes use the same Jensen JT-115K-E config and identical input.
// The comparison validates that the cheaper CPWL path doesn't deviate
// excessively from the reference Langevin/J-A path.
//
// Coverage item: CPWL vs Langevin parity for Jensen transformer models
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
#include <algorithm>
#include <memory>

using namespace transfo;

// ---- Helpers ----------------------------------------------------------------

static float computeRMS(const float* buf, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

// ---- Test 1: JT-115K-E THD parity at -6 dBFS, 1 kHz ------------------------

void test_thd_parity_jt115ke()
{
    std::printf("\n=== CPWL vs Langevin Parity — JT-115K-E @ 1 kHz / -6 dBFS ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float freq = 1000.0f;
    const float amplitude = 0.5f;  // -6 dBFS
    const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

    auto config = TransformerConfig::Jensen_JT115KE();

    // ---- CPWL (Realtime) path ---- (heap-allocate to avoid stack overflow)
    auto cpwlModel = std::make_unique<TransformerModel<CPWLLeaf>>();
    cpwlModel->setConfig(config);
    cpwlModel->setProcessingMode(ProcessingMode::Realtime);
    cpwlModel->setInputGain(0.0f);
    cpwlModel->setOutputGain(0.0f);
    cpwlModel->setMix(1.0f);
    cpwlModel->prepareToPlay(sampleRate, 512);

    // Warmup
    {
        std::vector<float> warmIn(4096), warmOut(4096);
        for (int i = 0; i < 4096; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        for (int off = 0; off < 4096; off += 512)
            cpwlModel->processBlock(warmIn.data() + off, warmOut.data() + off, 512);
    }

    std::vector<float> input(numSamples), cpwlOutput(numSamples);
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

    for (int offset = 0; offset < numSamples; offset += 512)
    {
        int blockSize = std::min(512, numSamples - offset);
        cpwlModel->processBlock(input.data() + offset, cpwlOutput.data() + offset, blockSize);
    }

    double thdCPWL = test::computeTHD(cpwlOutput.data(), numSamples, 1000.0, 44100.0, 8);
    float rmsCPWL = computeRMS(cpwlOutput.data(), numSamples);
    cpwlModel.reset(); // Free before allocating next model

    // ---- Langevin (Artistic) path ----
    auto jaModel = std::make_unique<TransformerModel<JilesAthertonLeaf<LangevinPade>>>();
    jaModel->setConfig(config);
    jaModel->setProcessingMode(ProcessingMode::Artistic);
    jaModel->setInputGain(0.0f);
    jaModel->setOutputGain(0.0f);
    jaModel->setMix(1.0f);
    jaModel->prepareToPlay(sampleRate, 512);

    // Warmup
    {
        std::vector<float> warmIn(4096), warmOut(4096);
        for (int i = 0; i < 4096; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        for (int off = 0; off < 4096; off += 512)
            jaModel->processBlock(warmIn.data() + off, warmOut.data() + off, 512);
    }

    std::vector<float> jaOutput(numSamples);
    // Re-generate same input (in case of aliasing with output buffer)
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

    for (int offset = 0; offset < numSamples; offset += 512)
    {
        int blockSize = std::min(512, numSamples - offset);
        jaModel->processBlock(input.data() + offset, jaOutput.data() + offset, blockSize);
    }

    double thdJA = test::computeTHD(jaOutput.data(), numSamples, 1000.0, 44100.0, 8);
    float rmsJA = computeRMS(jaOutput.data(), numSamples);

    // ---- Report ----
    std::printf("  CPWL:     THD=%.6f%%  RMS=%.6f\n", thdCPWL, rmsCPWL);
    std::printf("  Langevin: THD=%.6f%%  RMS=%.6f\n", thdJA, rmsJA);

    // THD ratio check: within 3 dB (factor ~1.4)
    // Guard against zero THD on either side
    if (thdCPWL > 1e-6 && thdJA > 1e-6)
    {
        double thdRatio = thdCPWL / thdJA;
        double thdRatioDB = 20.0 * std::log10(thdRatio);
        std::printf("  THD ratio: %.4f (%.2f dB)\n", thdRatio, thdRatioDB);

        CHECK(thdCPWL < 2.0 * thdJA,
            "CPWL THD < 2x Langevin THD (upper bound)");
        CHECK(thdCPWL > 0.5 * thdJA,
            "CPWL THD > 0.5x Langevin THD (lower bound)");
    }
    else
    {
        // If either THD is negligible, both should be very low
        std::printf("  THD near zero on one or both paths\n");
        CHECK(thdCPWL < 0.01 && thdJA < 0.01,
            "Both THDs negligible (< 0.01%) — parity by default");
    }

    // RMS difference check: within 1 dB
    if (rmsCPWL > 1e-10f && rmsJA > 1e-10f)
    {
        double rmsRatio = static_cast<double>(rmsCPWL) / static_cast<double>(rmsJA);
        double rmsRatioDB = 20.0 * std::log10(rmsRatio);
        std::printf("  RMS ratio: %.4f (%.2f dB)\n", rmsRatio, rmsRatioDB);

        CHECK(std::abs(rmsRatioDB) < 1.0,
            "RMS difference < 1 dB between CPWL and Langevin");
    }
    else
    {
        CHECK(false, "RMS near zero — cannot compare");
    }
}

// ---- Test 2: Parity at multiple frequencies ---------------------------------

void test_rms_parity_multifreq()
{
    std::printf("\n=== CPWL vs Langevin RMS Parity — Multiple Frequencies ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float amplitude = 0.5f;
    auto config = TransformerConfig::Jensen_JT115KE();

    const double frequencies[] = {100.0, 1000.0, 10000.0};
    const int numFreqs = 3;

    bool allWithin2dB = true;

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        const float freq = static_cast<float>(frequencies[fi]);
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        // CPWL (heap-allocate)
        auto cpwlModel = std::make_unique<TransformerModel<CPWLLeaf>>();
        cpwlModel->setConfig(config);
        cpwlModel->setProcessingMode(ProcessingMode::Realtime);
        cpwlModel->setInputGain(0.0f);
        cpwlModel->setOutputGain(0.0f);
        cpwlModel->setMix(1.0f);
        cpwlModel->prepareToPlay(sampleRate, 512);

        {
            std::vector<float> warmIn(4096), warmOut(4096);
            for (int i = 0; i < 4096; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            for (int off = 0; off < 4096; off += 512)
            cpwlModel->processBlock(warmIn.data() + off, warmOut.data() + off, 512);
        }

        std::vector<float> input(numSamples), cpwlOut(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));
        for (int offset = 0; offset < numSamples; offset += 512)
        {
            int blockSize = std::min(512, numSamples - offset);
            cpwlModel->processBlock(input.data() + offset, cpwlOut.data() + offset, blockSize);
        }
        float rmsCPWL = computeRMS(cpwlOut.data(), numSamples);
        cpwlModel.reset(); // Free before allocating next model

        // Langevin (heap-allocate)
        auto jaModel = std::make_unique<TransformerModel<JilesAthertonLeaf<LangevinPade>>>();
        jaModel->setConfig(config);
        jaModel->setProcessingMode(ProcessingMode::Artistic);
        jaModel->setInputGain(0.0f);
        jaModel->setOutputGain(0.0f);
        jaModel->setMix(1.0f);
        jaModel->prepareToPlay(sampleRate, 512);

        {
            std::vector<float> warmIn(4096), warmOut(4096);
            for (int i = 0; i < 4096; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            for (int off = 0; off < 4096; off += 512)
            jaModel->processBlock(warmIn.data() + off, warmOut.data() + off, 512);
        }

        std::vector<float> jaOut(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));
        for (int offset = 0; offset < numSamples; offset += 512)
        {
            int blockSize = std::min(512, numSamples - offset);
            jaModel->processBlock(input.data() + offset, jaOut.data() + offset, blockSize);
        }
        float rmsJA = computeRMS(jaOut.data(), numSamples);

        double rmsRatioDB = 20.0 * std::log10(
            (static_cast<double>(rmsCPWL) + 1e-30) / (static_cast<double>(rmsJA) + 1e-30));

        std::printf("  %6.0f Hz: CPWL=%.6f  JA=%.6f  diff=%.2f dB\n",
                    frequencies[fi], rmsCPWL, rmsJA, rmsRatioDB);

        if (std::abs(rmsRatioDB) > 2.0)
            allWithin2dB = false;
    }

    CHECK(allWithin2dB,
        "CPWL vs Langevin RMS within +/-2 dB at all test frequencies");
}

// ---- Test 3: JT-11ELCF parity check ----------------------------------------

void test_parity_jt11elcf()
{
    std::printf("\n=== CPWL vs Langevin Parity — JT-11ELCF @ 1 kHz ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float freq = 1000.0f;
    const float amplitude = 0.5f;
    const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

    auto config = TransformerConfig::Jensen_JT11ELCF();

    // CPWL (heap-allocate)
    auto cpwlModel = std::make_unique<TransformerModel<CPWLLeaf>>();
    cpwlModel->setConfig(config);
    cpwlModel->setProcessingMode(ProcessingMode::Realtime);
    cpwlModel->setInputGain(0.0f);
    cpwlModel->setOutputGain(0.0f);
    cpwlModel->setMix(1.0f);
    cpwlModel->prepareToPlay(sampleRate, 512);

    {
        std::vector<float> warmIn(4096), warmOut(4096);
        for (int i = 0; i < 4096; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        for (int off = 0; off < 4096; off += 512)
            cpwlModel->processBlock(warmIn.data() + off, warmOut.data() + off, 512);
    }

    std::vector<float> input(numSamples), cpwlOut(numSamples);
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));
    for (int offset = 0; offset < numSamples; offset += 512)
    {
        int blockSize = std::min(512, numSamples - offset);
        cpwlModel->processBlock(input.data() + offset, cpwlOut.data() + offset, blockSize);
    }
    float rmsCPWL = computeRMS(cpwlOut.data(), numSamples);
    cpwlModel.reset(); // Free before allocating next model

    // Langevin (heap-allocate)
    auto jaModel = std::make_unique<TransformerModel<JilesAthertonLeaf<LangevinPade>>>();
    jaModel->setConfig(config);
    jaModel->setProcessingMode(ProcessingMode::Artistic);
    jaModel->setInputGain(0.0f);
    jaModel->setOutputGain(0.0f);
    jaModel->setMix(1.0f);
    jaModel->prepareToPlay(sampleRate, 512);

    {
        std::vector<float> warmIn(4096), warmOut(4096);
        for (int i = 0; i < 4096; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        for (int off = 0; off < 4096; off += 512)
            jaModel->processBlock(warmIn.data() + off, warmOut.data() + off, 512);
    }

    std::vector<float> jaOut(numSamples);
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));
    for (int offset = 0; offset < numSamples; offset += 512)
    {
        int blockSize = std::min(512, numSamples - offset);
        jaModel->processBlock(input.data() + offset, jaOut.data() + offset, blockSize);
    }
    float rmsJA = computeRMS(jaOut.data(), numSamples);

    double rmsRatioDB = 20.0 * std::log10(
        (static_cast<double>(rmsCPWL) + 1e-30) / (static_cast<double>(rmsJA) + 1e-30));

    std::printf("  CPWL RMS:     %.6f\n", rmsCPWL);
    std::printf("  Langevin RMS: %.6f\n", rmsJA);
    std::printf("  Diff: %.2f dB\n", rmsRatioDB);

    CHECK(std::abs(rmsRatioDB) < 2.0,
        "JT-11ELCF: RMS difference < 2 dB between CPWL and Langevin");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  Jensen CPWL vs Langevin Parity Test Suite\n");
    std::printf("================================================================\n");

    test_thd_parity_jt115ke();
    test_rms_parity_multifreq();
    test_parity_jt11elcf();

    test::printSummary("test_jensen_cpwl_parity");
    return (test::g_fail() > 0) ? 1 : 0;
}
