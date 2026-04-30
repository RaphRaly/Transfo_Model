// =============================================================================
// Test: Jensen Full-Model Energy Passivity
//
// Validates that the TransformerModel (WDF circuit) does not amplify energy
// beyond the expected gain ceiling across the audio band.
//
// For the JT-115K-E, the WDF circuit normalizes output to approximate unity
// gain. We verify that RMS_out <= RMS_in * 1.2 (20% margin for any LC
// resonance peaking) across a 10-frequency sweep at -6 dBFS.
//
// Coverage item: Full-model energy passivity (frequency sweep)
// =============================================================================

#include "test_common.h"
#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/TransformerConfig.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"

#include <vector>
#include <cmath>
#include <cstdio>

using namespace transfo;

// ---- Helpers ----------------------------------------------------------------

static float computeRMS(const float* buf, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

// ---- Test 1: JT-115K-E Realtime (CPWL) passivity across frequency -----------

void test_jt115ke_passivity_realtime()
{
    std::printf("\n=== Jensen JT-115K-E Passivity — Realtime (CPWL) ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float amplitude = 0.5f;  // -6 dBFS
    const float passivityMargin = 1.2f;  // allow up to 20% gain from resonance

    const double frequencies[] = {
        20.0, 50.0, 100.0, 200.0, 500.0,
        1000.0, 2000.0, 5000.0, 10000.0, 20000.0
    };
    const int numFreqs = 10;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    bool allPassive = true;

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        model.reset();
        model.prepareToPlay(sampleRate, numSamples);

        const float freq = static_cast<float>(frequencies[fi]);
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        // Warmup: 2048 samples to settle filters
        {
            std::vector<float> warmIn(numSamples), warmOut(numSamples);
            for (int i = 0; i < numSamples; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            model.processBlock(warmIn.data(), warmOut.data(), numSamples);
        }

        // Measurement block
        std::vector<float> input(numSamples), output(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + numSamples));

        model.processBlock(input.data(), output.data(), numSamples);

        float rmsIn  = computeRMS(input.data(), numSamples);
        float rmsOut = computeRMS(output.data(), numSamples);
        float ratio  = (rmsIn > 1e-12f) ? (rmsOut / rmsIn) : 0.0f;

        std::printf("  %6.0f Hz: RMS_in=%.6f  RMS_out=%.6f  ratio=%.4f\n",
                    frequencies[fi], rmsIn, rmsOut, ratio);

        if (rmsOut > rmsIn * passivityMargin)
        {
            allPassive = false;
            std::printf("    *** VIOLATION: ratio %.4f > %.2f ***\n", ratio, passivityMargin);
        }
    }

    CHECK(allPassive,
        "JT-115K-E Realtime: RMS_out <= RMS_in * 1.2 at all frequencies");
}

// ---- Test 2: JT-11ELCF Realtime (CPWL) passivity across frequency -----------

void test_jt11elcf_passivity_realtime()
{
    std::printf("\n=== Jensen JT-11ELCF Passivity — Realtime (CPWL) ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float amplitude = 0.5f;
    const float passivityMargin = 1.2f;

    const double frequencies[] = {
        20.0, 50.0, 100.0, 200.0, 500.0,
        1000.0, 2000.0, 5000.0, 10000.0, 20000.0
    };
    const int numFreqs = 10;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(TransformerConfig::Jensen_JT11ELCF());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    bool allPassive = true;

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        model.reset();
        model.prepareToPlay(sampleRate, numSamples);

        const float freq = static_cast<float>(frequencies[fi]);
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        // Warmup
        {
            std::vector<float> warmIn(numSamples), warmOut(numSamples);
            for (int i = 0; i < numSamples; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            model.processBlock(warmIn.data(), warmOut.data(), numSamples);
        }

        // Measurement
        std::vector<float> input(numSamples), output(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + numSamples));

        model.processBlock(input.data(), output.data(), numSamples);

        float rmsIn  = computeRMS(input.data(), numSamples);
        float rmsOut = computeRMS(output.data(), numSamples);
        float ratio  = (rmsIn > 1e-12f) ? (rmsOut / rmsIn) : 0.0f;

        std::printf("  %6.0f Hz: RMS_in=%.6f  RMS_out=%.6f  ratio=%.4f\n",
                    frequencies[fi], rmsIn, rmsOut, ratio);

        if (rmsOut > rmsIn * passivityMargin)
        {
            allPassive = false;
            std::printf("    *** VIOLATION: ratio %.4f > %.2f ***\n", ratio, passivityMargin);
        }
    }

    CHECK(allPassive,
        "JT-11ELCF Realtime: RMS_out <= RMS_in * 1.2 at all frequencies");
}

// ---- Test 3: JT-115K-E Artistic (JA) passivity at 1 kHz --------------------

void test_jt115ke_passivity_Artistic()
{
    std::printf("\n=== Jensen JT-115K-E Passivity — Artistic (J-A) ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float amplitude = 0.5f;
    const float passivityMargin = 1.2f;
    const float freq = 1000.0f;

    TransformerModel<JilesAthertonLeaf<LangevinPade>> model;
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Artistic);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(sampleRate, numSamples);

    const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

    // Warmup
    {
        std::vector<float> warmIn(numSamples), warmOut(numSamples);
        for (int i = 0; i < numSamples; ++i)
            warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
        model.processBlock(warmIn.data(), warmOut.data(), numSamples);
    }

    // Measurement
    std::vector<float> input(numSamples), output(numSamples);
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + numSamples));

    model.processBlock(input.data(), output.data(), numSamples);

    float rmsIn  = computeRMS(input.data(), numSamples);
    float rmsOut = computeRMS(output.data(), numSamples);
    float ratio  = (rmsIn > 1e-12f) ? (rmsOut / rmsIn) : 0.0f;

    std::printf("  1 kHz: RMS_in=%.6f  RMS_out=%.6f  ratio=%.4f\n",
                rmsIn, rmsOut, ratio);

    CHECK(rmsOut <= rmsIn * passivityMargin,
        "JT-115K-E Artistic 1kHz: RMS_out <= RMS_in * 1.2");
}

// ---- Test 4: Output is finite and non-zero across sweep ---------------------

void test_output_finite_nonzero()
{
    std::printf("\n=== Jensen JT-115K-E — Output Finite & Non-Zero ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 2048;
    const float amplitude = 0.5f;

    const double frequencies[] = {20.0, 100.0, 1000.0, 10000.0, 20000.0};
    const int numFreqs = 5;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    bool allFinite = true;
    bool allNonZero = true;

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        model.reset();
        model.prepareToPlay(sampleRate, numSamples);

        const float freq = static_cast<float>(frequencies[fi]);
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        std::vector<float> input(numSamples), output(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));

        model.processBlock(input.data(), output.data(), numSamples);

        bool finite = true;
        bool nonZero = false;
        for (int i = 0; i < numSamples; ++i)
        {
            if (!std::isfinite(output[static_cast<size_t>(i)])) finite = false;
            if (std::abs(output[static_cast<size_t>(i)]) > 1e-10f) nonZero = true;
        }

        if (!finite) allFinite = false;
        if (!nonZero) allNonZero = false;

        std::printf("  %6.0f Hz: finite=%s  nonzero=%s\n",
                    frequencies[fi],
                    finite ? "yes" : "NO",
                    nonZero ? "yes" : "NO");
    }

    CHECK(allFinite,  "All output samples are finite at all frequencies");
    CHECK(allNonZero, "Output is non-zero at all frequencies");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  Jensen Passivity Test Suite\n");
    std::printf("================================================================\n");

    test_jt115ke_passivity_realtime();
    test_jt11elcf_passivity_realtime();
    test_jt115ke_passivity_Artistic();
    test_output_finite_nonzero();

    test::printSummary("test_jensen_passivity");
    return (test::g_fail() > 0) ? 1 : 0;
}
