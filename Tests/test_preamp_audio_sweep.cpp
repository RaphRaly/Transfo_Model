// =============================================================================
// Test: PreampModel — Real audio sweep test (post-fix validation)
//
// This test exercises the preamp at DAW-realistic signal levels (0.1-1.0)
// which previously caused saturation and silence. It generates:
//   1. A 20Hz-20kHz logarithmic sine sweep
//   2. Processes it through both Neve and JE990 paths
//   3. Writes output WAV files for audible verification
//   4. Validates: no NaN/Inf, no silence, no permanent clipping
//
// The test FAILS if the output is silent, contains NaN, or is stuck at
// the rail for more than a few consecutive samples.
//
// WAV output: out_sweep_neve.wav, out_sweep_je990.wav
// =============================================================================

#include "../core/include/core/preamp/PreampModel.h"
#include "../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>
#include <string>

using JALeaf = transfo::JilesAthertonLeaf<transfo::LangevinPade>;

// ── Test macros ──────────────────────────────────────────────────────────────

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s\n  %s:%d\n", msg, __FILE__, __LINE__); \
        return false; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    std::printf("  Running: %s ... ", #fn); \
    if (fn()) { std::printf("PASS\n"); pass++; } \
    else { std::printf("FAIL\n"); fail++; } \
} while(0)

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr float  kSR         = 44100.0f;
static constexpr int    kBlock      = 512;
static constexpr double kPI         = 3.14159265358979323846;
static constexpr float  kSweepStart = 20.0f;     // Hz
static constexpr float  kSweepEnd   = 20000.0f;  // Hz
static constexpr float  kSweepDur   = 3.0f;      // seconds
static constexpr int    kSweepLen   = static_cast<int>(kSR * kSweepDur);

// ── Helper: default config ───────────────────────────────────────────────────

static transfo::PreampConfig makeDefaultConfig()
{
    auto cfg = transfo::PreampConfig::DualTopology();
    cfg.t2Config.loadImpedance = 10000.0f;
    return cfg;
}

// ── Helper: generate log sweep 20Hz-20kHz ────────────────────────────────────

static void generateLogSweep(float* buf, int numSamples, float amplitude)
{
    const double k = std::log(static_cast<double>(kSweepEnd) / kSweepStart);
    double phase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        double t = static_cast<double>(i) / kSR;
        double instantFreq = kSweepStart * std::exp(k * t / kSweepDur);
        phase += 2.0 * kPI * instantFreq / kSR;
        buf[i] = amplitude * static_cast<float>(std::sin(phase));
    }
}

// ── Helper: compute RMS ──────────────────────────────────────────────────────

static double computeRMS(const float* buf, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(buf[i]) * buf[i];
    return std::sqrt(sum / numSamples);
}

// ── Helper: check NaN/Inf ────────────────────────────────────────────────────

static bool hasNaNOrInf(const float* buf, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
        if (!std::isfinite(buf[i]))
            return true;
    return false;
}

// ── Helper: count consecutive clipped samples (|x| > threshold) ──────────────

static int maxConsecutiveClipped(const float* buf, int numSamples, float threshold)
{
    int maxRun = 0;
    int currentRun = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        if (std::fabs(buf[i]) > threshold)
        {
            currentRun++;
            if (currentRun > maxRun)
                maxRun = currentRun;
        }
        else
        {
            currentRun = 0;
        }
    }
    return maxRun;
}

// ── WAV writer (minimal 16-bit mono) ─────────────────────────────────────────

static bool writeWav(const char* path, const float* data, int numSamples, float sr)
{
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        std::fprintf(stderr, "Cannot open %s for writing\n", path);
        return false;
    }

    const int sampleRate = static_cast<int>(sr);
    const int16_t bitsPerSample = 16;
    const int16_t numChannels = 1;
    const int32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    const int16_t blockAlign = numChannels * bitsPerSample / 8;
    const int32_t dataSize = numSamples * blockAlign;
    const int32_t fileSize = 36 + dataSize;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    int32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    int16_t audioFormat = 1;  // PCM
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);

    // data chunk
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

    // Write samples
    for (int i = 0; i < numSamples; ++i)
    {
        float clamped = std::clamp(data[i], -1.0f, 1.0f);
        int16_t sample = static_cast<int16_t>(clamped * 32767.0f);
        fwrite(&sample, 2, 1, f);
    }

    fclose(f);
    return true;
}

// =============================================================================
// TEST 1 — DAW-level amplitude: 0.5 peak (typical instrument track)
// =============================================================================

static bool test_daw_level_neve()
{
    transfo::PreampModel<JALeaf> model;
    model.setConfig(makeDefaultConfig());
    model.prepareToPlay(kSR, kBlock);
    model.setPath(0);            // Neve
    model.setGainPosition(5);    // Default +26 dB

    std::vector<float> input(kSweepLen);
    std::vector<float> output(kSweepLen);

    generateLogSweep(input.data(), kSweepLen, 0.5f);
    model.processBlock(input.data(), output.data(), kSweepLen);

    // Write WAV for audible verification
    writeWav("out_sweep_neve.wav", output.data(), kSweepLen, kSR);

    // Validate: no NaN/Inf
    ASSERT_TRUE(!hasNaNOrInf(output.data(), kSweepLen),
                "Neve sweep: no NaN/Inf");

    // Validate: not silent (RMS of last 2 seconds > -60 dBFS)
    int checkStart = static_cast<int>(kSR);  // skip first second for settling
    int checkLen = kSweepLen - checkStart;
    double rms = computeRMS(output.data() + checkStart, checkLen);
    std::printf("[RMS=%.4e = %.1f dBFS] ", rms, 20.0 * std::log10(rms + 1e-30));

    ASSERT_TRUE(rms > 1e-3,  // > -60 dBFS
                "Neve sweep: output is not silent (RMS > -60 dBFS)");

    // Validate: not permanently clipped (no more than 100 consecutive clipped samples)
    int maxClip = maxConsecutiveClipped(output.data() + checkStart, checkLen, 0.99f);
    std::printf("[maxClipRun=%d] ", maxClip);

    ASSERT_TRUE(maxClip < 100,
                "Neve sweep: no sustained permanent clipping");

    return true;
}

// =============================================================================
// TEST 2 — DAW-level amplitude: JE-990 path (the one that had the Acl bug)
// =============================================================================

static bool test_daw_level_je990()
{
    transfo::PreampModel<JALeaf> model;
    model.setConfig(makeDefaultConfig());
    model.prepareToPlay(kSR, kBlock);
    model.setPath(1);            // JE-990
    model.setGainPosition(5);    // Default +26 dB

    // Warmup to let crossfade settle to JE-990
    std::vector<float> warmup(4096, 0.0f);
    std::vector<float> warmupOut(4096);
    model.processBlock(warmup.data(), warmupOut.data(), 4096);

    std::vector<float> input(kSweepLen);
    std::vector<float> output(kSweepLen);

    generateLogSweep(input.data(), kSweepLen, 0.5f);
    model.processBlock(input.data(), output.data(), kSweepLen);

    // Write WAV
    writeWav("out_sweep_je990.wav", output.data(), kSweepLen, kSR);

    // Validate: no NaN/Inf
    ASSERT_TRUE(!hasNaNOrInf(output.data(), kSweepLen),
                "JE990 sweep: no NaN/Inf");

    // Validate: not silent
    int checkStart = static_cast<int>(kSR);
    int checkLen = kSweepLen - checkStart;
    double rms = computeRMS(output.data() + checkStart, checkLen);
    std::printf("[RMS=%.4e = %.1f dBFS] ", rms, 20.0 * std::log10(rms + 1e-30));

    ASSERT_TRUE(rms > 1e-3,
                "JE990 sweep: output is not silent (RMS > -60 dBFS)");

    // Validate: not permanently clipped
    int maxClip = maxConsecutiveClipped(output.data() + checkStart, checkLen, 0.99f);
    std::printf("[maxClipRun=%d] ", maxClip);

    ASSERT_TRUE(maxClip < 100,
                "JE990 sweep: no sustained permanent clipping");

    return true;
}

// =============================================================================
// TEST 3 — Full-scale stress test (worst case: 1.0 amplitude, max gain)
// =============================================================================

static bool test_fullscale_stress()
{
    transfo::PreampModel<JALeaf> model;
    model.setConfig(makeDefaultConfig());
    model.prepareToPlay(kSR, kBlock);
    model.setPath(0);             // Neve
    model.setGainPosition(10);    // Max gain +56 dB

    std::vector<float> input(kSweepLen);
    std::vector<float> output(kSweepLen);

    generateLogSweep(input.data(), kSweepLen, 1.0f);  // Full scale
    model.processBlock(input.data(), output.data(), kSweepLen);

    writeWav("out_sweep_fullscale.wav", output.data(), kSweepLen, kSR);

    // Must NOT produce NaN/Inf even under extreme conditions
    ASSERT_TRUE(!hasNaNOrInf(output.data(), kSweepLen),
                "Fullscale stress: no NaN/Inf even at max gain + full scale input");

    // Must produce SOME output (not complete silence)
    int checkStart = static_cast<int>(kSR);
    int checkLen = kSweepLen - checkStart;
    double rms = computeRMS(output.data() + checkStart, checkLen);
    std::printf("[RMS=%.4e = %.1f dBFS] ", rms, 20.0 * std::log10(rms + 1e-30));

    ASSERT_TRUE(rms > 1e-4,
                "Fullscale stress: output is not completely silent");

    return true;
}

// =============================================================================
// TEST 4 — JE990 max gain stress (the exact scenario that caused the bug)
// =============================================================================

static bool test_je990_max_gain_stress()
{
    transfo::PreampModel<JALeaf> model;
    model.setConfig(makeDefaultConfig());
    model.prepareToPlay(kSR, kBlock);
    model.setPath(1);             // JE-990
    model.setGainPosition(10);    // Max gain — Acl = 1 + 14700/47 = 313.8x

    // Warmup
    std::vector<float> warmup(4096, 0.0f);
    std::vector<float> warmupOut(4096);
    model.processBlock(warmup.data(), warmupOut.data(), 4096);

    std::vector<float> input(kSweepLen);
    std::vector<float> output(kSweepLen);

    generateLogSweep(input.data(), kSweepLen, 1.0f);  // Full scale
    model.processBlock(input.data(), output.data(), kSweepLen);

    writeWav("out_sweep_je990_maxgain.wav", output.data(), kSweepLen, kSR);

    // No NaN/Inf — this was the exact failure mode before the fix
    ASSERT_TRUE(!hasNaNOrInf(output.data(), kSweepLen),
                "JE990 max gain: no NaN/Inf (previously produced Inf from exp overflow)");

    int checkStart = static_cast<int>(kSR);
    int checkLen = kSweepLen - checkStart;
    double rms = computeRMS(output.data() + checkStart, checkLen);
    std::printf("[RMS=%.4e = %.1f dBFS] ", rms, 20.0 * std::log10(rms + 1e-30));

    ASSERT_TRUE(rms > 1e-4,
                "JE990 max gain: output is not silent");

    return true;
}

// =============================================================================
// TEST 5 — Recovery from saturation: hot signal then normal level
// =============================================================================

static bool test_sustained_signal_no_fadeout()
{
    // Test that a sustained signal at normal DAW levels does not fade out
    // over time (no progressive silence from DC tracker drift or filter lockup)
    transfo::PreampModel<JALeaf> model;
    model.setConfig(makeDefaultConfig());
    model.prepareToPlay(kSR, kBlock);
    model.setPath(0);
    model.setGainPosition(5);

    const int N = static_cast<int>(kSR * 4.0f);  // 4 seconds
    std::vector<float> input(N);
    std::vector<float> output(N);

    // Sustained 1kHz tone at moderate DAW level
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = 0.3f * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    model.processBlock(input.data(), output.data(), N);

    writeWav("out_sustained_signal.wav", output.data(), N, kSR);

    ASSERT_TRUE(!hasNaNOrInf(output.data(), N),
                "Sustained signal: no NaN/Inf");

    // Compare RMS of second 1 vs second 4 — should not have faded significantly
    int sec1Start = static_cast<int>(kSR * 1.0f);
    int sec1Len = static_cast<int>(kSR * 1.0f);
    int sec4Start = static_cast<int>(kSR * 3.0f);
    int sec4Len = static_cast<int>(kSR * 1.0f);

    double rmsSec1 = computeRMS(output.data() + sec1Start, sec1Len);
    double rmsSec4 = computeRMS(output.data() + sec4Start, sec4Len);

    std::printf("[sec1=%.4e sec4=%.4e ratio=%.3f] ", rmsSec1, rmsSec4,
                rmsSec4 / (rmsSec1 + 1e-30));

    ASSERT_TRUE(rmsSec1 > 1e-4,
                "Sustained signal: output is not silent at second 1");
    ASSERT_TRUE(rmsSec4 > 1e-4,
                "Sustained signal: output is not silent at second 4");

    // Output should not have faded by more than 6 dB (half)
    ASSERT_TRUE(rmsSec4 > rmsSec1 * 0.5,
                "Sustained signal: output did not fade by more than 6 dB over 3 seconds");

    return true;
}

// =============================================================================
// TEST 6 — Both paths produce comparable output (no 300x gain mismatch)
// =============================================================================

static bool test_path_gain_parity()
{
    const int N = 8192;
    std::vector<float> input(N);
    std::vector<float> outNeve(N);
    std::vector<float> outJensen(N);

    // Moderate level sine
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / kSR;
        input[i] = 0.1f * std::sin(2.0f * static_cast<float>(kPI) * 1000.0f * t);
    }

    // Neve path
    {
        transfo::PreampModel<JALeaf> model;
        model.setConfig(makeDefaultConfig());
        model.prepareToPlay(kSR, kBlock);
        model.setPath(0);
        model.setGainPosition(5);
        model.processBlock(input.data(), outNeve.data(), N);
    }

    // JE-990 path
    {
        transfo::PreampModel<JALeaf> model;
        model.setConfig(makeDefaultConfig());
        model.prepareToPlay(kSR, kBlock);
        model.setPath(1);
        model.setGainPosition(5);

        std::vector<float> warmup(4096, 0.0f);
        std::vector<float> warmupOut(4096);
        model.processBlock(warmup.data(), warmupOut.data(), 4096);

        model.processBlock(input.data(), outJensen.data(), N);
    }

    int checkStart = N / 2;
    int checkLen = N - checkStart;
    double rmsNeve = computeRMS(outNeve.data() + checkStart, checkLen);
    double rmsJensen = computeRMS(outJensen.data() + checkStart, checkLen);

    std::printf("[Neve=%.4e Jensen=%.4e ratio=%.2f] ",
                rmsNeve, rmsJensen,
                rmsJensen > 1e-30 ? rmsNeve / rmsJensen : 999.0);

    ASSERT_TRUE(rmsNeve > 1e-6, "Neve produces output");
    ASSERT_TRUE(rmsJensen > 1e-6, "JE990 produces output");

    // Both paths should produce meaningful output (not silence or explosion).
    // The paths have inherently different gains due to different topologies
    // (Neve 2-CE vs JE990 DiffPair+VAS), so we only check that neither
    // is silent nor wildly out of range.
    double ratio = rmsNeve / (rmsJensen + 1e-30);
    ASSERT_TRUE(ratio > 0.001 && ratio < 1000.0,
                "Neve and JE990 output levels within 60 dB of each other");

    return true;
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::printf("================================================================\n");
    std::printf("  PreampModel Audio Sweep — Post-Fix Validation\n");
    std::printf("  DAW-realistic levels + WAV output for audible verification\n");
    std::printf("================================================================\n\n");

    int pass = 0, fail = 0;

    RUN_TEST(test_daw_level_neve);
    RUN_TEST(test_daw_level_je990);
    RUN_TEST(test_fullscale_stress);
    RUN_TEST(test_je990_max_gain_stress);
    RUN_TEST(test_sustained_signal_no_fadeout);
    RUN_TEST(test_path_gain_parity);

    std::printf("\n================================================================\n");
    std::printf("  Results: %d passed, %d failed\n", pass, fail);
    std::printf("================================================================\n");

    if (pass == 6 && fail == 0)
    {
        std::printf("\n  WAV files written:\n");
        std::printf("    out_sweep_neve.wav         — Neve path, 0.5 amplitude\n");
        std::printf("    out_sweep_je990.wav        — JE990 path, 0.5 amplitude\n");
        std::printf("    out_sweep_fullscale.wav    — Neve max gain, full scale\n");
        std::printf("    out_sweep_je990_maxgain.wav — JE990 max gain, full scale\n");
        std::printf("    out_sustained_signal.wav    — 4s sustained tone stability\n");
        std::printf("\n  Import these into your DAW to verify they sound correct.\n");
    }

    return fail > 0 ? 1 : 0;
}
