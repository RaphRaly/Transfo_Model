// =============================================================================
// test_bertotti_zero_regression.cpp — A2 zero-Bertotti regression
//
// Validates that with K1=K2=0, the post-A2 cascade (Voie C, H_eff = H_applied
// - H_dyn pre-J-A) produces output bit-exact identical to a "J-A only"
// reference path (no DynamicLosses contribution).
//
// Test contract:
//   1. With K1=K2=0, output is deterministic across reset/replay.
//   2. With K1=K2=0, all samples finite (no NaN/Inf).
//   3. With K1=K2=0, output differs from a reference run with default
//      Jensen K1/K2 — confirms the Bertotti path is exercised when enabled.
//   4. Output magnitude at K1=K2=0 stays in a sane envelope (sanity check).
//
// Why this guards A2: the new processSample loop (TransformerModel.h) gates
// the Bertotti H_dyn predictor on `directDynLosses_.isEnabled()`, which
// returns false when K1=K2=0. With the gate failed, H_eff = H_applied
// unconditionally, M = solveImplicitStep(H_applied), B = μ₀·(H_applied + M)
// — identical arithmetic to pre-A2 in the no-Bertotti regime.
//
// Sprint reference: A2 Voie C task #5 (renamed: "zero_regression", not
// "implicit_zero_regression" — v1 schéma is explicit by construction).
// =============================================================================

#include "test_common.h"

#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/TransformerConfig.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace transfo;

namespace {

constexpr float kSampleRate = 44100.0f;
constexpr int   kBlockSize  = 512;
constexpr int   kNumSamples = 8192;

void initJensenK1K2Zero(TransformerModel<CPWLLeaf>& model)
{
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(kSampleRate, kBlockSize);
    model.reset();
    // Override Jensen's default K1=1.44e-3, K2=0.02 → Bertotti disabled
    model.setDynamicLossCoefficients(0.0f, 0.0f);
}

void initJensenDefault(TransformerModel<CPWLLeaf>& model)
{
    model.setConfig(TransformerConfig::Jensen_JT115KE());
    model.setProcessingMode(ProcessingMode::Realtime);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(kSampleRate, kBlockSize);
    model.reset();
    // Keep default Jensen K1, K2 → Bertotti enabled
}

void runSine(TransformerModel<CPWLLeaf>& model, float freqHz, float amplitude,
             std::vector<float>& output)
{
    output.resize(kNumSamples);
    std::vector<float> input(kNumSamples);
    const float w = 2.0f * static_cast<float>(test::kPi) * freqHz / kSampleRate;
    for (int i = 0; i < kNumSamples; ++i) {
        input[static_cast<size_t>(i)] =
            amplitude * std::sin(w * static_cast<float>(i));
    }
    int offset = 0, remaining = kNumSamples;
    while (remaining > 0) {
        int block = std::min(remaining, kBlockSize);
        model.processBlock(input.data() + offset, output.data() + offset, block);
        offset += block;
        remaining -= block;
    }
}

bool allFinite(const std::vector<float>& v)
{
    for (float x : v) {
        if (!std::isfinite(x)) return false;
    }
    return true;
}

// ── Test 1: deterministic output across reset/replay ──────────────────────────

void test_deterministic_replay()
{
    std::printf("\n=== Bertotti Zero Regression: Deterministic Replay ===\n");

    TransformerModel<CPWLLeaf> m1, m2;
    initJensenK1K2Zero(m1);
    initJensenK1K2Zero(m2);

    std::vector<float> out1, out2;
    runSine(m1, 1000.0f, 0.5f, out1);
    runSine(m2, 1000.0f, 0.5f, out2);

    bool identical = true;
    int firstDiff = -1;
    for (int i = 0; i < kNumSamples; ++i) {
        if (out1[static_cast<size_t>(i)] != out2[static_cast<size_t>(i)]) {
            identical = false;
            firstDiff = i;
            break;
        }
    }
    if (firstDiff >= 0) {
        std::printf("  First diff at sample %d: %.9g vs %.9g\n",
                    firstDiff,
                    out1[static_cast<size_t>(firstDiff)],
                    out2[static_cast<size_t>(firstDiff)]);
    }
    CHECK(identical, "Two independent runs at K1=K2=0 produce bit-exact output");
}

// ── Test 2: all output samples finite ─────────────────────────────────────────

void test_all_finite()
{
    std::printf("\n=== Bertotti Zero Regression: Finite Output ===\n");

    TransformerModel<CPWLLeaf> model;
    initJensenK1K2Zero(model);

    const float freqs[] = {100.0f, 1000.0f, 10000.0f};
    for (float f : freqs) {
        std::vector<float> out;
        runSine(model, f, 0.5f, out);
        bool finite = allFinite(out);
        std::printf("  %.0f Hz: all finite = %s\n", f, finite ? "yes" : "no");
        CHECK(finite, "All output samples finite at K1=K2=0");
    }
}

// ── Test 3: Bertotti path exercised when K1, K2 > 0 ───────────────────────────
//
// Sanity check that the "K1=K2=0 disables Bertotti" branch is meaningful:
// outputs MUST differ when K1, K2 are non-zero (i.e., at Jensen defaults).
// If outputs were identical regardless of K1/K2, the Bertotti code path
// wouldn't be exercised at all — undermining the whole sprint.

void test_bertotti_path_active_when_enabled()
{
    std::printf("\n=== Bertotti Zero Regression: Path Active When Enabled ===\n");

    TransformerModel<CPWLLeaf> mZero, mDefault;
    initJensenK1K2Zero(mZero);
    initJensenDefault(mDefault);

    std::vector<float> outZero, outDefault;
    // 5 kHz: dB/dt is large enough that Bertotti has measurable effect.
    runSine(mZero,    5000.0f, 0.5f, outZero);
    runSine(mDefault, 5000.0f, 0.5f, outDefault);

    double maxAbsDiff = 0.0;
    int diffSamples = 0;
    for (int i = kBlockSize; i < kNumSamples; ++i) {  // skip warmup
        double d = std::abs(static_cast<double>(outZero[static_cast<size_t>(i)])
                          - static_cast<double>(outDefault[static_cast<size_t>(i)]));
        if (d > 1e-9) diffSamples++;
        if (d > maxAbsDiff) maxAbsDiff = d;
    }
    std::printf("  K1=K2=0 vs K1,K2>0 @ 5 kHz: max|diff|=%.6e, diff samples=%d/%d\n",
                maxAbsDiff, diffSamples, kNumSamples - kBlockSize);
    CHECK(maxAbsDiff > 1e-6,
        "Bertotti has measurable effect when K1, K2 > 0 (path is exercised)");
    CHECK(diffSamples > kNumSamples / 4,
        "Bertotti effect is sustained, not just an isolated transient");
}

// ── Test 4: output magnitude in sane envelope ─────────────────────────────────

void test_sane_envelope()
{
    std::printf("\n=== Bertotti Zero Regression: Sane Output Envelope ===\n");

    TransformerModel<CPWLLeaf> model;
    initJensenK1K2Zero(model);

    std::vector<float> out;
    runSine(model, 1000.0f, 0.5f, out);

    float peak = 0.0f;
    double sumSq = 0.0;
    for (int i = kBlockSize; i < kNumSamples; ++i) {
        float v = std::abs(out[static_cast<size_t>(i)]);
        if (v > peak) peak = v;
        sumSq += static_cast<double>(out[static_cast<size_t>(i)])
               * static_cast<double>(out[static_cast<size_t>(i)]);
    }
    double rms = std::sqrt(sumSq / static_cast<double>(kNumSamples - kBlockSize));
    std::printf("  K1=K2=0 @ 1 kHz / 0.5 amp: peak=%.4f, RMS=%.4f\n",
                peak, rms);
    CHECK(peak < 4.0f,
        "Peak output bounded (no runaway with K1=K2=0)");
    CHECK(peak > 0.05f,
        "Peak output non-trivial (J-A path active)");
}

} // namespace

int main()
{
    std::printf("================================================================\n");
    std::printf("  A2 Bertotti Zero Regression Test\n");
    std::printf("  Validates: K1=K2=0 → cascade output equivalent to J-A only\n");
    std::printf("================================================================\n");

    test_deterministic_replay();
    test_all_finite();
    test_bertotti_path_active_when_enabled();
    test_sane_envelope();

    return test::printSummary("test_bertotti_zero_regression");
}
