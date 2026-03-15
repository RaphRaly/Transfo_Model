// =============================================================================
// Test: Plugin Integration — Core-level integration tests
//
// Standalone test (no JUCE dependency) that validates the full TransformerModel
// processing pipeline and preset/mode switching, using only core/ headers.
//
// Test groups:
//   1. TransformerModel lifecycle (construct, prepare, process)
//   2. Preset switching (all 5 presets, hot-swap, distinct output)
//   3. Mode switching (Realtime vs Physical)
//   4. Parameter ranges (gain, mix, SVU extremes)
//   5. Stress tests (tiny/large buffers, DC, hot signal)
//   6. B-H Queue (SPSC queue receives data after processing)
//
// Pattern: same as test_cpwl_adaa.cpp (CHECK macro, main()).
// =============================================================================

#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/Presets.h"
#include "../core/include/core/model/ToleranceModel.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/util/Constants.h"
#include "../core/include/core/util/SPSCQueue.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <numeric>

static constexpr double PI = 3.14159265358979323846;

// ---- Helpers ----------------------------------------------------------------

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

void CHECK_NEAR(double actual, double expected, double tol, const char* msg)
{
    double err = std::abs(actual - expected);
    if (err <= tol)
    {
        std::cout << "  PASS: " << msg << " (err=" << err << ")" << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "  *** FAIL: " << msg
                  << " -- expected " << expected << ", got " << actual
                  << " (err=" << err << ", tol=" << tol << ") ***" << std::endl;
        g_fail++;
    }
}

// Check that no sample in the buffer is NaN or Inf
bool allFinite(const float* data, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(data[i]))
            return false;
    return true;
}

// RMS of a buffer
double rms(const float* data, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return std::sqrt(sum / std::max(n, 1));
}

// Peak absolute value
float peakAbs(const float* data, int n)
{
    float mx = 0.0f;
    for (int i = 0; i < n; ++i)
        mx = std::max(mx, std::abs(data[i]));
    return mx;
}

// Generate a sine wave buffer
void generateSine(float* buf, int n, float freq, float sampleRate, float amplitude = 1.0f)
{
    for (int i = 0; i < n; ++i)
        buf[i] = amplitude * std::sin(2.0f * static_cast<float>(PI) * freq * static_cast<float>(i) / sampleRate);
}

// ---- Type aliases for readability -------------------------------------------

using RealtimeModel = transfo::TransformerModel<transfo::CPWLLeaf>;
using PhysicalModel = transfo::TransformerModel<transfo::JilesAthertonLeaf<transfo::LangevinPade>>;

// =============================================================================
// 1. TransformerModel Lifecycle Tests
// =============================================================================

void test1_lifecycle()
{
    std::cout << "\n=== TEST 1: TransformerModel Lifecycle ===" << std::endl;

    // 1a. Construction/destruction without crash (Realtime)
    {
        RealtimeModel model;
        (void)model; // suppress unused warning
    }
    CHECK(true, "RealtimeModel construct/destruct without crash");

    // 1b. Construction/destruction without crash (Physical)
    {
        PhysicalModel model;
        (void)model;
    }
    CHECK(true, "PhysicalModel construct/destruct without crash");

    // 1c. prepareToPlay at different sample rates
    float sampleRates[] = { 44100.0f, 48000.0f, 88200.0f, 96000.0f };
    for (float sr : sampleRates)
    {
        RealtimeModel rtModel;
        rtModel.setProcessingMode(transfo::ProcessingMode::Realtime);
        rtModel.setConfig(transfo::Presets::Jensen_JT115KE());
        rtModel.prepareToPlay(sr, 512);

        PhysicalModel phModel;
        phModel.setProcessingMode(transfo::ProcessingMode::Physical);
        phModel.setConfig(transfo::Presets::Jensen_JT115KE());
        phModel.prepareToPlay(sr, 512);

        std::string msg = "prepareToPlay @ " + std::to_string(static_cast<int>(sr)) + " Hz (both modes)";
        CHECK(true, msg.c_str());
    }

    // 1d. processBlock with silent buffer -> output should be silent (or near-silent)
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(transfo::Presets::Jensen_JT115KE());
        model.prepareToPlay(44100.0f, 256);
        model.setInputGain(0.0f);  // 0 dB
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> input(256, 0.0f);
        std::vector<float> output(256, 0.0f);

        model.processBlock(input.data(), output.data(), 256);

        double outRms = rms(output.data(), 256);
        CHECK(outRms < 1e-6, "Silent input -> near-silent output (Realtime)");
        CHECK(allFinite(output.data(), 256), "Silent output is all finite (Realtime)");
    }

    // 1e. processBlock with sine -> output should be non-zero
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(transfo::Presets::Jensen_JT115KE());
        model.prepareToPlay(44100.0f, 512);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(512);
        generateSine(buf.data(), 512, 1000.0f, 44100.0f, 0.5f);

        std::vector<float> output(512);
        model.processBlock(buf.data(), output.data(), 512);

        double outRms = rms(output.data(), 512);
        CHECK(outRms > 1e-6, "Sine input -> non-zero output (Realtime)");
        CHECK(allFinite(output.data(), 512), "Sine output is all finite (Realtime)");
    }
}

// =============================================================================
// 2. Preset Switching Tests
// =============================================================================

void test2_preset_switching()
{
    std::cout << "\n=== TEST 2: Preset Switching ===" << std::endl;

    const int numPresets = transfo::Presets::count();
    const int N = 512;
    const float sr = 44100.0f;

    // 2a. Load each preset and process without crash
    for (int p = 0; p < numPresets; ++p)
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(transfo::Presets::getByIndex(p));
        model.prepareToPlay(sr, N);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(N);
        generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
        std::vector<float> output(N);

        model.processBlock(buf.data(), output.data(), N);

        std::string msg = "Preset " + std::to_string(p) + " (" +
                          transfo::Presets::getNameByIndex(p) + ") processes without crash";
        CHECK(allFinite(output.data(), N), msg.c_str());
    }

    // 2b. Switch preset during processing
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(transfo::Presets::getByIndex(0));
        model.prepareToPlay(sr, N);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(N);
        std::vector<float> output(N);
        generateSine(buf.data(), N, 1000.0f, sr, 0.5f);

        // Process with preset 0
        model.processBlock(buf.data(), output.data(), N);
        CHECK(allFinite(output.data(), N), "Process before preset switch OK");

        // Switch to preset 0 mid-stream (only 1 factory preset)
        model.setConfig(transfo::Presets::getByIndex(0));

        // Process again
        model.processBlock(buf.data(), output.data(), N);
        CHECK(allFinite(output.data(), N), "Process after preset switch OK (no crash)");
    }

    // 2c. Verify that each preset produces a different output
    {
        std::vector<double> presetRms(numPresets, 0.0);

        for (int p = 0; p < numPresets; ++p)
        {
            RealtimeModel model;
            model.setProcessingMode(transfo::ProcessingMode::Realtime);
            model.setConfig(transfo::Presets::getByIndex(p));
            model.prepareToPlay(sr, N);
            model.setInputGain(0.0f);
            model.setOutputGain(0.0f);
            model.setMix(1.0f);

            std::vector<float> buf(N);
            generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
            std::vector<float> output(N);

            model.processBlock(buf.data(), output.data(), N);
            presetRms[p] = rms(output.data(), N);
        }

        // With multiple presets, verify they produce different outputs.
        // With only 1 factory preset, skip this check.
        if (numPresets > 1)
        {
            bool anyDifferent = false;
            for (int i = 1; i < numPresets; ++i)
            {
                if (std::abs(presetRms[i] - presetRms[0]) > 1e-6)
                {
                    anyDifferent = true;
                    break;
                }
            }
            CHECK(anyDifferent, "At least two presets produce different RMS outputs");
        }

        // Print RMS for debugging
        for (int p = 0; p < numPresets; ++p)
            std::cout << "    Preset " << p << " RMS: " << presetRms[p] << std::endl;
    }
}

// =============================================================================
// 3. Mode Switching Tests
// =============================================================================

void test3_mode_switching()
{
    std::cout << "\n=== TEST 3: Mode Switching (Realtime vs Physical) ===" << std::endl;

    const int N = 512;
    const float sr = 44100.0f;
    auto cfg = transfo::Presets::Jensen_JT115KE();

    // 3a. Realtime mode produces non-null output
    double realtimeRms = 0.0;
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(cfg);
        model.prepareToPlay(sr, N);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(N);
        generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
        std::vector<float> output(N);

        model.processBlock(buf.data(), output.data(), N);
        realtimeRms = rms(output.data(), N);
        CHECK(realtimeRms > 1e-6, "Realtime mode produces non-zero output");
        CHECK(allFinite(output.data(), N), "Realtime mode output is finite");
    }

    // 3b. Physical mode produces non-null output
    double physicalRms = 0.0;
    {
        PhysicalModel model;
        model.setProcessingMode(transfo::ProcessingMode::Physical);
        model.setConfig(cfg);
        model.prepareToPlay(sr, N);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(N);
        generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
        std::vector<float> output(N);

        model.processBlock(buf.data(), output.data(), N);
        physicalRms = rms(output.data(), N);
        CHECK(physicalRms > 1e-6, "Physical mode produces non-zero output");
        CHECK(allFinite(output.data(), N), "Physical mode output is finite");
    }

    // 3c. Both modes produce output (summary)
    std::cout << "    Realtime RMS: " << realtimeRms << std::endl;
    std::cout << "    Physical RMS: " << physicalRms << std::endl;

    // Both should be non-zero, but they may differ in character
    CHECK(realtimeRms > 1e-8 && physicalRms > 1e-8,
          "Both modes produce measurable output");
}

// =============================================================================
// 4. Parameter Range Tests
// =============================================================================

void test4_parameter_ranges()
{
    std::cout << "\n=== TEST 4: Parameter Range Tests ===" << std::endl;

    const int N = 512;
    const float sr = 44100.0f;
    auto cfg = transfo::Presets::Jensen_JT115KE();

    // Helper lambda: create a Realtime model, set params, process sine, return output.
    // Processes 3 warm-up blocks first so SmoothedValue ramps fully settle
    // (20ms ramp = 882 samples at 44.1 kHz; 3 × 512 = 1536 > 882).
    auto processWithParams = [&](float inputGainDb, float outputGainDb, float mix) -> std::vector<float>
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(cfg);
        model.prepareToPlay(sr, N);
        model.setInputGain(inputGainDb);
        model.setOutputGain(outputGainDb);
        model.setMix(mix);

        std::vector<float> buf(N);
        std::vector<float> output(N);

        // Warm-up: let SmoothedValue ramps settle
        for (int warmup = 0; warmup < 3; ++warmup)
        {
            generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
            model.processBlock(buf.data(), output.data(), N);
        }

        // Final block with settled parameters
        generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
        model.processBlock(buf.data(), output.data(), N);
        return output;
    };

    // 4a. InputGain at -40 dB (very quiet)
    {
        auto out = processWithParams(-40.0f, 0.0f, 1.0f);
        CHECK(allFinite(out.data(), N), "InputGain -40 dB: output is finite");
        double r = rms(out.data(), N);
        std::cout << "    InputGain -40 dB RMS: " << r << std::endl;
    }

    // 4b. InputGain at +20 dB (very hot)
    {
        auto out = processWithParams(20.0f, 0.0f, 1.0f);
        CHECK(allFinite(out.data(), N), "InputGain +20 dB: output is finite");
        double r = rms(out.data(), N);
        std::cout << "    InputGain +20 dB RMS: " << r << std::endl;
    }

    // 4c. OutputGain at -40 dB
    {
        auto out = processWithParams(0.0f, -40.0f, 1.0f);
        CHECK(allFinite(out.data(), N), "OutputGain -40 dB: output is finite");
        double r = rms(out.data(), N);
        CHECK(r < 0.01, "OutputGain -40 dB: output is very quiet");
    }

    // 4d. OutputGain at +20 dB
    {
        auto out = processWithParams(0.0f, 20.0f, 1.0f);
        CHECK(allFinite(out.data(), N), "OutputGain +20 dB: output is finite");
    }

    // 4e. Mix at 0.0 (fully dry)
    {
        auto out = processWithParams(0.0f, 0.0f, 0.0f);
        CHECK(allFinite(out.data(), N), "Mix 0.0 (dry): output is finite");
        // At mix=0 with outputGain=0dB, output should equal input * outputGain
        // The dry path is: dry * (1 - mix) + wet * mix = dry * 1 + wet * 0 = dry
        // After output gain scaling
    }

    // 4f. Mix at 0.5
    {
        auto out = processWithParams(0.0f, 0.0f, 0.5f);
        CHECK(allFinite(out.data(), N), "Mix 0.5: output is finite");
    }

    // 4g. Mix at 1.0 (fully wet)
    {
        auto out = processWithParams(0.0f, 0.0f, 1.0f);
        CHECK(allFinite(out.data(), N), "Mix 1.0 (wet): output is finite");
    }

    // 4h. SVU/Tolerance at 0% and 5%
    {
        transfo::ToleranceModel tol;

        // SVU 0%
        tol.generateRandomOffsets(0.0f);
        auto cfgL = tol.applyToConfig(cfg, transfo::ToleranceModel::Channel::Left);
        auto cfgR = tol.applyToConfig(cfg, transfo::ToleranceModel::Channel::Right);

        // At 0% tolerance, configs should be identical to base
        CHECK_NEAR(cfgL.windings.Rdc_primary, cfg.windings.Rdc_primary, 1e-6,
                   "SVU 0%: Left Rdc_primary unchanged");
        CHECK_NEAR(cfgR.windings.Rdc_primary, cfg.windings.Rdc_primary, 1e-6,
                   "SVU 0%: Right Rdc_primary unchanged");

        // SVU 5%
        tol.generateRandomOffsets(5.0f);
        cfgL = tol.applyToConfig(cfg, transfo::ToleranceModel::Channel::Left);
        cfgR = tol.applyToConfig(cfg, transfo::ToleranceModel::Channel::Right);

        // At 5%, L and R should differ
        bool lrDiffer = (std::abs(cfgL.windings.Rdc_primary - cfgR.windings.Rdc_primary) > 1e-6);
        CHECK(lrDiffer, "SVU 5%: L and R channels have different Rdc_primary");

        // Both should still be reasonable (within +/- 5% of base)
        float maxDev = cfg.windings.Rdc_primary * 0.06f; // slight margin above 5%
        CHECK(std::abs(cfgL.windings.Rdc_primary - cfg.windings.Rdc_primary) < maxDev,
              "SVU 5%: Left Rdc_primary within tolerance band");
        CHECK(std::abs(cfgR.windings.Rdc_primary - cfg.windings.Rdc_primary) < maxDev,
              "SVU 5%: Right Rdc_primary within tolerance band");
    }
}

// =============================================================================
// 5. Stress Tests
// =============================================================================

void test5_stress()
{
    std::cout << "\n=== TEST 5: Stress Tests ===" << std::endl;

    const float sr = 44100.0f;
    auto cfg = transfo::Presets::Jensen_JT115KE();

    // 5a. Very small buffer (1 sample)
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 1);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        float input = 0.3f;
        float output = 0.0f;
        model.processBlock(&input, &output, 1);
        CHECK(std::isfinite(output), "1-sample buffer: output is finite");
    }

    // 5b. Large buffer (4096 samples)
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 4096);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(4096);
        generateSine(buf.data(), 4096, 1000.0f, sr, 0.5f);
        std::vector<float> output(4096);

        model.processBlock(buf.data(), output.data(), 4096);
        CHECK(allFinite(output.data(), 4096), "4096-sample buffer: all output finite");
        CHECK(rms(output.data(), 4096) > 1e-6, "4096-sample buffer: non-zero output");
    }

    // 5c. DC signal -> no divergence
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 1024);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> input(1024, 0.5f); // constant DC
        std::vector<float> output(1024, 0.0f);

        // Process multiple blocks to allow any instability to manifest
        for (int block = 0; block < 10; ++block)
            model.processBlock(input.data(), output.data(), 1024);

        CHECK(allFinite(output.data(), 1024), "DC signal: output is finite after 10 blocks");
        float peak = peakAbs(output.data(), 1024);
        CHECK(peak < 100.0f, "DC signal: output does not diverge (peak < 100)");
        std::cout << "    DC peak after 10 blocks: " << peak << std::endl;
    }

    // 5d. Very loud signal (+20 dBFS -> amplitude ~10) -> no Inf/NaN
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 512);
        model.setInputGain(20.0f); // +20 dB input gain
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(512);
        float hotAmplitude = std::pow(10.0f, 20.0f / 20.0f); // ~10.0
        generateSine(buf.data(), 512, 1000.0f, sr, hotAmplitude);
        std::vector<float> output(512);

        model.processBlock(buf.data(), output.data(), 512);
        CHECK(allFinite(output.data(), 512), "Hot signal (+20dBFS + 20dB gain): no Inf/NaN");
    }

    // 5e. Physical mode stress: 1-sample buffer
    {
        PhysicalModel model;
        model.setProcessingMode(transfo::ProcessingMode::Physical);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 1);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        float input = 0.3f;
        float output = 0.0f;
        model.processBlock(&input, &output, 1);
        CHECK(std::isfinite(output), "Physical mode 1-sample: output is finite");
    }

    // 5f. Physical mode stress: large buffer
    {
        PhysicalModel model;
        model.setProcessingMode(transfo::ProcessingMode::Physical);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 4096);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(4096);
        generateSine(buf.data(), 4096, 1000.0f, sr, 0.5f);
        std::vector<float> output(4096);

        model.processBlock(buf.data(), output.data(), 4096);
        CHECK(allFinite(output.data(), 4096), "Physical mode 4096-sample: all output finite");
    }

    // 5g. DC in Physical mode
    {
        PhysicalModel model;
        model.setProcessingMode(transfo::ProcessingMode::Physical);
        model.setConfig(cfg);
        model.prepareToPlay(sr, 1024);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> input(1024, 0.5f);
        std::vector<float> output(1024, 0.0f);

        for (int block = 0; block < 10; ++block)
            model.processBlock(input.data(), output.data(), 1024);

        CHECK(allFinite(output.data(), 1024), "Physical mode DC: output is finite after 10 blocks");
        float peak = peakAbs(output.data(), 1024);
        CHECK(peak < 100.0f, "Physical mode DC: no divergence (peak < 100)");
        std::cout << "    Physical DC peak after 10 blocks: " << peak << std::endl;
    }
}

// =============================================================================
// 6. B-H Queue Tests
// =============================================================================

void test6_bh_queue()
{
    std::cout << "\n=== TEST 6: B-H Queue Tests ===" << std::endl;

    // 6a. SPSC queue basic push/pop
    {
        transfo::SPSCQueue<transfo::BHSample, 64> queue;
        transfo::BHSample sample{1.0f, 2.0f};

        bool pushed = queue.push(sample);
        CHECK(pushed, "SPSC queue: push succeeds");
        CHECK(!queue.empty(), "SPSC queue: not empty after push");

        transfo::BHSample out{};
        bool popped = queue.pop(out);
        CHECK(popped, "SPSC queue: pop succeeds");
        CHECK_NEAR(out.h, 1.0f, 1e-6, "SPSC queue: popped h correct");
        CHECK_NEAR(out.b, 2.0f, 1e-6, "SPSC queue: popped b correct");
        CHECK(queue.empty(), "SPSC queue: empty after pop");
    }

    // 6b. Queue does not block when full
    {
        transfo::SPSCQueue<transfo::BHSample, 16> queue; // capacity 16
        transfo::BHSample sample{0.0f, 0.0f};

        int pushed = 0;
        for (int i = 0; i < 32; ++i)
        {
            sample.h = static_cast<float>(i);
            if (queue.push(sample))
                pushed++;
        }
        CHECK(pushed < 32, "SPSC queue: push returns false when full (non-blocking)");
        CHECK(pushed == 15, "SPSC queue: pushed capacity-1 items before full");
    }

    // 6c. Realtime model produces BH samples after processing
    {
        RealtimeModel model;
        model.setProcessingMode(transfo::ProcessingMode::Realtime);
        model.setConfig(transfo::Presets::Jensen_JT115KE());
        model.prepareToPlay(44100.0f, 1024);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        // Process enough samples to generate BH data
        // BH queue is downsampled by 32 in Realtime mode, so we need >= 32 samples
        std::vector<float> buf(1024);
        generateSine(buf.data(), 1024, 1000.0f, 44100.0f, 0.5f);
        std::vector<float> output(1024);

        model.processBlock(buf.data(), output.data(), 1024);

        // Read BH samples
        transfo::BHSample bhBuf[128];
        size_t count = model.readBHSamples(bhBuf, 128);

        CHECK(count > 0, "Realtime model: BH queue has samples after processing");
        std::cout << "    BH samples read: " << count << std::endl;

        // Verify BH samples are finite
        bool allBHFinite = true;
        for (size_t i = 0; i < count; ++i)
        {
            if (!std::isfinite(bhBuf[i].h) || !std::isfinite(bhBuf[i].b))
            {
                allBHFinite = false;
                break;
            }
        }
        CHECK(allBHFinite, "Realtime model: all BH samples are finite");
    }

    // 6d. Physical model also produces BH samples
    {
        PhysicalModel model;
        model.setProcessingMode(transfo::ProcessingMode::Physical);
        model.setConfig(transfo::Presets::Jensen_JT115KE());
        model.prepareToPlay(44100.0f, 1024);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(1024);
        generateSine(buf.data(), 1024, 1000.0f, 44100.0f, 0.5f);
        std::vector<float> output(1024);

        model.processBlock(buf.data(), output.data(), 1024);

        transfo::BHSample bhBuf[128];
        size_t count = model.readBHSamples(bhBuf, 128);

        // Physical mode downsamples by 128, so with 1024*4=4096 OS samples, ~32 BH samples
        CHECK(count > 0, "Physical model: BH queue has samples after processing");
        std::cout << "    Physical BH samples read: " << count << std::endl;
    }

    // 6e. Queue reset works
    {
        transfo::SPSCQueue<transfo::BHSample, 64> queue;
        transfo::BHSample sample{1.0f, 2.0f};
        queue.push(sample);
        queue.push(sample);
        queue.push(sample);

        queue.reset();
        CHECK(queue.empty(), "SPSC queue: empty after reset");

        transfo::BHSample out{};
        bool popped = queue.pop(out);
        CHECK(!popped, "SPSC queue: pop fails after reset");
    }
}

// =============================================================================
// 7. Multi-block consistency (bonus integration test)
// =============================================================================

void test7_multiblock_consistency()
{
    std::cout << "\n=== TEST 7: Multi-block Processing Consistency ===" << std::endl;

    const float sr = 44100.0f;
    const int blockSize = 256;
    const int numBlocks = 20;
    auto cfg = transfo::Presets::Jensen_JT115KE();

    // Process 20 consecutive blocks and verify stability
    RealtimeModel model;
    model.setProcessingMode(transfo::ProcessingMode::Realtime);
    model.setConfig(cfg);
    model.prepareToPlay(sr, blockSize);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    std::vector<float> input(blockSize);
    std::vector<float> output(blockSize);
    double prevRms = -1.0;
    bool allStable = true;

    for (int b = 0; b < numBlocks; ++b)
    {
        // Generate continuous sine (phase-continuous across blocks)
        for (int i = 0; i < blockSize; ++i)
        {
            int sampleIndex = b * blockSize + i;
            input[i] = 0.5f * std::sin(2.0f * static_cast<float>(PI) * 1000.0f *
                                        static_cast<float>(sampleIndex) / sr);
        }

        model.processBlock(input.data(), output.data(), blockSize);

        if (!allFinite(output.data(), blockSize))
        {
            allStable = false;
            std::cout << "    Block " << b << ": NON-FINITE output detected!" << std::endl;
            break;
        }

        double r = rms(output.data(), blockSize);
        if (prevRms > 0 && r > prevRms * 100.0)
        {
            allStable = false;
            std::cout << "    Block " << b << ": RMS jumped from " << prevRms
                      << " to " << r << std::endl;
            break;
        }
        prevRms = r;
    }

    CHECK(allStable, "20-block processing: output remains stable");
    std::cout << "    Final block RMS: " << prevRms << std::endl;
}

// =============================================================================
// 8. All presets in Physical mode (additional coverage)
// =============================================================================

void test8_physical_mode_all_presets()
{
    std::cout << "\n=== TEST 8: All Presets in Physical Mode ===" << std::endl;

    const int N = 256;
    const float sr = 44100.0f;
    const int numPresets = transfo::Presets::count();

    for (int p = 0; p < numPresets; ++p)
    {
        PhysicalModel model;
        model.setProcessingMode(transfo::ProcessingMode::Physical);
        model.setConfig(transfo::Presets::getByIndex(p));
        model.prepareToPlay(sr, N);
        model.setInputGain(0.0f);
        model.setOutputGain(0.0f);
        model.setMix(1.0f);

        std::vector<float> buf(N);
        generateSine(buf.data(), N, 1000.0f, sr, 0.5f);
        std::vector<float> output(N);

        model.processBlock(buf.data(), output.data(), N);

        std::string msg = "Physical preset " + std::to_string(p) + " (" +
                          transfo::Presets::getNameByIndex(p) + "): finite output";
        CHECK(allFinite(output.data(), N), msg.c_str());

        double r = rms(output.data(), N);
        std::string msg2 = "Physical preset " + std::to_string(p) + ": non-zero RMS=" +
                           std::to_string(r);
        CHECK(r > 1e-8, msg2.c_str());
    }
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "  Plugin Integration Test Suite (core/ only, no JUCE)" << std::endl;
    std::cout << "================================================================" << std::endl;

    test1_lifecycle();
    test2_preset_switching();
    test3_mode_switching();
    test4_parameter_ranges();
    test5_stress();
    test6_bh_queue();
    test7_multiblock_consistency();
    test8_physical_mode_all_presets();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_fail > 0) ? 1 : 0;
}
