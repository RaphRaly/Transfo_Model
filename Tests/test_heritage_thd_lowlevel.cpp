// =============================================================================
// Test: Heritage THD vs Gain at Low Level (-60 dBFS)
//
// Sprint C.1: At very low input levels, the T1 input transformer does NOT
// saturate, so THD should increase with gain (preamp nonlinearity visible).
// At higher levels (0.01 peak), T1 saturates and dominates THD → constant.
//
// This test uses kAmplitude = 0.001 (-60 dBFS) to stay below T1 saturation.
// =============================================================================

#include "test_common.h"
#include "../core/include/core/preamp/PreampModel.h"
#include "../core/include/core/model/PreampConfig.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/preamp/GainTable.h"
#include <vector>
#include <cmath>
#include <cstdio>

using namespace transfo;

static constexpr float  kSampleRate = 44100.0f;
static constexpr int    kMaxBlock   = 512;
static constexpr int    kWarmup     = 4096;
static constexpr int    kMeasure    = 8192;
static constexpr double kFreq       = 1000.0;
static constexpr float  kAmplitude  = 0.001f;  // -60 dBFS — below T1 saturation

static void generateSine(float* buf, int n, double freq, float amp, float sr, int off = 0)
{
    for (int i = 0; i < n; ++i)
    {
        double t = static_cast<double>(i + off) / static_cast<double>(sr);
        buf[i] = amp * static_cast<float>(std::sin(2.0 * test::PI * freq * t));
    }
}

static double measureTHD(int path, int gainPos)
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

    // Warmup
    std::vector<float> wIn(kWarmup), wOut(kWarmup);
    generateSine(wIn.data(), kWarmup, kFreq, kAmplitude, kSampleRate);
    for (int off = 0; off < kWarmup; off += kMaxBlock)
    {
        int chunk = std::min(kMaxBlock, kWarmup - off);
        model->processBlock(wIn.data() + off, wOut.data() + off, chunk);
    }

    // Measure
    std::vector<float> input(kMeasure), output(kMeasure);
    generateSine(input.data(), kMeasure, kFreq, kAmplitude, kSampleRate, kWarmup);
    for (int off = 0; off < kMeasure; off += kMaxBlock)
    {
        int chunk = std::min(kMaxBlock, kMeasure - off);
        model->processBlock(input.data() + off, output.data() + off, chunk);
    }

    return test::computeTHD(output.data(), kMeasure, kFreq, kSampleRate, 8);
}

static void test_heritage_thd_vs_gain_lowlevel()
{
    std::printf("\n--- Heritage THD vs Gain at -60 dBFS ---\n");
    std::printf("    %-10s %-12s %-12s\n", "Position", "Gain (dB)", "THD (%)");
    std::printf("    %-10s %-12s %-12s\n", "--------", "---------", "-------");

    const int positions[] = { 0, 3, 5, 7, 10 };
    double thdValues[5];

    for (int p = 0; p < 5; ++p)
    {
        int pos = positions[p];
        thdValues[p] = measureTHD(0, pos);
        std::printf("    %-10d %-12.1f %-12.6f\n",
                    pos, static_cast<double>(GainTable::getGainDB(pos)),
                    thdValues[p]);
    }

    // At low level, THD should generally increase with gain
    // (preamp distortion visible, T1 not saturating)
    CHECK(thdValues[4] > thdValues[0],
          "Heritage: THD at gain 10 > THD at gain 0 (low-level, non-saturated)");

    // THD should NOT be constant (unlike the 0.01 peak case where T1 dominates)
    double range = thdValues[4] - thdValues[0];
    std::printf("\n    THD range (pos 0 to 10): %.6f%%\n", range);
    std::printf("    NOTE: If range > 0.1%%, preamp distortion is visible at -60 dBFS\n");

    CHECK(range > 0.0001,
          "Heritage: THD varies with gain at low level (T1 not dominating)");
}

static void test_modern_thd_vs_gain_lowlevel()
{
    std::printf("\n--- Modern THD vs Gain at -60 dBFS ---\n");
    std::printf("    %-10s %-12s %-12s\n", "Position", "Gain (dB)", "THD (%)");
    std::printf("    %-10s %-12s %-12s\n", "--------", "---------", "-------");

    const int positions[] = { 0, 3, 5, 7, 10 };
    double thdValues[5];

    for (int p = 0; p < 5; ++p)
    {
        int pos = positions[p];
        thdValues[p] = measureTHD(1, pos);
        std::printf("    %-10d %-12.1f %-12.6f\n",
                    pos, static_cast<double>(GainTable::getGainDB(pos)),
                    thdValues[p]);
    }

    // Modern (JE-990) THD at low level is non-monotonic due to the feedback
    // loop + ClassAB crossover interaction. The Newton solver and loop gain
    // vary with signal level, creating complex THD vs gain behavior.
    // We only check that THD is non-zero (the path is producing distortion).
    CHECK(thdValues[0] > 0.001,
          "Modern: measurable THD at gain 0 (low-level signal passes through)");
    CHECK(thdValues[4] > 0.001,
          "Modern: measurable THD at gain 10 (low-level signal passes through)");
}

int main()
{
    std::printf("================================================================\n");
    std::printf("  Heritage/Modern THD vs Gain — Low Level (-60 dBFS)\n");
    std::printf("  Sprint C.1: Below T1 saturation threshold\n");
    std::printf("================================================================\n");

    test_heritage_thd_vs_gain_lowlevel();
    test_modern_thd_vs_gain_lowlevel();

    std::printf("\n");
    test::printSummary("test_heritage_thd_lowlevel");
    return (test::g_fail() > 0) ? 1 : 0;
}
