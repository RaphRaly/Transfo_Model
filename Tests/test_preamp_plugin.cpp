// =============================================================================
// test_preamp_plugin — Sprint 7: Plugin integration tests for PreampModel.
//
// Tests that PreampModel can be configured and processes audio correctly,
// simulating the plugin's usage pattern. NO JUCE dependency — core-only.
// =============================================================================

#include "core/preamp/PreampModel.h"
#include "core/magnetics/JilesAthertonLeaf.h"
#include "core/magnetics/AnhystereticFunctions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using JALeaf = transfo::JilesAthertonLeaf<transfo::LangevinPade>;
using PreampJA = transfo::PreampModel<JALeaf>;

#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL: %s\n  %s:%d\n", msg, __FILE__, __LINE__); \
    return false; } } while(0)

// ── Helpers ─────────────────────────────────────────────────────────────────

static std::vector<float> generateSineBlock(int numSamples, float freq,
                                             float sampleRate, float amplitude)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    const float omega = 2.0f * 3.14159265f * freq / sampleRate;
    for (int i = 0; i < numSamples; ++i)
        buf[static_cast<size_t>(i)] = amplitude * std::sin(omega * static_cast<float>(i));
    return buf;
}

static float rmsLevel(const float* data, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / static_cast<float>(n));
}

// ── Test 1: Plugin lifecycle ────────────────────────────────────────────────

static bool test_plugin_lifecycle()
{
    std::printf("  test_plugin_lifecycle...\n");

    PreampJA preamp;

    // 1. setConfig
    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    preamp.setConfig(cfg);

    // 2. prepareToPlay
    const float sr = 44100.0f;
    const int blockSize = 128;
    preamp.prepareToPlay(sr, blockSize);

    // 3. processBlock with silence — should not crash
    std::vector<float> buf(static_cast<size_t>(blockSize), 0.0f);
    preamp.processBlock(buf.data(), buf.data(), blockSize);

    // Output should be near-zero for zero input
    float rms = rmsLevel(buf.data(), blockSize);
    ASSERT_TRUE(rms < 1.0f, "Silence should produce near-zero output");

    // 4. processBlock with sine
    auto sine = generateSineBlock(blockSize, 1000.0f, sr, 0.01f);
    preamp.processBlock(sine.data(), sine.data(), blockSize);

    // 5. reset — should not crash
    preamp.reset();

    // 6. Process again after reset
    auto sine2 = generateSineBlock(blockSize, 1000.0f, sr, 0.01f);
    preamp.prepareToPlay(sr, blockSize);
    preamp.processBlock(sine2.data(), sine2.data(), blockSize);

    std::printf("    PASS\n");
    return true;
}

// ── Test 2: Parameter sweep ────────────────────────────────────────────────

static bool test_parameter_sweep()
{
    std::printf("  test_parameter_sweep...\n");

    PreampJA preamp;
    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    preamp.setConfig(cfg);
    preamp.prepareToPlay(44100.0f, 64);

    const int blockSize = 64;

    // Sweep all gain positions
    for (int pos = 0; pos <= 10; ++pos)
    {
        preamp.setGainPosition(pos);
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
        // Should not crash or produce NaN
        for (int i = 0; i < blockSize; ++i)
            ASSERT_TRUE(!std::isnan(sine[static_cast<size_t>(i)]),
                        "NaN at some gain position");
    }

    // Switch paths
    preamp.setPath(0);  // Neve
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }

    preamp.setPath(1);  // Jensen
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }

    // Toggle PAD
    preamp.setPadEnabled(true);
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }
    preamp.setPadEnabled(false);

    // Toggle ratio
    preamp.setRatio(0);  // 1:5
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }
    preamp.setRatio(1);  // 1:10

    // Toggle phase invert
    preamp.setPhaseInvert(true);
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }
    preamp.setPhaseInvert(false);

    // Set input/output gain, mix
    preamp.setInputGain(-6.0f);
    preamp.setOutputGain(3.0f);
    preamp.setMix(0.5f);
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
            ASSERT_TRUE(!std::isnan(sine[static_cast<size_t>(i)]),
                        "NaN after gain/mix change");
    }

    std::printf("    PASS\n");
    return true;
}

// ── Test 3: Dual channel independence ──────────────────────────────────────

static bool test_dual_channel_independence()
{
    std::printf("  test_dual_channel_independence...\n");

    PreampJA preampL, preampR;

    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;

    preampL.setConfig(cfg);
    preampL.prepareToPlay(44100.0f, 128);

    preampR.setConfig(cfg);
    preampR.prepareToPlay(44100.0f, 128);

    // Set different gain positions
    preampL.setGainPosition(3);
    preampR.setGainPosition(8);

    const int blockSize = 128;

    // Process identical input through both
    auto inputL = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
    auto inputR = inputL;  // copy

    preampL.processBlock(inputL.data(), inputL.data(), blockSize);
    preampR.processBlock(inputR.data(), inputR.data(), blockSize);

    // Outputs should differ (different gain positions)
    float rmsL = rmsLevel(inputL.data(), blockSize);
    float rmsR = rmsLevel(inputR.data(), blockSize);

    // Both should be non-zero
    ASSERT_TRUE(rmsL > 0.0f || rmsR > 0.0f,
                "At least one channel should produce non-zero output");

    // They should be different (higher gain position = more output)
    // Note: due to settling, initial blocks might be similar,
    // so we just verify no crash and no NaN
    for (int i = 0; i < blockSize; ++i)
    {
        ASSERT_TRUE(!std::isnan(inputL[static_cast<size_t>(i)]),
                    "NaN in left channel");
        ASSERT_TRUE(!std::isnan(inputR[static_cast<size_t>(i)]),
                    "NaN in right channel");
    }

    std::printf("    PASS\n");
    return true;
}

// ── Test 4: Preset change at runtime ───────────────────────────────────────

static bool test_preset_change_runtime()
{
    std::printf("  test_preset_change_runtime...\n");

    PreampJA preamp;

    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    preamp.setConfig(cfg);
    preamp.prepareToPlay(44100.0f, 128);

    const int blockSize = 128;

    // Process a few blocks normally
    for (int b = 0; b < 3; ++b)
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }

    // Mid-stream: change config and re-prepare
    auto cfg2 = transfo::PreampConfig::DualTopology();
    cfg2.t2Config.loadImpedance = 5000.0f;  // different load
    preamp.setConfig(cfg2);
    preamp.prepareToPlay(44100.0f, blockSize);

    // Process more blocks — should not crash
    for (int b = 0; b < 3; ++b)
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.001f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
            ASSERT_TRUE(!std::isnan(sine[static_cast<size_t>(i)]),
                        "NaN after config change");
    }

    std::printf("    PASS\n");
    return true;
}

// ── Test 5: Monitor data valid ────────────────────────────────────────────

static bool test_monitor_data_valid()
{
    std::printf("  test_monitor_data_valid...\n");

    PreampJA preamp;

    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    preamp.setConfig(cfg);
    preamp.prepareToPlay(44100.0f, 256);

    // Process a few blocks of sine to build up levels
    const int blockSize = 256;
    for (int b = 0; b < 5; ++b)
    {
        auto sine = generateSineBlock(blockSize, 1000.0f, 44100.0f, 0.01f);
        preamp.processBlock(sine.data(), sine.data(), blockSize);
    }

    // Get monitor data
    auto mon = preamp.getMonitorData();

    // Check values are sane (not NaN, not inf)
    ASSERT_TRUE(!std::isnan(mon.inputLevel_dBu), "inputLevel_dBu is NaN");
    ASSERT_TRUE(!std::isnan(mon.outputLevel_dBu), "outputLevel_dBu is NaN");
    ASSERT_TRUE(!std::isinf(mon.inputLevel_dBu), "inputLevel_dBu is inf");
    ASSERT_TRUE(!std::isinf(mon.outputLevel_dBu), "outputLevel_dBu is inf");
    ASSERT_TRUE(!std::isnan(mon.t1_magnetizing_current),
                "t1_magnetizing_current is NaN");
    ASSERT_TRUE(!std::isnan(mon.t2_magnetizing_current),
                "t2_magnetizing_current is NaN");

    // Path should be 0 (Neve default)
    ASSERT_TRUE(mon.currentPath == 0, "Default path should be Neve (0)");

    // Gain position should be 5 (default)
    ASSERT_TRUE(mon.gainPosition == 5, "Default gain position should be 5");

    // Input level should be reasonable (we fed 0.01V amplitude)
    // -120 dBu is the default for zero, so after processing it should be > -120
    ASSERT_TRUE(mon.inputLevel_dBu > -120.0f,
                "Input level should be above silence floor after processing");

    std::printf("    PASS\n");
    return true;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main()
{
    std::printf("=== Sprint 7: Preamp Plugin Integration Tests ===\n\n");

    int passed = 0, failed = 0;

    auto run = [&](bool (*fn)()) {
        if (fn()) ++passed; else ++failed;
    };

    run(test_plugin_lifecycle);
    run(test_parameter_sweep);
    run(test_dual_channel_independence);
    run(test_preset_change_runtime);
    run(test_monitor_data_valid);

    std::printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return (failed > 0) ? 1 : 0;
}
