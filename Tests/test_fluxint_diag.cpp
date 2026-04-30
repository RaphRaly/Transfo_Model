// =============================================================================
// test_fluxint_diag.cpp — FluxIntegrator Exploration Diagnostic
//
// Standalone diagnostic that dumps FR sweep, THD vs level, THD vs frequency,
// harmonic spectrum, and cross-mode HF comparison for Jensen JT-115K-E.
//
// Run BEFORE writing ArtisticFlux test thresholds. Output is plain text tables
// to stdout, one measurement per line, easily parseable.
//
// Usage: test_fluxint_diag.exe
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
static constexpr int kTotalSamples = kWarmupSamples + kAnalysisSamples;
static constexpr int kBlockSize = 512;
static constexpr float kSampleRate = 44100.0f;

// ── Dual config helpers (Step 0) ─────────────────────────────────────────────

static TransformerConfig makeConfigFlatLm()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.fluxIntegratorEnabled = false;
    return cfg;
}

static TransformerConfig makeConfigFlux()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.fluxIntegratorEnabled = true;  // explicit
    return cfg;
}

// Artistic calibration + FluxInt ON — the correct combination per Perplexity analysis
static TransformerConfig makeConfigArtisticFlux()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.calibrationMode = CalibrationMode::Artistic;
    cfg.fluxIntegratorEnabled = true;
    return cfg;
}

// Artistic calibration + FluxInt OFF — baseline for comparison
static TransformerConfig makeConfigArtisticFlat()
{
    auto cfg = TransformerConfig::Jensen_JT115KE();
    cfg.calibrationMode = CalibrationMode::Artistic;
    cfg.fluxIntegratorEnabled = false;
    return cfg;
}

// ── dBu to linear amplitude ──────────────────────────────────────────────────
static float dBuToAmplitude(float dBu)
{
    return std::pow(10.0f, dBu / 20.0f) * 0.1f;
}

// ── Process sine through model, return output buffer ─────────────────────────
static std::vector<float> processSine(TransformerModel<CPWLLeaf>& model,
                                       float freq, float amplitude,
                                       int warmup = kWarmupSamples,
                                       int analysis = kAnalysisSamples)
{
    const int total = warmup + analysis;
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

    // Return only the analysis portion
    return std::vector<float>(output.begin() + warmup, output.end());
}

// ── Init model with config ───────────────────────────────────────────────────
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

// ── Measure gain (dB) at a given frequency ───────────────────────────────────
static float measureGainDB(TransformerModel<CPWLLeaf>& model, float freq, float amplitude)
{
    model.reset();
    model.prepareToPlay(kSampleRate, kBlockSize);

    auto out = processSine(model, freq, amplitude);

    // Input RMS
    double inRms = amplitude / std::sqrt(2.0);

    // Output RMS
    double sumSq = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        sumSq += static_cast<double>(out[i]) * static_cast<double>(out[i]);
    double outRms = std::sqrt(sumSq / static_cast<double>(out.size()));

    if (inRms < 1e-30) return -200.0f;
    return static_cast<float>(20.0 * std::log10(outRms / inRms));
}

// =============================================================================
// 1. FR SWEEP at -20 dBu
// =============================================================================
static void diagFRSweep(const char* label, const TransformerConfig& cfg)
{
    TransformerModel<CPWLLeaf> model;
    initModel(model, cfg);

    const float amplitude = dBuToAmplitude(-20.0f);
    const float freqs[] = {10, 20, 30, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};

    // Measure 1kHz reference first
    float ref1k = measureGainDB(model, 1000.0f, amplitude);

    for (float f : freqs) {
        float gain = measureGainDB(model, f, amplitude);
        std::printf("[FR_SWEEP_%s] freq_hz=%.0f  level_dbu=-20  gain_db=%.4f  rel_1k_db=%.4f\n",
                    label, f, gain, gain - ref1k);
    }
}

// =============================================================================
// 2. THD vs LEVEL at 20 Hz
// =============================================================================
static void diagTHDvsLevel_20Hz(const char* label, const TransformerConfig& cfg)
{
    const float levels[] = {-30, -20, -10, 0, 4, 6, 10};

    for (float lvl : levels) {
        TransformerModel<CPWLLeaf> model;
        initModel(model, cfg);

        float amp = dBuToAmplitude(lvl);
        auto out = processSine(model, 20.0f, amp);
        auto thd = test::measureTHD(out.data(), static_cast<int>(out.size()), 20.0, kSampleRate);
        std::printf("[THD_VS_LEVEL_%s] freq_hz=20  level_dbu=%.0f  thd_pct=%.6f\n",
                    label, lvl, thd.thdPercent);
    }
}

// =============================================================================
// 3. THD vs LEVEL at 1 kHz
// =============================================================================
static void diagTHDvsLevel_1kHz(const char* label, const TransformerConfig& cfg)
{
    const float levels[] = {-30, -20, -10, 0, 4, 6, 10};

    for (float lvl : levels) {
        TransformerModel<CPWLLeaf> model;
        initModel(model, cfg);

        float amp = dBuToAmplitude(lvl);
        auto out = processSine(model, 1000.0f, amp);
        auto thd = test::measureTHD(out.data(), static_cast<int>(out.size()), 1000.0, kSampleRate);
        std::printf("[THD_VS_LEVEL_%s] freq_hz=1000  level_dbu=%.0f  thd_pct=%.6f\n",
                    label, lvl, thd.thdPercent);
    }
}

// =============================================================================
// 4. THD vs FREQUENCY at +4 dBu
// =============================================================================
static void diagTHDvsFreq(const char* label, const TransformerConfig& cfg)
{
    const float freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000};
    const float amp = dBuToAmplitude(4.0f);

    for (float f : freqs) {
        TransformerModel<CPWLLeaf> model;
        initModel(model, cfg);

        auto out = processSine(model, f, amp);
        auto thd = test::measureTHD(out.data(), static_cast<int>(out.size()), static_cast<double>(f), kSampleRate);
        std::printf("[THD_VS_FREQ_%s] freq_hz=%.0f  level_dbu=4  thd_pct=%.6f\n",
                    label, f, thd.thdPercent);
    }
}

// =============================================================================
// 5. HARMONIC SPECTRUM (H1-H8) at 0 dBu, 1kHz and 20Hz
// =============================================================================
static void diagHarmonicSpectrum(const char* label, const TransformerConfig& cfg)
{
    const float amp = dBuToAmplitude(0.0f);

    for (float freq : {1000.0f, 20.0f}) {
        TransformerModel<CPWLLeaf> model;
        initModel(model, cfg);

        auto out = processSine(model, freq, amp);
        auto thd = test::measureTHD(out.data(), static_cast<int>(out.size()),
                                     static_cast<double>(freq), kSampleRate, 8);

        for (int k = 0; k < 8; ++k) {
            double mag = thd.harmonicMag[k];
            double db = (mag > 1e-30) ? 20.0 * std::log10(mag) : -999.0;
            std::printf("[HARMONICS_%s] freq_hz=%.0f  level_dbu=0  H%d_mag=%.8f  H%d_db=%.2f\n",
                        label, freq, k + 1, mag, k + 1, db);
        }
        std::printf("[HARMONICS_%s] freq_hz=%.0f  level_dbu=0  thd_pct=%.6f\n",
                    label, freq, thd.thdPercent);
    }
}

// =============================================================================
// 6. CROSS-MODE HF COMPARISON
// =============================================================================
static void diagCrossMode()
{
    auto cfgFlat = makeConfigFlatLm();
    auto cfgFlux = makeConfigFlux();
    const float amp = dBuToAmplitude(-20.0f);
    const float freqs[] = {1000, 5000, 10000, 20000};

    for (float f : freqs) {
        TransformerModel<CPWLLeaf> modelFlat, modelFlux;
        initModel(modelFlat, cfgFlat);
        initModel(modelFlux, cfgFlux);

        float gainFlat = measureGainDB(modelFlat, f, amp);
        float gainFlux = measureGainDB(modelFlux, f, amp);

        std::printf("[CROSS_MODE] freq_hz=%.0f  gain_flat_db=%.4f  gain_flux_db=%.4f  delta_db=%.4f\n",
                    f, gainFlat, gainFlux, gainFlux - gainFlat);
    }
}

// =============================================================================
// 7. ADDITIONAL: Jensen Bertotti RMS across frequency (both modes)
// =============================================================================
static void diagBertottiRMS(const char* label, const TransformerConfig& cfg)
{
    const float amp = 0.5f;  // -6 dBFS, same as jensen_bertotti_freq test
    const float freqs[] = {100, 500, 1000, 5000, 10000};

    TransformerModel<CPWLLeaf> model;

    for (float f : freqs) {
        initModel(model, cfg);

        auto out = processSine(model, f, amp, 4096, 8192);
        double sumSq = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
            sumSq += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        double rmsOut = std::sqrt(sumSq / static_cast<double>(out.size()));

        std::printf("[BERTOTTI_RMS_%s] freq_hz=%.0f  amplitude=0.5  rms=%.8f\n",
                    label, f, rmsOut);
    }
}

// =============================================================================
// 8. Jensen harmonics at +6 dBFS (matches jensen_harmonics test)
// =============================================================================
static void diagJensenHarmonicsHot(const char* label, const TransformerConfig& cfg)
{
    const float amp = 2.0f;  // +6 dBFS
    const float freq = 1000.0f;

    TransformerModel<CPWLLeaf> model;
    initModel(model, cfg);

    auto out = processSine(model, freq, amp, 4096, 8192);
    auto thd = test::measureTHD(out.data(), static_cast<int>(out.size()), 1000.0, kSampleRate, 8);

    for (int k = 0; k < 5; ++k) {
        double mag = thd.harmonicMag[k];
        double db = (mag > 1e-30) ? 20.0 * std::log10(mag) : -999.0;
        std::printf("[HOT_HARMONICS_%s] H%d_mag=%.8f  H%d_db=%.2f\n",
                    label, k + 1, mag, k + 1, db);
    }
    std::printf("[HOT_HARMONICS_%s] thd_pct=%.6f\n", label, thd.thdPercent);
}

// =============================================================================
// 9. Nonlinear Lm: 50Hz/1kHz and 10Hz/1kHz relative gain at multiple levels
// =============================================================================
static void diagNonlinearLmGain(const char* label, const TransformerConfig& cfg)
{
    const float levels[] = {-50, -20, 0, 10, 20};
    const float testFreqs[] = {10, 20, 50, 100};

    for (float lvl : levels) {
        float amp = dBuToAmplitude(lvl);

        // Measure 1kHz reference
        TransformerModel<CPWLLeaf> modelRef;
        initModel(modelRef, cfg);
        float ref1k = measureGainDB(modelRef, 1000.0f, amp);

        for (float tf : testFreqs) {
            TransformerModel<CPWLLeaf> modelTest;
            initModel(modelTest, cfg);
            float gain = measureGainDB(modelTest, tf, amp);

            std::printf("[NONLIN_LM_%s] freq_hz=%.0f  level_dbu=%.0f  rel_1k_db=%.4f\n",
                        label, tf, lvl, gain - ref1k);
        }
    }
}

// =============================================================================
// MAIN
// =============================================================================
int main()
{
    std::printf("================================================================\n");
    std::printf("  FluxIntegrator Exploration Diagnostic\n");
    std::printf("  Jensen JT-115K-E — FlatLm vs ArtisticFlux\n");
    std::printf("================================================================\n\n");

    auto cfgFlat = makeConfigFlatLm();
    auto cfgFlux = makeConfigFlux();

    // 1. FR Sweep
    std::printf("--- FR Sweep at -20 dBu ---\n");
    diagFRSweep("FLAT", cfgFlat);
    std::printf("\n");
    diagFRSweep("FLUX", cfgFlux);

    // 2. THD vs Level at 20 Hz
    std::printf("\n--- THD vs Level at 20 Hz ---\n");
    diagTHDvsLevel_20Hz("FLAT", cfgFlat);
    std::printf("\n");
    diagTHDvsLevel_20Hz("FLUX", cfgFlux);

    // 3. THD vs Level at 1 kHz
    std::printf("\n--- THD vs Level at 1 kHz ---\n");
    diagTHDvsLevel_1kHz("FLAT", cfgFlat);
    std::printf("\n");
    diagTHDvsLevel_1kHz("FLUX", cfgFlux);

    // 4. THD vs Frequency at +4 dBu
    std::printf("\n--- THD vs Frequency at +4 dBu ---\n");
    diagTHDvsFreq("FLAT", cfgFlat);
    std::printf("\n");
    diagTHDvsFreq("FLUX", cfgFlux);

    // 5. Harmonic Spectrum at 0 dBu
    std::printf("\n--- Harmonic Spectrum at 0 dBu ---\n");
    diagHarmonicSpectrum("FLAT", cfgFlat);
    std::printf("\n");
    diagHarmonicSpectrum("FLUX", cfgFlux);

    // 6. Cross-mode HF Comparison
    std::printf("\n--- Cross-Mode HF Comparison at -20 dBu ---\n");
    diagCrossMode();

    // 7. Bertotti RMS across frequency
    std::printf("\n--- Bertotti RMS Across Frequency ---\n");
    diagBertottiRMS("FLAT", cfgFlat);
    std::printf("\n");
    diagBertottiRMS("FLUX", cfgFlux);

    // 8. Hot harmonics at +6 dBFS / 1kHz
    std::printf("\n--- Hot Harmonics at +6 dBFS / 1kHz ---\n");
    diagJensenHarmonicsHot("FLAT", cfgFlat);
    std::printf("\n");
    diagJensenHarmonicsHot("FLUX", cfgFlux);

    // 9. Nonlinear Lm relative gain
    std::printf("\n--- Nonlinear Lm Relative Gain ---\n");
    diagNonlinearLmGain("FLAT", cfgFlat);
    std::printf("\n");
    diagNonlinearLmGain("FLUX", cfgFlux);

    // ============================================================
    // Artistic MODE DIAGNOSTICS (correct FluxInt context)
    // ============================================================
    auto cfgArtisticFlux = makeConfigArtisticFlux();
    auto cfgArtisticFlat = makeConfigArtisticFlat();

    std::printf("\n\n================================================================\n");
    std::printf("  Artistic MODE: FlatLm vs ArtisticFlux\n");
    std::printf("================================================================\n\n");

    // 10. Artistic FR Sweep
    std::printf("--- Artistic FR Sweep at -20 dBu ---\n");
    diagFRSweep("ART_FLAT", cfgArtisticFlat);
    std::printf("\n");
    diagFRSweep("ART_FLUX", cfgArtisticFlux);

    // 11. Artistic THD vs Level at 20 Hz
    std::printf("\n--- Artistic THD vs Level at 20 Hz ---\n");
    diagTHDvsLevel_20Hz("ART_FLAT", cfgArtisticFlat);
    std::printf("\n");
    diagTHDvsLevel_20Hz("ART_FLUX", cfgArtisticFlux);

    // 12. Artistic THD vs Level at 1 kHz
    std::printf("\n--- Artistic THD vs Level at 1 kHz ---\n");
    diagTHDvsLevel_1kHz("ART_FLAT", cfgArtisticFlat);
    std::printf("\n");
    diagTHDvsLevel_1kHz("ART_FLUX", cfgArtisticFlux);

    // 13. Artistic THD vs Frequency at +4 dBu
    std::printf("\n--- Artistic THD vs Frequency at +4 dBu ---\n");
    diagTHDvsFreq("ART_FLAT", cfgArtisticFlat);
    std::printf("\n");
    diagTHDvsFreq("ART_FLUX", cfgArtisticFlux);

    // 14. Artistic Harmonic Spectrum
    std::printf("\n--- Artistic Harmonic Spectrum at 0 dBu ---\n");
    diagHarmonicSpectrum("ART_FLAT", cfgArtisticFlat);
    std::printf("\n");
    diagHarmonicSpectrum("ART_FLUX", cfgArtisticFlux);

    // 15. Artistic cross-mode HF
    std::printf("\n--- Artistic Cross-Mode HF at -20 dBu ---\n");
    {
        const float amp = dBuToAmplitude(-20.0f);
        const float freqs[] = {1000, 5000, 10000, 20000};
        for (float f : freqs) {
            TransformerModel<CPWLLeaf> mFlat, mFlux;
            initModel(mFlat, cfgArtisticFlat);
            initModel(mFlux, cfgArtisticFlux);
            float gFlat = measureGainDB(mFlat, f, amp);
            float gFlux = measureGainDB(mFlux, f, amp);
            std::printf("[ART_CROSS_MODE] freq_hz=%.0f  flat=%.4f  flux=%.4f  delta=%.4f\n",
                        f, gFlat, gFlux, gFlux - gFlat);
        }
    }

    std::printf("\n================================================================\n");
    std::printf("  Diagnostic complete.\n");
    std::printf("================================================================\n");

    return 0;
}
