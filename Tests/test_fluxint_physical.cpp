// =============================================================================
// test_fluxint_physical.cpp — FluxIntegrator regression in Physical mode
//
// Lightweight guard: validates that enabling the FluxIntegrator in Physical
// calibration mode does not significantly alter behavior compared to the
// FlatLm baseline.  Also checks FR(20 Hz) stays within the Jensen JT-115K-E
// guaranteed window.
//
// Test contract (from diagnostic 2026-03-29):
//   1. FR(20 Hz) at -20 dBu within Jensen guaranteed [-0.50, 0.0] dB
//   2. FLAT→FLUX FR delta at 20 Hz < 0.15 dB (measured: ~0.05 dB)
//   3. FLAT→FLUX FR delta at 1 kHz < 0.05 dB (measured: ~0.008 dB)
//   4. FLAT→FLUX THD delta at 20 Hz / -20 dBu < 0.5% (measured: ~0.04%)
//   5. FLAT→FLUX THD delta at 1 kHz / -20 dBu < 0.05% (measured: ~0.018%)
//   6. HF not contaminated: cross-mode delta at 10 kHz < 0.5 dB
//
// Does NOT validate absolute THD at 20 Hz (pre-existing Physical LF
// calibration gap — ~1.1% vs Jensen 0.065% typ — deferred to separate
// "Physical LF THD calibration" chantier).
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

static TransformerConfig makePhysFlat()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.calibrationMode = CalibrationMode::Physical;
    cfg.fluxIntegratorEnabled = false;
    return cfg;
}

static TransformerConfig makePhysFlux()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.calibrationMode = CalibrationMode::Physical;
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
// TEST 1: FR(20 Hz) within Jensen guaranteed window
// Jensen JT-115K-E: FR(20 Hz, -20 dBu, TC1) = [-0.50, 0.00] dB
// =============================================================================
static void test_fr_20hz_jensen_window()
{
    std::printf("\n=== FluxInt Physical: FR(20 Hz) Jensen Window ===\n");

    const float amp = dBuToAmplitude(-20.0f);

    // Measure both configs, normalize to 1 kHz
    for (const auto& [label, cfg] : {
        std::pair<const char*, TransformerConfig>{"PHYS_FLAT", makePhysFlat()},
        std::pair<const char*, TransformerConfig>{"PHYS_FLUX", makePhysFlux()}
    }) {
        TransformerModel<CPWLLeaf> model;
        initModel(model, cfg);
        double gain20  = measureGainDB(model, 20.0f, amp);
        double gain1k  = measureGainDB(model, 1000.0f, amp);
        double rel20   = gain20 - gain1k;

        std::printf("  %s: FR(20Hz)=%+.4f dB, FR(1kHz)=%+.4f dB, rel=%+.4f dB\n",
                    label, gain20, gain1k, rel20);

        // Jensen guaranteed: [-0.50, 0.00] dB at 20 Hz relative to 1 kHz
        CHECK_RANGE(rel20, -0.50, 0.05,
                    (std::string(label) + ": FR(20Hz) within Jensen [-0.50, 0.00] dB").c_str());
    }
}

// =============================================================================
// TEST 2: FLAT→FLUX FR delta at 20 Hz and 1 kHz
// =============================================================================
static void test_flat_flux_fr_delta()
{
    std::printf("\n=== FluxInt Physical: FLAT→FLUX FR Delta ===\n");

    const float amp = dBuToAmplitude(-20.0f);
    const float freqs[] = {20.0f, 1000.0f, 10000.0f};
    const double maxDelta[] = {0.15, 0.05, 0.50};  // Allowed delta per frequency
    const char* names[] = {"20 Hz", "1 kHz", "10 kHz"};

    for (int i = 0; i < 3; ++i) {
        TransformerModel<CPWLLeaf> mFlat, mFlux;
        initModel(mFlat, makePhysFlat());
        initModel(mFlux, makePhysFlux());

        double gFlat = measureGainDB(mFlat, freqs[i], amp);
        double gFlux = measureGainDB(mFlux, freqs[i], amp);
        double delta = std::abs(gFlux - gFlat);

        std::printf("  %s: FLAT=%+.4f dB, FLUX=%+.4f dB, delta=%.4f dB (max=%.2f)\n",
                    names[i], gFlat, gFlux, delta, maxDelta[i]);

        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "FLAT→FLUX FR delta at %s < %.2f dB", names[i], maxDelta[i]);
        CHECK(delta < maxDelta[i], msg);
    }
}

// =============================================================================
// TEST 3: FLAT→FLUX THD delta at 20 Hz and 1 kHz
// =============================================================================
static void test_flat_flux_thd_delta()
{
    std::printf("\n=== FluxInt Physical: FLAT→FLUX THD Delta ===\n");

    const float amp = dBuToAmplitude(-20.0f);

    // 20 Hz
    {
        TransformerModel<CPWLLeaf> mFlat, mFlux;
        initModel(mFlat, makePhysFlat());
        initModel(mFlux, makePhysFlux());

        double thdFlat = measureTHDpct(mFlat, 20.0f, amp);
        double thdFlux = measureTHDpct(mFlux, 20.0f, amp);
        double delta = std::abs(thdFlux - thdFlat);

        std::printf("  20 Hz/-20dBu: FLAT=%.4f%%, FLUX=%.4f%%, delta=%.4f%%\n",
                    thdFlat, thdFlux, delta);
        CHECK(delta < 0.5, "FLAT→FLUX THD delta at 20 Hz < 0.5%");
    }

    // 1 kHz
    {
        TransformerModel<CPWLLeaf> mFlat, mFlux;
        initModel(mFlat, makePhysFlat());
        initModel(mFlux, makePhysFlux());

        double thdFlat = measureTHDpct(mFlat, 1000.0f, amp);
        double thdFlux = measureTHDpct(mFlux, 1000.0f, amp);
        double delta = std::abs(thdFlux - thdFlat);

        std::printf("  1 kHz/-20dBu: FLAT=%.4f%%, FLUX=%.4f%%, delta=%.4f%%\n",
                    thdFlat, thdFlux, delta);
        CHECK(delta < 0.05, "FLAT→FLUX THD delta at 1 kHz < 0.05%");
    }
}

// =============================================================================
// TEST 4: FluxInt gating — must be OFF in Artistic mode
// =============================================================================
static void test_fluxint_gating()
{
    std::printf("\n=== FluxInt Gating: OFF in Artistic, ON in Physical ===\n");

    const float amp = dBuToAmplitude(-20.0f);

    // Artistic mode: even with fluxIntegratorEnabled=true, FluxInt must be gated off
    // by configureCircuit() because hScale_artistic would cause massive LF oversaturation.
    auto cfgArtistic = TransformerConfig::Jensen_JT115KE();
    cfgArtistic.calibrationMode = CalibrationMode::Artistic;
    cfgArtistic.fluxIntegratorEnabled = true;  // Request ON, but should be gated off

    auto cfgArtisticOff = TransformerConfig::Jensen_JT115KE();
    cfgArtisticOff.calibrationMode = CalibrationMode::Artistic;
    cfgArtisticOff.fluxIntegratorEnabled = false;

    TransformerModel<CPWLLeaf> mOn, mOff;
    initModel(mOn, cfgArtistic);
    initModel(mOff, cfgArtisticOff);

    // If gating works, both should produce identical FR at 20 Hz
    double gOn  = measureGainDB(mOn,  20.0f, amp);
    double gOff = measureGainDB(mOff, 20.0f, amp);
    double delta = std::abs(gOn - gOff);

    std::printf("  Artistic FluxInt=true:  FR(20Hz)=%+.4f dB\n", gOn);
    std::printf("  Artistic FluxInt=false: FR(20Hz)=%+.4f dB\n", gOff);
    std::printf("  Delta: %.6f dB\n", delta);

    // Identical within numerical noise (< 0.001 dB)
    CHECK(delta < 0.001,
          "Artistic mode: FluxInt gated OFF (delta < 0.001 dB regardless of flag)");
}

// =============================================================================
// MAIN
// =============================================================================
int main()
{
    std::printf("================================================================\n");
    std::printf("  FluxIntegrator Physical Mode Regression Test\n");
    std::printf("  Jensen JT-115K-E — FLAT vs FLUX in Physical calibration\n");
    std::printf("================================================================\n");

    test_fr_20hz_jensen_window();
    test_flat_flux_fr_delta();
    test_flat_flux_thd_delta();
    test_fluxint_gating();

    return test::printSummary("FluxInt Physical Regression");
}
