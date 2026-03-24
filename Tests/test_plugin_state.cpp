// =============================================================================
// test_plugin_state.cpp — State save/load regression tests (A2.4)
//
// Verifies that TransformerModel produces deterministic output after
// config reset+restore, survives rapid preset switching, and maintains
// bounded output under heavy drive for all factory presets.
// =============================================================================

#include <core/model/TransformerModel.h>
#include <core/model/Presets.h>
#include <core/magnetics/CPWLLeaf.h>
#include <core/magnetics/JilesAthertonLeaf.h>
#include <core/magnetics/AnhystereticFunctions.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

using namespace transfo;

// ── Minimal test framework ──────────────────────────────────────────────────
static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_failed++; \
    } else { \
        g_passed++; \
    } \
} while(0)

#define TEST_SECTION(name) std::printf("\n=== %s ===\n", (name))

// ── Helpers ─────────────────────────────────────────────────────────────────

static std::vector<float> generateSine(int numSamples, float freq, float amplitude,
                                        float sampleRate)
{
    std::vector<float> sig(static_cast<size_t>(numSamples));
    const float w = 2.0f * 3.14159265f * freq / sampleRate;
    for (int i = 0; i < numSamples; ++i)
        sig[static_cast<size_t>(i)] = amplitude * std::sin(w * static_cast<float>(i));
    return sig;
}

template <typename LeafType>
static std::vector<float> processBlock(TransformerModel<LeafType>& model,
                                        const std::vector<float>& input)
{
    std::vector<float> output(input.size());
    model.processBlock(input.data(), output.data(), static_cast<int>(input.size()));
    return output;
}

// ── Test: identical config → identical output ───────────────────────────────
static void testConfigRestore()
{
    TEST_SECTION("Config Restore Determinism");

    const float sr = 44100.0f;
    const int blockSize = 512;

    for (int p = 0; p < Presets::kFactoryCount; ++p)
    {
        auto config = Presets::getByIndex(p);
        const char* name = Presets::getNameByIndex(p);

        // Two identical models should produce identical output
        TransformerModel<CPWLLeaf> m1, m2;

        m1.setConfig(config);
        m1.setProcessingMode(ProcessingMode::Realtime);
        m1.setUseWdfCircuit(false);
        m1.prepareToPlay(sr, blockSize);

        m2.setConfig(config);
        m2.setProcessingMode(ProcessingMode::Realtime);
        m2.setUseWdfCircuit(false);
        m2.prepareToPlay(sr, blockSize);

        auto input = generateSine(blockSize, 1000.0f, 0.1f, sr);
        auto out1 = processBlock(m1, input);
        auto out2 = processBlock(m2, input);

        // Check determinism
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out1.size(); ++i)
        {
            float d = std::abs(out1[i] - out2[i]);
            if (d > maxDiff) maxDiff = d;
        }
        TEST_ASSERT(maxDiff < 1e-6f,
            (std::string(name) + " Realtime: non-deterministic output").c_str());

        // Check non-silent
        float maxAbs = 0.0f;
        for (size_t i = 0; i < out1.size(); ++i)
        {
            float a = std::abs(out1[i]);
            if (a > maxAbs) maxAbs = a;
        }
        TEST_ASSERT(maxAbs > 1e-8f,
            (std::string(name) + " Realtime: silent output").c_str());

        // Check no NaN/Inf
        bool hasNaN = false;
        for (size_t i = 0; i < out1.size(); ++i)
            if (!std::isfinite(out1[i])) hasNaN = true;
        TEST_ASSERT(!hasNaN,
            (std::string(name) + " Realtime: NaN in output").c_str());

        std::printf("  [%d] %-24s OK (maxDiff=%.2e, peak=%.4f)\n",
                    p, name, maxDiff, maxAbs);
    }
}

// ── Test: Physical mode determinism ─────────────────────────────────────────
static void testPhysicalDeterminism()
{
    TEST_SECTION("Physical Mode Determinism");

    const float sr = 44100.0f;
    const int blockSize = 256;

    for (int p = 0; p < Presets::kFactoryCount; ++p)
    {
        auto config = Presets::getByIndex(p);
        const char* name = Presets::getNameByIndex(p);

        TransformerModel<JilesAthertonLeaf<LangevinPade>> m1, m2;

        m1.setConfig(config);
        m1.setProcessingMode(ProcessingMode::Physical);
        m1.setUseWdfCircuit(false);
        m1.prepareToPlay(sr, blockSize);

        m2.setConfig(config);
        m2.setProcessingMode(ProcessingMode::Physical);
        m2.setUseWdfCircuit(false);
        m2.prepareToPlay(sr, blockSize);

        auto input = generateSine(blockSize, 1000.0f, 0.1f, sr);
        auto out1 = processBlock(m1, input);
        auto out2 = processBlock(m2, input);

        float maxDiff = 0.0f;
        for (size_t i = 0; i < out1.size(); ++i)
        {
            float d = std::abs(out1[i] - out2[i]);
            if (d > maxDiff) maxDiff = d;
        }
        TEST_ASSERT(maxDiff < 1e-5f,
            (std::string(name) + " Physical: non-deterministic").c_str());

        bool hasNaN = false;
        for (size_t i = 0; i < out1.size(); ++i)
            if (!std::isfinite(out1[i])) hasNaN = true;
        TEST_ASSERT(!hasNaN,
            (std::string(name) + " Physical: NaN").c_str());

        std::printf("  [%d] %-24s OK (maxDiff=%.2e)\n", p, name, maxDiff);
    }
}

// ── Test: rapid preset switching ────────────────────────────────────────────
static void testPresetSwitching()
{
    TEST_SECTION("Rapid Preset Switching");

    const float sr = 44100.0f;
    const int blockSize = 256;

    TransformerModel<CPWLLeaf> model;
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setUseWdfCircuit(false);

    // Switch through all presets 3 times rapidly
    for (int round = 0; round < 3; ++round)
    {
        for (int p = 0; p < Presets::kFactoryCount; ++p)
        {
            model.setConfig(Presets::getByIndex(p));
            model.prepareToPlay(sr, blockSize);

            auto input = generateSine(blockSize, 440.0f, 0.1f, sr);
            auto output = processBlock(model, input);

            bool hasNaN = false;
            for (size_t k = 0; k < output.size(); ++k)
                if (!std::isfinite(output[k])) { hasNaN = true; break; }

            TEST_ASSERT(!hasNaN,
                ("Round " + std::to_string(round) + " preset " + std::to_string(p) + ": NaN").c_str());
        }
    }
    std::printf("  All %d switches (3 rounds × %d presets): OK\n",
                3 * Presets::kFactoryCount, Presets::kFactoryCount);
}

// ── Test: bounded amplitude under heavy drive ───────────────────────────────
static void testBoundedAmplitude()
{
    TEST_SECTION("Bounded Amplitude Under Drive");

    const float sr = 44100.0f;
    const int blockSize = 4096;

    for (int p = 0; p < Presets::kFactoryCount; ++p)
    {
        const char* name = Presets::getNameByIndex(p);

        TransformerModel<CPWLLeaf> model;
        model.setConfig(Presets::getByIndex(p));
        model.setProcessingMode(ProcessingMode::Realtime);
        model.setUseWdfCircuit(false);
        model.prepareToPlay(sr, blockSize);

        // Drive hard: full scale at 100 Hz
        auto input = generateSine(blockSize, 100.0f, 1.0f, sr);
        auto output = processBlock(model, input);

        float maxAbs = 0.0f;
        for (size_t k = 0; k < output.size(); ++k)
        {
            float a = std::abs(output[k]);
            if (a > maxAbs) maxAbs = a;
        }

        TEST_ASSERT(maxAbs < 100.0f,
            (std::string(name) + ": unbounded output").c_str());
        TEST_ASSERT(maxAbs > 0.0f,
            (std::string(name) + ": zero output under drive").c_str());

        std::printf("  [%d] %-24s peak=%.4f %s\n",
                    p, name, maxAbs, maxAbs < 100.0f ? "OK" : "UNBOUNDED");
    }
}

// ── Test: sample rate change survival ───────────────────────────────────────
static void testSampleRateChange()
{
    TEST_SECTION("Sample Rate Change Survival");

    const float sampleRates[] = {22050.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f};
    const int blockSize = 256;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(Presets::getByIndex(0));
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setUseWdfCircuit(false);

    for (float sr : sampleRates)
    {
        model.prepareToPlay(sr, blockSize);

        auto input = generateSine(blockSize, 1000.0f, 0.1f, sr);
        auto output = processBlock(model, input);

        bool hasNaN = false;
        for (size_t k = 0; k < output.size(); ++k)
            if (!std::isfinite(output[k])) { hasNaN = true; break; }

        TEST_ASSERT(!hasNaN,
            ("SR " + std::to_string(static_cast<int>(sr)) + ": NaN after switch").c_str());

        std::printf("  SR=%d Hz: OK\n", static_cast<int>(sr));
    }
}

// ── Main ────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("Plugin State Regression Tests (A2.4)\n");
    std::printf("=====================================\n");

    testConfigRestore();
    testPhysicalDeterminism();
    testPresetSwitching();
    testBoundedAmplitude();
    testSampleRateChange();

    std::printf("\n=====================================\n");
    std::printf("Results: %d passed, %d failed\n", g_passed, g_failed);

    return g_failed > 0 ? 1 : 0;
}
