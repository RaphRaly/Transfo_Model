// =============================================================================
// test_fluxint_artistic.cpp — FluxIntegrator regression in Artistic mode
//
// Lightweight guard for the FluxIntegrator/Bertotti interaction in Artistic
// calibration mode. In the linear no-Bertotti case the integrator and matched
// differentiator are transparent at calibrationFreqHz. With A2 phase 2,
// Bertotti is evaluated between those stages, so FLAT and FLUX intentionally
// produce different nonlinear trajectories.
//
// Test contract (from A2 phase 2 data collection):
//   1. FR(20 Hz) relative to 1 kHz stays in the A2p2 measured envelopes
//   2. FLAT->FLUX FR deltas stay bounded at 20 Hz, 1 kHz, and 10 kHz
//   3. FLAT->FLUX THD deltas stay bounded at 20 Hz and 1 kHz
//   4. FluxInt gating remains OFF in LegacyColor calibration
//
// Does NOT validate absolute THD against Jensen datasheets. Artistic-mode
// K1/K2 fitting is deferred to Sprint A5.
//
// Jensen JT-115K-E datasheet references:
//   FR(20 Hz, -20 dBu, TC1): -0.50 dB min, -0.26 dB typ, 0.0 dB max
//   FR(20 kHz, -20 dBu, TC1): -0.25 dB min, -0.13 dB typ, +0.10 dB max
//   Bandwidth (-3 dB): 2.5 Hz to 90 kHz
// =============================================================================

#include "test_common.h"
#include <core/model/TransformerModel.h>
#include <core/model/TransformerConfig.h>
#include <core/magnetics/CPWLLeaf.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

using namespace transfo;

// ── Constants ────────────────────────────────────────────────────────────────
static constexpr int kWarmupSamples = 16384;
static constexpr int kAnalysisSamples = 65536;
static constexpr int kBlockSize = 512;
static constexpr float kSampleRate = 44100.0f;

// ── dBu to linear amplitude (same convention as test_thd_validation.cpp) ─────
static float dBuToAmplitude(float dBu)
{
    return std::pow(10.0f, dBu / 20.0f) * 0.1f;
}

// ── Dual config factories ────────────────────────────────────────────────────

static TransformerConfig makeArtisticFlat()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.calibrationMode = CalibrationMode::Artistic;
    cfg.fluxIntegratorEnabled = false;
    return cfg;
}

static TransformerConfig makeArtisticFlux()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.calibrationMode = CalibrationMode::Artistic;
    cfg.fluxIntegratorEnabled = true;
    return cfg;
}

// ── Init model helper ────────────────────────────────────────────────────────
static void initModel(TransformerModel<CPWLLeaf>& model, const TransformerConfig& cfg)
{
    model.setConfig(cfg);
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.reset();
    model.prepareToPlay(kSampleRate, kBlockSize);
}

// ── Measure gain (dB) at a single frequency ──────────────────────────────────
static double measureGainDB(TransformerModel<CPWLLeaf>& model,
                            float freq, float amplitude)
{
    model.reset();
    model.prepareToPlay(kSampleRate, kBlockSize);

    const int total = kWarmupSamples + kAnalysisSamples;
    std::vector<float> input(static_cast<size_t>(total));
    std::vector<float> output(static_cast<size_t>(total));

    const float w = 2.0f * 3.14159265f * freq / kSampleRate;
    for (int i = 0; i < total; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(w * static_cast<float>(i));

    int offset = 0, remaining = total;
    while (remaining > 0) {
        int block = std::min(remaining, kBlockSize);
        model.processBlock(input.data() + offset, output.data() + offset, block);
        offset += block;
        remaining -= block;
    }

    // RMS on analysis window
    double inSumSq = 0.0, outSumSq = 0.0;
    for (int i = kWarmupSamples; i < total; ++i) {
        double si = static_cast<double>(input[static_cast<size_t>(i)]);
        double so = static_cast<double>(output[static_cast<size_t>(i)]);
        inSumSq += si * si;
        outSumSq += so * so;
    }
    double inRms = std::sqrt(inSumSq / kAnalysisSamples);
    double outRms = std::sqrt(outSumSq / kAnalysisSamples);

    if (inRms < 1e-30) return -200.0;
    return 20.0 * std::log10(outRms / inRms);
}

// ── Measure THD at a single operating point ──────────────────────────────────
static double measureTHDpct(TransformerModel<CPWLLeaf>& model,
                            float freq, float amplitude)
{
    model.reset();
    model.prepareToPlay(kSampleRate, kBlockSize);

    const int total = kWarmupSamples + kAnalysisSamples;
    std::vector<float> input(static_cast<size_t>(total));
    std::vector<float> output(static_cast<size_t>(total));

    const float w = 2.0f * 3.14159265f * freq / kSampleRate;
    for (int i = 0; i < total; ++i)
        input[static_cast<size_t>(i)] = amplitude * std::sin(w * static_cast<float>(i));

    int offset = 0, remaining = total;
    while (remaining > 0) {
        int block = std::min(remaining, kBlockSize);
        model.processBlock(input.data() + offset, output.data() + offset, block);
        offset += block;
        remaining -= block;
    }

    return test::computeTHD(output.data() + kWarmupSamples,
                            kAnalysisSamples, static_cast<double>(freq), kSampleRate);
}

// =============================================================================
// TEST 1: FR(20 Hz) within A2 phase 2 Artistic/Bertotti envelopes
// =============================================================================
static void test_fr_20hz_jensen_window()
{
    std::printf("\n=== FluxInt Artistic: FR(20 Hz) A2p2 Envelope ===\n");

    const float amp = dBuToAmplitude(-20.0f);

    // Measure both configs, normalize to 1 kHz
    const std::pair<const char*, TransformerConfig> cases[] = {
        {"ART_FLAT", makeArtisticFlat()},
        {"ART_FLUX", makeArtisticFlux()}
    };
    const double minRel20[] = {-0.80, 1.00};
    const double maxRel20[] = {-0.50, 1.30};

    for (int i = 0; i < 2; ++i) {
        const char* label = cases[i].first;
        TransformerModel<CPWLLeaf> model;
        initModel(model, cases[i].second);
        double gain20  = measureGainDB(model, 20.0f, amp);
        double gain1k  = measureGainDB(model, 1000.0f, amp);
        double rel20   = gain20 - gain1k;

        std::printf("  %s: FR(20Hz)=%+.4f dB, FR(1kHz)=%+.4f dB, rel=%+.4f dB\n",
                    label, gain20, gain1k, rel20);

        CHECK_RANGE(rel20, minRel20[i], maxRel20[i],
                    (std::string(label) + ": FR(20Hz) within A2p2 Bertotti envelope").c_str());
    }
}

// =============================================================================
// TEST 2: FLAT→FLUX FR delta at 20 Hz and 1 kHz
// =============================================================================
static void test_flat_flux_fr_delta()
{
    std::printf("\n=== FluxInt Artistic: FLAT→FLUX FR Delta ===\n");

    const float amp = dBuToAmplitude(-20.0f);
    const float freqs[] = {20.0f, 1000.0f, 10000.0f};
    const double maxDelta[] = {4.50, 2.60, 0.80};  // A2p2 measured: 4.1806, 2.3570, 0.6691 dB
    const char* names[] = {"20 Hz", "1 kHz", "10 kHz"};

    for (int i = 0; i < 3; ++i) {
        TransformerModel<CPWLLeaf> mFlat, mFlux;
        initModel(mFlat, makeArtisticFlat());
        initModel(mFlux, makeArtisticFlux());

        double gFlat = measureGainDB(mFlat, freqs[i], amp);
        double gFlux = measureGainDB(mFlux, freqs[i], amp);
        double delta = std::abs(gFlux - gFlat);

        std::printf("  %s: FLAT=%+.4f dB, FLUX=%+.4f dB, delta=%.4f dB (max=%.2f)\n",
                    names[i], gFlat, gFlux, delta, maxDelta[i]);

        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "FLAT->FLUX FR delta at %s < %.2f dB (Bertotti active)", names[i], maxDelta[i]);
        CHECK(delta < maxDelta[i], msg);
    }
}

// =============================================================================
// TEST 3: FLAT→FLUX THD delta at 20 Hz and 1 kHz
// =============================================================================
static void test_flat_flux_thd_delta()
{
    std::printf("\n=== FluxInt Artistic: FLAT→FLUX THD Delta ===\n");

    const float amp = dBuToAmplitude(-20.0f);

    // 20 Hz
    {
        TransformerModel<CPWLLeaf> mFlat, mFlux;
        initModel(mFlat, makeArtisticFlat());
        initModel(mFlux, makeArtisticFlux());

        double thdFlat = measureTHDpct(mFlat, 20.0f, amp);
        double thdFlux = measureTHDpct(mFlux, 20.0f, amp);
        double delta = std::abs(thdFlux - thdFlat);

        std::printf("  20 Hz/-20dBu: FLAT=%.4f%%, FLUX=%.4f%%, delta=%.4f%%\n",
                    thdFlat, thdFlux, delta);
        CHECK(delta < 1.0, "FLAT->FLUX THD delta at 20 Hz < 1% (Bertotti active)");
    }

    // 1 kHz
    {
        TransformerModel<CPWLLeaf> mFlat, mFlux;
        initModel(mFlat, makeArtisticFlat());
        initModel(mFlux, makeArtisticFlux());

        double thdFlat = measureTHDpct(mFlat, 1000.0f, amp);
        double thdFlux = measureTHDpct(mFlux, 1000.0f, amp);
        double delta = std::abs(thdFlux - thdFlat);

        std::printf("  1 kHz/-20dBu: FLAT=%.4f%%, FLUX=%.4f%%, delta=%.4f%%\n",
                    thdFlat, thdFlux, delta);
        CHECK(delta < 60.0, "FLAT->FLUX THD delta at 1 kHz < 60% (Bertotti active)");
    }
}

// =============================================================================
// TEST 4: FluxInt gating — must be OFF in LegacyColor calibration
// =============================================================================
static void test_fluxint_gating()
{
    std::printf("\n=== FluxInt Gating: OFF in LegacyColor, ON in Artistic ===\n");

    const float amp = dBuToAmplitude(-20.0f);

    // LegacyColor calibration: even with fluxIntegratorEnabled=true, FluxInt must
    // be gated off because hScale=a*5 would cause massive LF oversaturation.
    auto cfgLegacy = TransformerConfig::Jensen_JT115KE();
    cfgLegacy.calibrationMode = CalibrationMode::LegacyColor;
    cfgLegacy.fluxIntegratorEnabled = true;  // Request ON, but should be gated off

    auto cfgLegacyOff = TransformerConfig::Jensen_JT115KE();
    cfgLegacyOff.calibrationMode = CalibrationMode::LegacyColor;
    cfgLegacyOff.fluxIntegratorEnabled = false;

    TransformerModel<CPWLLeaf> mOn, mOff;
    initModel(mOn, cfgLegacy);
    initModel(mOff, cfgLegacyOff);

    // If gating works, both should produce identical FR at 20 Hz
    double gOn  = measureGainDB(mOn,  20.0f, amp);
    double gOff = measureGainDB(mOff, 20.0f, amp);
    double delta = std::abs(gOn - gOff);

    std::printf("  LegacyColor FluxInt=true:  FR(20Hz)=%+.4f dB\n", gOn);
    std::printf("  LegacyColor FluxInt=false: FR(20Hz)=%+.4f dB\n", gOff);
    std::printf("  Delta: %.6f dB\n", delta);

    // Identical within numerical noise (< 0.001 dB)
    CHECK(delta < 0.001,
          "LegacyColor calibration: FluxInt gated OFF (delta < 0.001 dB regardless of flag)");
}

// =============================================================================
// MAIN
// =============================================================================
int main()
{
    std::printf("================================================================\n");
    std::printf("  FluxIntegrator Artistic Mode Regression Test\n");
    std::printf("  Jensen JT-115K-E — FLAT vs FLUX in Artistic calibration\n");
    std::printf("================================================================\n");

    test_fr_20hz_jensen_window();
    test_flat_flux_fr_delta();
    test_flat_flux_thd_delta();
    test_fluxint_gating();

    return test::printSummary("FluxInt Artistic Regression");
}
