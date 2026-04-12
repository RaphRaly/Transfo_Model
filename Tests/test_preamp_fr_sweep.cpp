// =============================================================================
// test_preamp_fr_sweep.cpp — Preamp FR full-chain validation (Chantier A2)
//
// Sweeps 20 Hz–20 kHz through PreampModel (Neve path, path=0) at 3 gain
// positions (min=0, mid=5, max=10), normalises to 1 kHz, asserts ±1.5 dB.
//
// Uses Goertzel (fundamental only) so harmonic distortion at high gain
// doesn't corrupt the magnitude measurement.
// =============================================================================

#include "../core/include/core/preamp/PreampModel.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "test_common.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

using JALeaf = transfo::JilesAthertonLeaf<transfo::LangevinPade>;

// ── Config ──────────────────────────────────────────────────────────────────
static constexpr float  kSR         = 44100.0f;
static constexpr int    kBlock      = 512;
static constexpr int    kWarmup     = 176400;    // settling samples (4s)
static constexpr int    kMeasure    = 16384;     // measurement window
static constexpr float  kTolDB      = 1.5f;      // ±1.5 dB target
static constexpr float  kFMin       = 20.0f;
static constexpr float  kFMax       = 20000.0f;
static constexpr int    kNumFreqs   = 30;        // log-spaced points

// Input amplitude per gain position — lower at higher gains to stay linear
static float amplitudeForGain(int gainPos)
{
    if (gainPos <= 2)  return 0.005f;
    if (gainPos <= 6)  return 0.001f;
    return 0.0002f;
}

// ── Measure magnitude at one frequency (Goertzel = fundamental only) ────────
static double measureMagnitudeDB(transfo::PreampModel<JALeaf>& model,
                                 float freq, float sr, float amplitude,
                                 int warmup, int measure)
{
    const int total = warmup + measure;
    const double w = 2.0 * test::kPi * static_cast<double>(freq) / static_cast<double>(sr);

    std::vector<float> input(static_cast<size_t>(total));
    std::vector<float> output(static_cast<size_t>(total));

    for (int i = 0; i < total; ++i)
        input[static_cast<size_t>(i)] = amplitude * static_cast<float>(
            std::sin(w * static_cast<double>(i)));

    // Process in blocks
    int offset = 0, remaining = total;
    while (remaining > 0) {
        int n = std::min(remaining, kBlock);
        model.processBlock(input.data() + offset, output.data() + offset, n);
        offset += n;
        remaining -= n;
    }

    // Goertzel: measure fundamental only (immune to harmonic distortion)
    double outMag = test::goertzelMagnitude(
        output.data() + warmup, measure,
        static_cast<double>(freq), static_cast<double>(sr));
    double inMag = test::goertzelMagnitude(
        input.data() + warmup, measure,
        static_cast<double>(freq), static_cast<double>(sr));

    if (inMag < 1e-30) return -200.0;
    return 20.0 * std::log10(outMag / inMag);
}

// ── Sweep + normalize to 1 kHz ─────────────────────────────────────────────
struct FRPoint { float freq; double mag_dB; };

static std::vector<FRPoint> sweepNevePreamp(int gainPosition)
{
    std::vector<FRPoint> points;
    points.reserve(kNumFreqs);

    const float amplitude = amplitudeForGain(gainPosition);

    for (int i = 0; i < kNumFreqs; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(kNumFreqs - 1);
        float freq = kFMin * std::pow(kFMax / kFMin, t);
        if (freq > kSR * 0.45f) break;

        // Fresh model per frequency (reset state, avoid inter-frequency leakage)
        transfo::PreampModel<JALeaf> model;
        model.setConfig(transfo::PreampConfig::DualTopology());
        model.prepareToPlay(kSR, kBlock);
        model.setPath(0);   // Neve
        model.setGainPosition(gainPosition);

        double mag = measureMagnitudeDB(model, freq, kSR, amplitude, kWarmup, kMeasure);
        points.push_back({freq, mag});
    }

    // Normalize to 1 kHz reference
    double refMag = 0.0;
    float minDist = 1e6f;
    for (const auto& p : points) {
        float dist = std::abs(std::log(p.freq / 1000.0f));
        if (dist < minDist) { minDist = dist; refMag = p.mag_dB; }
    }
    for (auto& p : points)
        p.mag_dB -= refMag;

    return points;
}

// ── Test per gain position ──────────────────────────────────────────────────
static void testFRAtGain(int gainPos, const char* label)
{
    std::printf("\n--- Neve FR @ gain position %d (%s) ---\n", gainPos, label);

    auto points = sweepNevePreamp(gainPos);

    bool pass = true;
    for (const auto& p : points) {
        const char* flag = (std::abs(p.mag_dB) > kTolDB) ? " ***" : "";
        std::printf("  %8.0f Hz: %+.2f dB%s\n", p.freq, p.mag_dB, flag);
        if (p.freq >= kFMin && p.freq <= kFMax) {
            if (std::abs(p.mag_dB) > kTolDB)
                pass = false;
        }
    }

    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "Neve FR %s: passband flatness <=+/-%.1f dB (20-20kHz)",
                  label, kTolDB);
    TEST_ASSERT(pass, msg);
}

// ── Diagnostic: trace 20 Hz amplitude decay over time ───────────────────────
static void diag20Hz()
{
    std::printf("\n=== DIAGNOSTIC: 20 Hz amplitude over time ===\n");
    const float freq = 20.0f;
    const double w = 2.0 * test::kPi * static_cast<double>(freq) / static_cast<double>(kSR);

    int gains[] = {0, 5, 10};
    const char* labels[] = {"min", "mid", "max"};

    for (int g = 0; g < 3; ++g)
    {
        float amp = amplitudeForGain(gains[g]);
        transfo::PreampModel<JALeaf> model;
        model.setConfig(transfo::PreampConfig::DualTopology());
        model.prepareToPlay(kSR, kBlock);
        model.setPath(0);
        model.setGainPosition(gains[g]);

        // Generate 60000 samples (~1.36 sec) of 20 Hz
        const int N = 60000;
        std::vector<float> input(N), output(N);
        for (int i = 0; i < N; ++i)
            input[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(w * i));

        int offset = 0, remaining = N;
        while (remaining > 0) {
            int n = std::min(remaining, kBlock);
            model.processBlock(input.data() + offset, output.data() + offset, n);
            offset += n;
            remaining -= n;
        }

        // Measure Goertzel in successive 4410-sample windows (100ms = 2 cycles @20Hz)
        std::printf("  gain %s (pos %d, amp=%.4f):\n", labels[g], gains[g], amp);
        const int winSize = 4410;  // 100ms
        for (int start = 0; start + winSize <= N; start += winSize)
        {
            double outMag = test::goertzelMagnitude(
                output.data() + start, winSize, 20.0, kSR);
            double inMag = test::goertzelMagnitude(
                input.data() + start, winSize, 20.0, kSR);
            double dB = (inMag > 1e-30) ? 20.0 * std::log10(outMag / inMag) : -999.0;
            std::printf("    t=%.2fs: inMag=%.2e outMag=%.2e  gain=%.1f dB\n",
                        static_cast<float>(start) / kSR, inMag, outMag, dB);
        }
    }
}

// ── Diagnostic: 20 Hz max gain — isolate T1 vs T2 ───────────────────────────
static void diagLinearTransformers()
{
    const float freq = 20.0f;
    const double w = 2.0 * test::kPi * static_cast<double>(freq) / static_cast<double>(kSR);
    const float amp = amplitudeForGain(10);
    const int N = 60000;
    const int winSize = 4410;

    const char* labels[] = {"BOTH LINEAR", "T1 linear only", "T2 linear only"};
    for (int mode = 0; mode < 3; ++mode)
    {
        std::printf("\n=== DIAG: 20 Hz max gain — %s ===\n", labels[mode]);
        transfo::PreampModel<JALeaf> model;
        model.setConfig(transfo::PreampConfig::DualTopology());
        model.prepareToPlay(kSR, kBlock);
        model.setPath(0);
        if (mode == 0 || mode == 1)
            model.getInputStage().getTransformerModel().setLinearMode(true);
        if (mode == 0 || mode == 2)
            model.getOutputStage().getTransformerModel().setLinearMode(true);
        model.setGainPosition(10);

        std::vector<float> input(N), output(N);
        for (int i = 0; i < N; ++i)
            input[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(w * i));

        int offset = 0, remaining = N;
        while (remaining > 0) {
            int n = std::min(remaining, kBlock);
            model.processBlock(input.data() + offset, output.data() + offset, n);
            offset += n;
            remaining -= n;
        }

        // Print 4 windows: settling, early, mid, late
        int windows[] = {0, 4410, 22050, 48510};
        for (int w_i = 0; w_i < 4; ++w_i) {
            int start = windows[w_i];
            if (start + winSize > N) break;
            double outMag = test::goertzelMagnitude(
                output.data() + start, winSize, 20.0, kSR);
            double inMag = test::goertzelMagnitude(
                input.data() + start, winSize, 20.0, kSR);
            double dB = (inMag > 1e-30) ? 20.0 * std::log10(outMag / inMag) : -999.0;
            std::printf("  t=%.2fs: gain=%.1f dB (outMag=%.2e)\n",
                        static_cast<float>(start) / kSR, dB, outMag);
        }
    }
}

// =============================================================================
int main()
{
    std::printf("================================================================\n");
    std::printf("  Preamp FR Full-Chain Sweep — Neve path (Chantier A2)\n");
    std::printf("  C_out = 220uF, Goertzel, target: +/-%.1f dB over 20-20kHz\n", kTolDB);
    std::printf("================================================================\n");

    diag20Hz();
    diagLinearTransformers();

    testFRAtGain(0,  "min gain");
    testFRAtGain(5,  "mid gain");
    testFRAtGain(10, "max gain");

    return test::printSummary("Preamp FR Sweep");
}
