// Test: VAS Stage Isolated Diagnostic
// Validates that VAS AC gain matches theory (Section V of UAD critique).
// If gain ≈ 338 (single degen) → processSample is correct, no fix needed.
// If gain ≈ 33 (double degen) → processSample has a bug.

#include "test_common.h"
#include "../core/include/core/preamp/VASStageWDF.h"
#include "../core/include/core/wdf/BJTLeaf.h"
#include <cstdio>
#include <cmath>

using namespace transfo;

static constexpr float kSampleRate = 44100.0f;

// Test 1: Measure AC gain with small-signal probe
static void test_vas_ac_gain()
{
    std::printf("\n--- Test: VAS AC Gain Diagnostic ---\n");

    VASStageWDF<BJTLeaf> vas;
    VASConfig cfg = VASConfig::Q6_Default();
    vas.prepare(kSampleRate, cfg);

    // Warmup 500 samples at zero input
    for (int i = 0; i < 500; ++i)
        vas.processSample(0.0f);

    // Small-signal probe: 1mV input
    const float probeInput = 0.001f;
    float out = vas.processSample(probeInput);
    float gain = std::abs(out / probeInput);

    std::printf("    Input:  %.6f V\n", probeInput);
    std::printf("    Output: %.6f V\n", out);
    std::printf("    AC gain (measured): %.1f\n", gain);

    // Theoretical: R_coll_AC / (R_emitter + 1/gm) with Miller LP
    // At Iq=1.5mA: gm = 0.058 S, 1/gm = 17.2
    // Av_theory = 60000 / (160 + 17.2) = 338.5
    // After Miller LP (alpha ~0.716 at first sample): gain is reduced
    // But after warmup, Miller state is at DC → first AC sample sees millerAlpha
    float gm = vas.getGm();
    float Av_theory = std::abs(vas.getGainInstantaneous());
    std::printf("    gm (measured): %.4f S\n", gm);
    std::printf("    Av_theory (getGainInstantaneous): %.1f\n", Av_theory);
    std::printf("    getEffectiveGainPerSample: %.1f\n",
                std::abs(vas.getEffectiveGainPerSample()));

    // The gain should be in the range of getGainInstantaneous (with Miller attenuation)
    // If double degen existed, gain would be ~33 (10x lower)
    CHECK(gain > 50.0f, "VAS gain > 50 (not double-degenerated to ~33)");
    CHECK(gain < 500.0f, "VAS gain < 500 (physically bounded)");

    // Check consistency: measured gain should be close to getEffectiveGainPerSample
    // (within factor of 3, since Miller LP is state-dependent)
    float effGain = std::abs(vas.getEffectiveGainPerSample());
    float ratio = gain / effGain;
    std::printf("    Measured/Effective ratio: %.2f (expect ~1.0)\n", ratio);
    CHECK(ratio > 0.2f && ratio < 5.0f,
          "Measured gain consistent with getEffectiveGainPerSample (within 5x)");
}

// Test 2: Multi-sample steady-state gain
static void test_vas_steady_state_gain()
{
    std::printf("\n--- Test: VAS Steady-State Gain ---\n");

    VASStageWDF<BJTLeaf> vas;
    VASConfig cfg = VASConfig::Q6_Default();
    vas.prepare(kSampleRate, cfg);

    // Warmup with zero
    for (int i = 0; i < 500; ++i)
        vas.processSample(0.0f);

    // Apply 100 samples of 1kHz sine at 1mV amplitude
    const int N = 4096;
    const double freq = 1000.0;
    float peakOut = 0.0f;
    float peakIn = 0.001f;

    for (int i = 0; i < N; ++i)
    {
        double t = static_cast<double>(i) / kSampleRate;
        float in = peakIn * static_cast<float>(std::sin(2.0 * 3.14159265358979 * freq * t));
        float out = vas.processSample(in);
        if (std::abs(out) > peakOut)
            peakOut = std::abs(out);
    }

    float steadyGain = peakOut / peakIn;
    std::printf("    Peak input:  %.6f V\n", peakIn);
    std::printf("    Peak output: %.6f V\n", peakOut);
    std::printf("    Steady-state gain: %.1f\n", steadyGain);
    std::printf("    Gain in dB: %.1f dB\n", 20.0f * std::log10(steadyGain));

    // After Miller LP settles, gain should approach getGainInstantaneous * millerAlpha
    // Expected ~242 for 1kHz (well below Miller pole at 17.7kHz)
    float effGain = std::abs(vas.getEffectiveGainPerSample());
    std::printf("    getEffectiveGainPerSample: %.1f\n", effGain);

    // At 1kHz (well below 17.7kHz Miller pole), gain should be close to
    // the full gain (338) since millerAlpha is per-sample, not per-frequency
    CHECK(steadyGain > 100.0f, "Steady-state VAS gain > 100 (meaningful amplification)");
    CHECK(steadyGain < 500.0f, "Steady-state VAS gain < 500 (bounded)");

    std::printf("\n    VERDICT: ");
    if (steadyGain > 200.0f)
        std::printf("Single degeneration confirmed (gain ~338 range)\n");
    else if (steadyGain > 50.0f)
        std::printf("Intermediate — Miller LP attenuation likely dominant\n");
    else
        std::printf("WARNING: Possible double degeneration (gain < 50)\n");
}

// Test 3: Verify getGainInstantaneous matches formula
static void test_vas_gain_formula_consistency()
{
    std::printf("\n--- Test: VAS Gain Formula Consistency ---\n");

    VASStageWDF<BJTLeaf> vas;
    VASConfig cfg = VASConfig::Q6_Default();
    vas.prepare(kSampleRate, cfg);

    // Warmup
    for (int i = 0; i < 500; ++i)
        vas.processSample(0.0f);

    float gm = vas.getGm();
    float Av_method = std::abs(vas.getGainInstantaneous());

    // Manual computation: R_coll_AC / (R_emitter + 1/gm)
    float Av_manual = cfg.R_coll_AC / (cfg.R_emitter + 1.0f / gm);

    // Alternative: gm * R_coll_AC * degen
    float degen = 1.0f / (1.0f + gm * cfg.R_emitter);
    float Av_alt = gm * cfg.R_coll_AC * degen;

    std::printf("    gm = %.4f S  (Iq/Vt = %.4f)\n", gm, cfg.I_quiescent / cfg.bjt.Vt);
    std::printf("    Av (getGainInstantaneous): %.2f\n", Av_method);
    std::printf("    Av (R_coll / (Re + 1/gm)): %.2f\n", Av_manual);
    std::printf("    Av (gm * R_coll * degen):  %.2f\n", Av_alt);
    std::printf("    degen factor: %.4f\n", degen);

    // All three should be identical
    CHECK_NEAR(Av_method, Av_manual, 0.1,
               "getGainInstantaneous == R_coll/(Re+1/gm)");
    CHECK_NEAR(Av_method, Av_alt, 0.1,
               "getGainInstantaneous == gm*R_coll*degen");
    CHECK_NEAR(Av_manual, Av_alt, 0.01,
               "Both formulas are algebraically identical");
}

int main()
{
    std::printf("================================================================\n");
    std::printf("  VAS Stage Diagnostic — Double Degeneration Check\n");
    std::printf("  Section V of UAD Phase 3 Critique\n");
    std::printf("================================================================\n");

    test_vas_ac_gain();
    test_vas_steady_state_gain();
    test_vas_gain_formula_consistency();

    std::printf("\n");
    test::printSummary("test_vas_diagnostic");
    return (test::g_fail() > 0) ? 1 : 0;
}
