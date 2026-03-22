// =============================================================================
// Diagnostic test: trace a 1kHz sine through every stage of the preamp.
//
// Writes:
//   diag_input.wav      — clean 1kHz sine input (reference)
//   diag_neve_out.wav   — Neve path output
//   diag_je990_out.wav  — JE990 path output
//
// Prints per-sample values for the first ~50 samples at each stage
// and detailed statistics (peak, RMS, DC offset, zero crossings).
// =============================================================================

#include "../core/include/core/preamp/PreampModel.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>

using JALeaf = transfo::JilesAthertonLeaf<transfo::LangevinPade>;

// ── WAV writer (16-bit mono, normalizing to avoid clipping) ────────────────

static bool writeWav(const char* path, const float* data, int numSamples,
                     float sr, float normalize = 0.0f)
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::fabs(data[i]));

    float scale = 1.0f;
    if (normalize > 0.0f && peak > normalize)
        scale = normalize / peak;

    FILE* f = fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "Cannot open %s\n", path); return false; }

    const int sampleRate = static_cast<int>(sr);
    const int16_t bitsPerSample = 16;
    const int16_t numChannels = 1;
    const int32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    const int16_t blockAlign = numChannels * bitsPerSample / 8;
    const int32_t dataSize = numSamples * blockAlign;
    const int32_t fileSize = 36 + dataSize;

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    int32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    int16_t audioFormat = 1;
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

    for (int i = 0; i < numSamples; ++i)
    {
        float clamped = std::clamp(data[i] * scale, -1.0f, 1.0f);
        int16_t sample = static_cast<int16_t>(clamped * 32767.0f);
        fwrite(&sample, 2, 1, f);
    }
    fclose(f);
    return true;
}

// ── Statistics helper ──────────────────────────────────────────────────────

struct AudioStats
{
    float peak     = 0.0f;
    float rms      = 0.0f;
    float dcOffset = 0.0f;
    int   zeroCrossings = 0;
    int   clippedSamples = 0;   // |x| > threshold
    int   nanCount = 0;
    int   infCount = 0;
};

static AudioStats analyze(const float* buf, int n, float clipThresh = 0.99f)
{
    AudioStats s;
    double sum = 0.0, sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (std::isnan(buf[i])) { s.nanCount++; continue; }
        if (std::isinf(buf[i])) { s.infCount++; continue; }
        float a = std::fabs(buf[i]);
        if (a > s.peak) s.peak = a;
        sum += buf[i];
        sumSq += static_cast<double>(buf[i]) * buf[i];
        if (a > clipThresh) s.clippedSamples++;
        if (i > 0 && ((buf[i] >= 0.0f) != (buf[i-1] >= 0.0f)))
            s.zeroCrossings++;
    }
    s.dcOffset = static_cast<float>(sum / n);
    s.rms      = static_cast<float>(std::sqrt(sumSq / n));
    return s;
}

static void printStats(const char* label, const AudioStats& s, int n)
{
    float rmsDB = (s.rms > 1e-30f) ? 20.0f * std::log10(s.rms) : -999.0f;
    float peakDB = (s.peak > 1e-30f) ? 20.0f * std::log10(s.peak) : -999.0f;
    std::printf("  %-25s peak=%.6f (%+.1f dBFS)  rms=%.6f (%+.1f dBFS)  "
                "dc=%.6f  zx=%d  clip=%d  nan=%d  inf=%d\n",
                label, s.peak, peakDB, s.rms, rmsDB,
                s.dcOffset, s.zeroCrossings, s.clippedSamples,
                s.nanCount, s.infCount);
}

// ── Constants ──────────────────────────────────────────────────────────────

static constexpr float kSR = 44100.0f;
static constexpr int kBlock = 512;
static constexpr double kPI = 3.14159265358979323846;

// =============================================================================
// Test 1: Trace a gentle 1kHz sine through Neve path
// =============================================================================

static bool test_neve_trace()
{
    std::printf("\n=== TEST 1: Neve Path — 1kHz sine, amplitude 0.05 ===\n\n");

    // 2 seconds of audio
    const int N = static_cast<int>(kSR * 2.0f);
    std::vector<float> input(N);
    std::vector<float> output(N);

    // Very gentle 1kHz sine — typical mic level after A/D
    const float amplitude = 0.05f;
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    // Configure preamp
    transfo::PreampModel<JALeaf> model;
    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    model.setConfig(cfg);
    model.prepareToPlay(kSR, kBlock);
    model.setPath(0);           // Neve
    model.setGainPosition(5);   // +26 dB amplifier (+46 dB with T1 1:10)

    // Process
    model.processBlock(input.data(), output.data(), N);

    // Write WAVs
    writeWav("diag_input.wav", input.data(), N, kSR);
    writeWav("diag_neve_out.wav", output.data(), N, kSR, 0.95f);

    // Stats on full buffer
    int skip = static_cast<int>(kSR * 0.5f); // skip first 0.5s for settling
    int len = N - skip;
    auto inStats  = analyze(input.data() + skip, len);
    auto outStats = analyze(output.data() + skip, len);

    std::printf("  Input:  amp=%.3f  1kHz sine\n", amplitude);
    std::printf("  Gain position 5: Rfb=%g, Acl=%.1f (%.1f dB), T1 ratio=10\n",
                transfo::GainTable::getRfb(5),
                transfo::GainTable::getGainLinear(5),
                transfo::GainTable::getGainDB(5));

    printStats("Input (after settle)", inStats, len);
    printStats("Neve Output", outStats, len);

    float gainLinear = (inStats.rms > 1e-10f) ? outStats.rms / inStats.rms : 0.0f;
    float gainDB = 20.0f * std::log10(gainLinear + 1e-30f);
    std::printf("  Measured gain: %.1fx = %.1f dB (expected ~%.1f dB total)\n",
                gainLinear, gainDB, transfo::GainTable::getTotalGainDB(5));

    // Dump first 50 output samples (after 0.5s settling)
    std::printf("\n  First 50 output samples (after 0.5s):\n");
    for (int i = 0; i < 50 && (skip + i) < N; ++i)
    {
        std::printf("    [%d] in=%.6f  out=%.6f\n", i, input[skip+i], output[skip+i]);
    }

    // Check output is not silent
    if (outStats.rms < 1e-6f)
    {
        std::printf("  FAIL: output is silent!\n");
        return false;
    }
    if (outStats.nanCount > 0 || outStats.infCount > 0)
    {
        std::printf("  FAIL: NaN or Inf in output!\n");
        return false;
    }

    std::printf("  PASS\n");
    return true;
}

// =============================================================================
// Test 2: Trace 1kHz sine through JE990 path
// =============================================================================

static bool test_je990_trace()
{
    std::printf("\n=== TEST 2: JE990 Path — 1kHz sine, amplitude 0.05 ===\n\n");

    const int N = static_cast<int>(kSR * 2.0f);
    std::vector<float> input(N);
    std::vector<float> output(N);

    const float amplitude = 0.05f;
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    transfo::PreampModel<JALeaf> model;
    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    model.setConfig(cfg);
    model.prepareToPlay(kSR, kBlock);
    model.setPath(1);           // JE990
    model.setGainPosition(5);

    // Warmup for crossfade
    std::vector<float> warmup(4096, 0.0f);
    std::vector<float> warmOut(4096);
    model.processBlock(warmup.data(), warmOut.data(), 4096);

    model.processBlock(input.data(), output.data(), N);

    writeWav("diag_je990_out.wav", output.data(), N, kSR, 0.95f);

    int skip = static_cast<int>(kSR * 0.5f);
    int len = N - skip;
    auto inStats  = analyze(input.data() + skip, len);
    auto outStats = analyze(output.data() + skip, len);

    printStats("Input (after settle)", inStats, len);
    printStats("JE990 Output", outStats, len);

    float gainLinear = (inStats.rms > 1e-10f) ? outStats.rms / inStats.rms : 0.0f;
    float gainDB = 20.0f * std::log10(gainLinear + 1e-30f);
    std::printf("  Measured gain: %.1fx = %.1f dB\n", gainLinear, gainDB);

    std::printf("\n  First 50 output samples (after 0.5s):\n");
    for (int i = 0; i < 50 && (skip + i) < N; ++i)
        std::printf("    [%d] in=%.6f  out=%.6f\n", i, input[skip+i], output[skip+i]);

    if (outStats.rms < 1e-6f)
    {
        std::printf("  FAIL: output is silent!\n");
        return false;
    }
    if (outStats.nanCount > 0 || outStats.infCount > 0)
    {
        std::printf("  FAIL: NaN or Inf in output!\n");
        return false;
    }

    std::printf("  PASS\n");
    return true;
}

// =============================================================================
// Test 3: Bypass test — process ONLY through Neve path (no transformers)
// This isolates whether the issue is in the amplifier or the transformers.
// =============================================================================

static bool test_neve_stages_only()
{
    std::printf("\n=== TEST 3: Neve stages ONLY (no T1/T2) — 1kHz sine ===\n\n");

    const int N = static_cast<int>(kSR * 1.0f);
    const float amplitude = 0.05f;

    // Set up JUST the Neve path
    transfo::NeveClassAPath neve;
    transfo::NevePathConfig neveCfg;
    neve.configure(neveCfg);
    neve.prepare(kSR, kBlock);
    neve.setGain(transfo::GainTable::getRfb(5)); // Position 5

    std::vector<float> input(N);
    std::vector<float> output(N);

    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    for (int i = 0; i < N; ++i)
        output[i] = neve.processSample(input[i]);

    writeWav("diag_neve_only.wav", output.data(), N, kSR, 0.95f);

    int skip = static_cast<int>(kSR * 0.3f);
    int len = N - skip;
    auto inStats  = analyze(input.data() + skip, len);
    auto outStats = analyze(output.data() + skip, len);

    printStats("Input", inStats, len);
    printStats("Neve Only (no T1/T2)", outStats, len);

    float gainLinear = (inStats.rms > 1e-10f) ? outStats.rms / inStats.rms : 0.0f;
    float gainDB = 20.0f * std::log10(gainLinear + 1e-30f);
    std::printf("  Measured gain: %.1fx = %.1f dB (expected ~%.1f dB)\n",
                gainLinear, gainDB, transfo::GainTable::getGainDB(5));

    // Report open-loop gain
    std::printf("  Open-loop gain: %.1f\n", neve.getOpenLoopGain());
    std::printf("  Closed-loop gain: %.1f (%.1f dB)\n",
                neve.getClosedLoopGain(), neve.getClosedLoopGainDB());

    auto op = neve.getOperatingPoint();
    std::printf("  Operating point: Q1(Vce=%.2f, Ic=%.4f) Q2(Vce=%.2f, Ic=%.4f) "
                "Q3(Vce=%.2f, Ic=%.4f)\n",
                op.Vce_q1, op.Ic_q1, op.Vce_q2, op.Ic_q2, op.Vce_q3, op.Ic_q3);

    std::printf("\n  First 50 output samples (after 0.3s):\n");
    for (int i = 0; i < 50 && (skip + i) < N; ++i)
        std::printf("    [%d] in=%.6f  out=%.6f\n", i, input[skip+i], output[skip+i]);

    if (outStats.rms < 1e-6f)
    {
        std::printf("  FAIL: output is silent!\n");
        return false;
    }
    std::printf("  PASS\n");
    return true;
}

// =============================================================================
// Test 4: T1 transformer only — check what T1 does to the signal
// =============================================================================

static bool test_t1_only()
{
    std::printf("\n=== TEST 4: T1 transformer ONLY — 1kHz sine ===\n\n");

    const int N = static_cast<int>(kSR * 1.0f);
    const float amplitude = 0.05f;

    auto cfg = transfo::PreampConfig::DualTopology();

    transfo::InputStageWDF<JALeaf> t1;
    t1.prepare(kSR, cfg.input);

    std::vector<float> input(N);
    std::vector<float> output(N);

    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    for (int i = 0; i < N; ++i)
        output[i] = t1.processSample(input[i]);

    writeWav("diag_t1_out.wav", output.data(), N, kSR, 0.95f);

    int skip = static_cast<int>(kSR * 0.3f);
    int len = N - skip;
    auto inStats  = analyze(input.data() + skip, len);
    auto outStats = analyze(output.data() + skip, len);

    printStats("Input", inStats, len);
    printStats("T1 Output", outStats, len);

    float gainLinear = (inStats.rms > 1e-10f) ? outStats.rms / inStats.rms : 0.0f;
    float gainDB = 20.0f * std::log10(gainLinear + 1e-30f);
    std::printf("  Measured T1 gain: %.1fx = %.1f dB (expected ~20 dB)\n",
                gainLinear, gainDB);
    std::printf("  T1 turns ratio: %.1f\n", t1.getTurnsRatio());

    std::printf("\n  First 50 T1 output samples (after 0.3s):\n");
    for (int i = 0; i < 50 && (skip + i) < N; ++i)
        std::printf("    [%d] in=%.6f  t1_out=%.6f\n", i, input[skip+i], output[skip+i]);

    if (outStats.rms < 1e-6f)
    {
        std::printf("  FAIL: T1 output is silent!\n");
        return false;
    }
    std::printf("  PASS\n");
    return true;
}

// =============================================================================
// Test 5: Multiple gain positions — check gain tracking
// =============================================================================

static bool test_gain_positions()
{
    std::printf("\n=== TEST 5: Gain positions 0-10, Neve path ===\n\n");

    const int N = static_cast<int>(kSR * 1.0f);
    const float amplitude = 0.01f; // very low to avoid clipping at high gain

    std::vector<float> input(N);
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    std::printf("  %-8s %-10s %-12s %-12s %-15s %-12s\n",
                "Pos", "Rfb", "Acl(dB)", "Total(dB)", "Measured(dB)", "Peak");

    for (int pos = 0; pos < 11; ++pos)
    {
        transfo::PreampModel<JALeaf> model;
        auto cfg = transfo::PreampConfig::DualTopology();
        cfg.t2Config.loadImpedance = 10000.0f;
        model.setConfig(cfg);
        model.prepareToPlay(kSR, kBlock);
        model.setPath(0);
        model.setGainPosition(pos);

        std::vector<float> output(N);
        model.processBlock(input.data(), output.data(), N);

        int skip = static_cast<int>(kSR * 0.5f);
        int len = N - skip;
        auto inS = analyze(input.data() + skip, len);
        auto outS = analyze(output.data() + skip, len);

        float measuredGain = (inS.rms > 1e-10f) ? outS.rms / inS.rms : 0.0f;
        float measuredDB = 20.0f * std::log10(measuredGain + 1e-30f);

        std::printf("  %-8d %-10.0f %-12.1f %-12.1f %-15.1f %-12.6f\n",
                    pos,
                    transfo::GainTable::getRfb(pos),
                    transfo::GainTable::getGainDB(pos),
                    transfo::GainTable::getTotalGainDB(pos),
                    measuredDB,
                    outS.peak);
    }

    std::printf("  PASS (visual inspection)\n");
    return true;
}

// =============================================================================
// Test 6: Waveform quality — check for obvious distortion artifacts
// =============================================================================

static bool test_waveform_quality()
{
    std::printf("\n=== TEST 6: Waveform quality check — 1kHz, low level ===\n\n");

    const int N = static_cast<int>(kSR * 2.0f);
    const float amplitude = 0.005f; // very low to stay in linear region

    transfo::PreampModel<JALeaf> model;
    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    model.setConfig(cfg);
    model.prepareToPlay(kSR, kBlock);
    model.setPath(0);
    model.setGainPosition(3); // moderate gain

    std::vector<float> input(N);
    std::vector<float> output(N);

    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = amplitude * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    model.processBlock(input.data(), output.data(), N);

    writeWav("diag_quality.wav", output.data(), N, kSR, 0.95f);

    // Analyze second half (well settled)
    int skip = N / 2;
    int len = N - skip;
    auto outStats = analyze(output.data() + skip, len);

    printStats("Output (settled)", outStats, len);

    // Expected zero crossings for 1kHz at 44100Hz over 1 second:
    // ~2000 zero crossings per second (sine crosses zero 2x per cycle)
    float expectedZC = 2.0f * 1000.0f;  // 1kHz = 2000 ZC/sec
    float measuredZCRate = static_cast<float>(outStats.zeroCrossings) /
                           (static_cast<float>(len) / kSR);
    std::printf("  Zero crossing rate: %.0f/sec (expected ~%.0f for 1kHz)\n",
                measuredZCRate, expectedZC);

    // Check that output is roughly sinusoidal: peak/rms should be near sqrt(2)
    float crestFactor = (outStats.rms > 1e-10f) ? outStats.peak / outStats.rms : 0.0f;
    std::printf("  Crest factor: %.3f (pure sine = %.3f)\n",
                crestFactor, std::sqrt(2.0f));

    // High crest factor = heavily distorted/spiky
    // Low crest factor = square wave / clipped
    if (crestFactor > 5.0f)
    {
        std::printf("  WARNING: Very high crest factor (spiky output)\n");
    }
    if (crestFactor < 1.0f)
    {
        std::printf("  WARNING: Very low crest factor (DC or clipped)\n");
    }

    // Check DC offset is reasonable (should be near zero after HP filter)
    if (std::fabs(outStats.dcOffset) > outStats.peak * 0.3f)
    {
        std::printf("  WARNING: Large DC offset (%.6f vs peak %.6f)\n",
                    outStats.dcOffset, outStats.peak);
    }

    // Dump one complete cycle (44.1 samples at 1kHz)
    std::printf("\n  One complete cycle (samples %d to %d):\n", skip, skip + 45);
    for (int i = 0; i < 45 && (skip + i) < N; ++i)
    {
        float t = static_cast<float>(skip + i) / kSR;
        std::printf("    [%d] in=%+.6f  out=%+.6f\n", i, input[skip+i], output[skip+i]);
    }

    std::printf("  PASS\n");
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::printf("================================================================\n");
    std::printf("  Preamp Diagnostic Trace\n");
    std::printf("  Tracing 1kHz sine through each stage\n");
    std::printf("================================================================\n");

    int pass = 0, fail = 0;

    auto run = [&](const char* name, bool (*fn)()) {
        std::printf("\n  Running: %s ... ", name);
        if (fn()) { std::printf("\n"); pass++; }
        else { std::printf("\n"); fail++; }
    };

    run("test_t1_only",          test_t1_only);
    run("test_neve_stages_only", test_neve_stages_only);
    run("test_neve_trace",       test_neve_trace);
    run("test_je990_trace",      test_je990_trace);
    run("test_gain_positions",   test_gain_positions);
    run("test_waveform_quality", test_waveform_quality);

    std::printf("\n================================================================\n");
    std::printf("  Results: %d passed, %d failed\n", pass, fail);
    std::printf("================================================================\n");

    std::printf("\n  WAV files written:\n");
    std::printf("    diag_input.wav       — clean 1kHz sine reference\n");
    std::printf("    diag_neve_out.wav    — full chain Neve output\n");
    std::printf("    diag_je990_out.wav   — full chain JE990 output\n");
    std::printf("    diag_neve_only.wav   — Neve stages only (no T1/T2)\n");
    std::printf("    diag_t1_out.wav      — T1 transformer only\n");
    std::printf("    diag_quality.wav     — low-level quality check\n");

    return fail > 0 ? 1 : 0;
}
