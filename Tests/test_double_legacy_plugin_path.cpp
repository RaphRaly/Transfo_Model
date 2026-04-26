// =============================================================================
// test_double_legacy_plugin_path.cpp
//
// Stress harness for the plugin's Double Legacy engine:
//   JT-115K-E -> JT-11ELCF in series
//
// Mirrors the plugin branch closely:
//   - fixed transformer pair
//   - realtime (CPWL) and physical (J-A) modes
//   - input/output gain
//   - dry/wet mix
//   - live T2 load switching
//   - variable block sizes, including size 1
//
// Primary goal: catch unstable outputs early.
// Assertions focus on:
//   - no NaN / Inf
//   - bounded peak amplitude
//   - no absurd per-sample jumps
// =============================================================================

#include "test_common.h"
#include <core/model/TransformerConfig.h>
#include <core/model/TransformerModel.h>
#include <core/magnetics/AnhystereticFunctions.h>
#include <core/magnetics/CPWLLeaf.h>
#include <core/magnetics/JilesAthertonLeaf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace transfo;

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr int kMaxBlockSize = 512;
constexpr float kPeakLimit = 64.0f;
constexpr float kJumpLimit = 32.0f;

float getT2LoadOhms(int t2LoadIndex)
{
    if (t2LoadIndex == 0)
        return 600.0f;
    if (t2LoadIndex == 2)
        return 47000.0f;
    return 10000.0f;
}

template <typename Leaf>
struct DoubleLegacyChain
{
    TransformerModel<Leaf> input;
    TransformerModel<Leaf> output;
    std::vector<float> dry;
    std::vector<float> mid;
    ProcessingMode mode = ProcessingMode::Realtime;

    void prepare(float sampleRate, int maxBlockSize, ProcessingMode newMode, int t2LoadIndex)
    {
        mode = newMode;

        auto inputCfg = TransformerConfig::Jensen_JT115KE();
        auto outputCfg = TransformerConfig::Jensen_JT11ELCF();
        outputCfg.loadImpedance = getT2LoadOhms(t2LoadIndex);

        input.setConfig(inputCfg);
        output.setConfig(outputCfg);
        input.setProcessingMode(mode);
        output.setProcessingMode(mode);


        input.setInputGain(0.0f);
        input.setOutputGain(0.0f);
        input.setMix(1.0f);
        output.setInputGain(0.0f);
        output.setOutputGain(0.0f);
        output.setMix(1.0f);

        if (mode == ProcessingMode::Physical)
        {
            input.setOversamplingFactor(4);
            output.setOversamplingFactor(4);
        }

        input.prepareToPlay(sampleRate, maxBlockSize);
        output.prepareToPlay(sampleRate, maxBlockSize);
        dry.assign(static_cast<size_t>(maxBlockSize), 0.0f);
        mid.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    }

    void switchLoad(int t2LoadIndex)
    {
        auto outputCfg = TransformerConfig::Jensen_JT11ELCF();
        outputCfg.loadImpedance = getT2LoadOhms(t2LoadIndex);
        output.setConfig(outputCfg);
    }

    void processBlock(float* data, int numSamples, float inputGainDb, float outputGainDb, float mix)
    {
        const float inputGainLin  = std::pow(10.0f, inputGainDb / 20.0f);
        const float outputGainLin = std::pow(10.0f, outputGainDb / 20.0f);

        std::copy(data, data + numSamples, dry.data());

        for (int i = 0; i < numSamples; ++i)
            data[i] *= inputGainLin;

        input.processBlock(data, mid.data(), numSamples);
        output.processBlock(mid.data(), data, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            data[i] = mix * (data[i] * outputGainLin)
                    + (1.0f - mix) * dry[static_cast<size_t>(i)];

            if (!std::isfinite(data[i]) || std::abs(data[i]) < 1e-15f)
                data[i] = 0.0f;
        }
    }
};

template <typename Leaf>
void processStream(DoubleLegacyChain<Leaf>& chain, float* data, int numSamples,
                   float inputGainDb, float outputGainDb, float mix)
{
    int offset = 0;
    int remaining = numSamples;
    while (remaining > 0)
    {
        const int block = std::min(remaining, kMaxBlockSize);
        chain.processBlock(data + offset, block, inputGainDb, outputGainDb, mix);
        offset += block;
        remaining -= block;
    }
}

struct StressStats
{
    float peak = 0.0f;
    float maxJump = 0.0f;
    float lastSample = 0.0f;
    bool hasLast = false;
    bool allFinite = true;
};

template <typename Leaf>
StressStats runStressScenario(ProcessingMode mode)
{
    DoubleLegacyChain<Leaf> chain;
    chain.prepare(kSampleRate, kMaxBlockSize, mode, 1);

    const int blockSizes[] = {1, 3, 7, 16, 31, 64, 127, 255, 511};
    const int numBlocks = 240;
    const float baseFreqs[] = {37.0f, 83.0f, 173.0f, 997.0f, 3217.0f};

    std::vector<float> block(static_cast<size_t>(kMaxBlockSize), 0.0f);
    StressStats stats;
    int globalSample = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        const int blockSize = blockSizes[b % static_cast<int>(std::size(blockSizes))];
        const float freq = baseFreqs[b % static_cast<int>(std::size(baseFreqs))];
        const float inputGainDb = -24.0f + 44.0f * (static_cast<float>(b % 17) / 16.0f);
        const float outputGainDb = -18.0f + 30.0f * (static_cast<float>(b % 11) / 10.0f);
        const float mixValues[] = {1.0f, 0.75f, 0.5f, 0.25f};
        const float mix = mixValues[b % static_cast<int>(std::size(mixValues))];
        const float drive = (mode == ProcessingMode::Realtime) ? 1.25f : 0.85f;

        if ((b % 40) == 0)
            chain.switchLoad((b / 40) % 3);

        for (int i = 0; i < blockSize; ++i)
        {
            const float t = static_cast<float>(globalSample + i) / kSampleRate;
            const float env = 0.08f + 0.92f * std::abs(std::sin(2.0f * 3.14159265f * 0.37f * t));
            const float toneA = std::sin(2.0f * 3.14159265f * freq * t);
            const float toneB = 0.35f * std::sin(2.0f * 3.14159265f * (freq * 2.71f) * t);
            const float dcBias = ((b % 9) == 0) ? 0.03f : 0.0f;
            block[static_cast<size_t>(i)] = drive * env * (0.72f * toneA + toneB) + dcBias;
        }

        chain.processBlock(block.data(), blockSize, inputGainDb, outputGainDb, mix);

        for (int i = 0; i < blockSize; ++i)
        {
            const float s = block[static_cast<size_t>(i)];
            if (!std::isfinite(s))
            {
                stats.allFinite = false;
                continue;
            }

            stats.peak = std::max(stats.peak, std::abs(s));
            if (stats.hasLast)
                stats.maxJump = std::max(stats.maxJump, std::abs(s - stats.lastSample));
            stats.lastSample = s;
            stats.hasLast = true;
        }

        globalSample += blockSize;
    }

    return stats;
}

template <typename Leaf>
void testLoadSensitivity(ProcessingMode mode, const char* label)
{
    DoubleLegacyChain<Leaf> chainA;
    DoubleLegacyChain<Leaf> chainB;
    chainA.prepare(kSampleRate, kMaxBlockSize, mode, 0);
    chainB.prepare(kSampleRate, kMaxBlockSize, mode, 2);

    constexpr int warmupSamples = 8192;
    constexpr int measureSamples = 8192;
    std::vector<float> input(static_cast<size_t>(warmupSamples + measureSamples), 0.0f);
    std::vector<float> outA = input;
    std::vector<float> outB = input;

    for (int i = 0; i < static_cast<int>(input.size()); ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        input[static_cast<size_t>(i)] =
            0.22f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
    }

    processStream(chainA, input.data(), warmupSamples, 0.0f, 0.0f, 1.0f);
    processStream(chainB, input.data(), warmupSamples, 0.0f, 0.0f, 1.0f);

    std::copy(input.begin() + warmupSamples, input.end(), outA.begin() + warmupSamples);
    std::copy(input.begin() + warmupSamples, input.end(), outB.begin() + warmupSamples);
    processStream(chainA, outA.data() + warmupSamples, measureSamples, 0.0f, 0.0f, 1.0f);
    processStream(chainB, outB.data() + warmupSamples, measureSamples, 0.0f, 0.0f, 1.0f);

    const double rmsA = test::computeRMS(outA.data() + warmupSamples, measureSamples);
    const double rmsB = test::computeRMS(outB.data() + warmupSamples, measureSamples);
    const double relDiff = std::abs(rmsA - rmsB) / std::max({rmsA, rmsB, 1e-12});

    std::printf("  %s load sensitivity: RMS600=%.6f RMS47k=%.6f relDiff=%.4f\n",
                label, rmsA, rmsB, relDiff);
    CHECK(relDiff > 1e-4, "T2 load changes should reach Double Legacy output");
}

void testRealtimeStress()
{
    std::printf("\n=== Double Legacy: Realtime Stress ===\n");
    auto stats = runStressScenario<CPWLLeaf>(ProcessingMode::Realtime);
    std::printf("  Peak=%.4f  MaxJump=%.4f  Finite=%s\n",
                stats.peak, stats.maxJump, stats.allFinite ? "yes" : "no");

    CHECK(stats.allFinite, "Realtime Double Legacy stays finite");
    CHECK(stats.peak < kPeakLimit, "Realtime Double Legacy peak stays bounded");
    CHECK(stats.maxJump < kJumpLimit, "Realtime Double Legacy has no absurd sample jump");
}

void testPhysicalStress()
{
    std::printf("\n=== Double Legacy: Physical Stress ===\n");
    auto stats = runStressScenario<JilesAthertonLeaf<LangevinPade>>(ProcessingMode::Physical);
    std::printf("  Peak=%.4f  MaxJump=%.4f  Finite=%s\n",
                stats.peak, stats.maxJump, stats.allFinite ? "yes" : "no");

    CHECK(stats.allFinite, "Physical Double Legacy stays finite");
    CHECK(stats.peak < kPeakLimit, "Physical Double Legacy peak stays bounded");
    CHECK(stats.maxJump < kJumpLimit, "Physical Double Legacy has no absurd sample jump");
}

void testRealtimeLoadSensitivity()
{
    std::printf("\n=== Double Legacy: Realtime T2 Load Wire-Up ===\n");
    testLoadSensitivity<CPWLLeaf>(ProcessingMode::Realtime, "Realtime");
}

void testPhysicalLoadSensitivity()
{
    std::printf("\n=== Double Legacy: Physical T2 Load Wire-Up ===\n");
    testLoadSensitivity<JilesAthertonLeaf<LangevinPade>>(ProcessingMode::Physical, "Physical");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Double Legacy Plugin Path Stress Test\n");
    std::printf("(JT-115K-E -> JT-11ELCF, plugin-style gain/mix/load path)\n");
    std::printf("=========================================================\n");

    testRealtimeStress();
    testPhysicalStress();
    testRealtimeLoadSensitivity();
    testPhysicalLoadSensitivity();

    return test::printSummary("Double Legacy Plugin Path");
}
