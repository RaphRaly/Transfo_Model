// =============================================================================
// Test: Jensen Frequency-Dependent Bertotti Losses
//
// Validates that the Bertotti dynamic loss model produces frequency-dependent
// behavior consistent with the physical field separation model:
//   - Eddy current losses increase with frequency (dB/dt grows with f)
//   - THD varies across frequency due to the K1*dB/dt + K2*sqrt(|dB/dt|) terms
//   - Output level at HF remains within +-3 dB of 1 kHz reference
//
// Tests both the standalone HysteresisModel+DynamicLosses path and the
// full TransformerModel to validate end-to-end behavior.
//
// Coverage item: Frequency-dependent Bertotti losses for Jensen transformers
// =============================================================================

#include "test_common.h"
#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/TransformerConfig.h"
#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/DynamicLosses.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/util/Constants.h"

#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

using namespace transfo;

// ---- Helpers ----------------------------------------------------------------

static float computeRMS(const float* buf, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

// ---- Test 1: THD varies with frequency (full model) -------------------------

void test_thd_freq_dependence()
{
    std::printf("\n=== Bertotti — THD vs Frequency (JT-115K-E Realtime) ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 8192;
    const float amplitude = 0.5f;  // -6 dBFS

    const double frequencies[] = {100.0, 1000.0, 10000.0};
    const int numFreqs = 3;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    double thdValues[3] = {};
    double rmsValues[3] = {};

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        model.reset();
        model.prepareToPlay(sampleRate, 512);

        const float freq = static_cast<float>(frequencies[fi]);
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        // Warmup: 4096 samples
        {
            std::vector<float> warmIn(4096), warmOut(4096);
            for (int i = 0; i < 4096; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            model.processBlock(warmIn.data(), warmOut.data(), 4096);
        }

        // Measurement block
        std::vector<float> input(numSamples), output(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

        for (int offset = 0; offset < numSamples; offset += 512)
        {
            int blockSize = std::min(512, numSamples - offset);
            model.processBlock(input.data() + offset, output.data() + offset, blockSize);
        }

        thdValues[fi] = test::computeTHD(output.data(), numSamples,
                                          frequencies[fi], sampleRate, 8);
        rmsValues[fi] = static_cast<double>(computeRMS(output.data(), numSamples));

        std::printf("  %6.0f Hz: THD=%.6f%%  RMS_out=%.6f\n",
                    frequencies[fi], thdValues[fi], rmsValues[fi]);
    }

    // THD should differ meaningfully across frequencies
    // The Bertotti model changes the effective field at different rates per frequency
    double thdDiff_100_10k = std::abs(thdValues[0] - thdValues[2]);
    std::printf("  |THD@100Hz - THD@10kHz| = %.6f%%\n", thdDiff_100_10k);

    CHECK(thdDiff_100_10k > 0.001,
        "THD differs by > 0.001% between 100 Hz and 10 kHz");

    // Output at 10 kHz should be within +-3 dB of 1 kHz (no catastrophic rolloff)
    double rmsRatio_10k_1k = rmsValues[2] / (rmsValues[1] + 1e-30);
    double ratioDB = 20.0 * std::log10(rmsRatio_10k_1k + 1e-30);
    std::printf("  RMS ratio 10kHz/1kHz = %.4f (%.2f dB)\n", rmsRatio_10k_1k, ratioDB);

    CHECK(std::abs(ratioDB) < 3.0,
        "10 kHz output within +/-3 dB of 1 kHz");
}

// ---- Test 2: Standalone Bertotti loop area increases with frequency ----------

void test_bertotti_loop_area()
{
    std::printf("\n=== Bertotti — Loop Area vs Frequency (Standalone) ===\n");

    auto params = JAParameterSet::defaultMuMetal();

    const double freqs[] = {100.0, 1000.0, 5000.0};
    const int numFreqs = 3;
    const double Hmax = 150.0;
    const double sampleRate = 44100.0;
    const int numCycles = 5;

    double areas[3] = {};

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        HysteresisModel<LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(sampleRate);
        model.reset();

        DynamicLosses dyn;
        dyn.setCoefficients(params.K1, params.K2);
        dyn.setSampleRate(sampleRate);
        dyn.reset();

        int samplesPerCycle = std::max(4, static_cast<int>(std::round(sampleRate / freqs[fi])));
        int totalSamples = samplesPerCycle * numCycles;

        // Collect last cycle's B-H points
        std::vector<double> Hvals, Bvals;
        Hvals.reserve(static_cast<size_t>(samplesPerCycle));
        Bvals.reserve(static_cast<size_t>(samplesPerCycle));

        for (int n = 0; n < totalSamples; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            double H = Hmax * std::sin(2.0 * test::PI * freqs[fi] * t);

            double M_c = model.getMagnetization();
            double chi = std::max(0.0, model.getInstantaneousSusceptibility());
            double B_pred = kMu0 * (H + M_c);
            double dBdt_raw = dyn.computeDBdt(B_pred);
            double G = dyn.getK1() * dyn.getSampleRate() * kMu0 * chi;
            double dBdt = dBdt_raw * (1.0 + chi) / (1.0 + G);
            double Hdyn = dyn.computeHfromDBdt(dBdt);
            double H_eff = H - Hdyn;

            double M = model.solveImplicitStep(H_eff);
            model.commitState();

            double B = kMu0 * (H + M);
            dyn.commitState(B);

            if (n >= samplesPerCycle * (numCycles - 1))
            {
                Hvals.push_back(H);
                Bvals.push_back(B);
            }
        }

        // Shoelace formula for loop area
        double area = 0.0;
        int N = static_cast<int>(Hvals.size());
        for (int i = 0; i < N; ++i)
        {
            int j = (i + 1) % N;
            area += Hvals[static_cast<size_t>(i)] * Bvals[static_cast<size_t>(j)]
                  - Hvals[static_cast<size_t>(j)] * Bvals[static_cast<size_t>(i)];
        }
        areas[fi] = std::abs(area) / 2.0;

        std::printf("  %6.0f Hz: loop area = %.6e\n", freqs[fi], areas[fi]);
    }

    // Loop area should increase with frequency (dynamic losses add dissipation)
    CHECK(areas[1] > areas[0],
        "Loop area at 1 kHz > 100 Hz (dynamic losses increase with frequency)");
    CHECK(areas[2] > areas[0],
        "Loop area at 5 kHz > 100 Hz");
}

// ---- Test 3: Dynamic losses enabled check -----------------------------------

void test_dynamic_losses_enabled()
{
    std::printf("\n=== Bertotti — Dynamic Losses Enabled for Jensen Presets ===\n");

    auto muMetal = JAParameterSet::defaultMuMetal();
    auto nife50  = JAParameterSet::output50NiFe();

    // MuMetal should have nonzero K1 and/or K2
    DynamicLosses dyn1;
    dyn1.setCoefficients(muMetal.K1, muMetal.K2);
    std::printf("  MuMetal:  K1=%.6f, K2=%.6f, enabled=%s\n",
                muMetal.K1, muMetal.K2, dyn1.isEnabled() ? "yes" : "no");
    CHECK(dyn1.isEnabled(),
        "MuMetal dynamic losses enabled (K1 > 0 or K2 > 0)");

    // Output 50% NiFe
    DynamicLosses dyn2;
    dyn2.setCoefficients(nife50.K1, nife50.K2);
    std::printf("  50%%NiFe:  K1=%.6f, K2=%.6f, enabled=%s\n",
                nife50.K1, nife50.K2, dyn2.isEnabled() ? "yes" : "no");
    CHECK(dyn2.isEnabled(),
        "50%% NiFe dynamic losses enabled");
}

// ---- Test 4: RMS monotonicity check across frequency ------------------------

void test_rms_across_frequency()
{
    std::printf("\n=== Bertotti — RMS Output Across Frequency ===\n");

    const float sampleRate = 44100.0f;
    const int numSamples = 8192;
    const float amplitude = 0.5f;

    const double frequencies[] = {100.0, 500.0, 1000.0, 5000.0, 10000.0};
    const int numFreqs = 5;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    double rmsValues[5] = {};

    for (int fi = 0; fi < numFreqs; ++fi)
    {
        model.reset();
        model.prepareToPlay(sampleRate, 512);

        const float freq = static_cast<float>(frequencies[fi]);
        const float omega = 2.0f * static_cast<float>(test::PI) * freq / sampleRate;

        // Warmup
        {
            std::vector<float> warmIn(4096), warmOut(4096);
            for (int i = 0; i < 4096; ++i)
                warmIn[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
            model.processBlock(warmIn.data(), warmOut.data(), 4096);
        }

        std::vector<float> input(numSamples), output(numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i + 4096));

        for (int offset = 0; offset < numSamples; offset += 512)
        {
            int blockSize = std::min(512, numSamples - offset);
            model.processBlock(input.data() + offset, output.data() + offset, blockSize);
        }

        rmsValues[fi] = static_cast<double>(computeRMS(output.data(), numSamples));
        std::printf("  %6.0f Hz: RMS = %.6f\n", frequencies[fi], rmsValues[fi]);
    }

    // No output should be more than 6 dB away from the 1 kHz reference
    double rms1k = rmsValues[2];
    bool allWithin6dB = true;
    for (int fi = 0; fi < numFreqs; ++fi)
    {
        double ratioDB = 20.0 * std::log10((rmsValues[fi] + 1e-30) / (rms1k + 1e-30));
        if (std::abs(ratioDB) > 6.0)
        {
            std::printf("    *** %6.0f Hz: %.2f dB from 1 kHz reference ***\n",
                        frequencies[fi], ratioDB);
            allWithin6dB = false;
        }
    }

    CHECK(allWithin6dB,
        "All frequencies within +/-6 dB of 1 kHz reference");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::printf("================================================================\n");
    std::printf("  Jensen Bertotti Frequency-Dependent Losses Test Suite\n");
    std::printf("================================================================\n");

    test_thd_freq_dependence();
    test_bertotti_loop_area();
    test_dynamic_losses_enabled();
    test_rms_across_frequency();

    test::printSummary("test_jensen_bertotti_freq");
    return (test::g_fail() > 0) ? 1 : 0;
}
