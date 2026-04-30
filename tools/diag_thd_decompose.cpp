// =============================================================================
// diag_thd_decompose.cpp — Sprint 2 analytical re-baseline.
//
// Decomposes the cascade THD into contributions per stage by re-running each
// validate_jensen test point with selected components disabled:
//
//   col [base]   = full cascade (matches data/measurements/thd_*.csv)
//   col [noK2]   = cascade with material.K2 = 0 (test wedge hypothesis: the
//                  Bertotti excess-loss term K2·sign(dB/dt)·√|dB/dt| is the
//                  level-independent harmonics floor at LF/low signal)
//   col [noBer]  = cascade with K1 = K2 = 0 (no dynamic losses at all)
//   col [JAonly] = isolated HysteresisModel<LangevinPade> with H₀ matching
//                  the cascade's effective drive (no fluxInt, no HP, no LC,
//                  no Bertotti). THD measured on B = μ₀·(H + M).
//
// The tool reuses the dBu→amplitude helpers and test-point list from
// validate_jensen.cpp (kept in sync manually).
//
// Usage:
//   diag_thd_decompose [--out path.csv]
// =============================================================================

#include "../Tests/test_common.h"

#include <core/magnetics/AnhystereticFunctions.h>
#include <core/magnetics/CPWLLeaf.h>
#include <core/magnetics/HysteresisModel.h>
#include <core/model/TransformerConfig.h>
#include <core/model/TransformerModel.h>
#include <core/util/Constants.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace transfo;

namespace {

constexpr int   kBlockSize        = 512;
constexpr int   kRealtimeWarmup   = 8192;
constexpr int   kPhysicalWarmup   = 131072;
constexpr int   kAnalysis         = 65536;
constexpr float kSampleRate       = 44100.0f;

enum class Preset { JT115KE, JT11ELCF };
enum class Mode   { Realtime, Physical };

const char* presetTag(Preset p)  { return p == Preset::JT115KE ? "JT115KE" : "JT11ELCF"; }
const char* modeTag(Mode m)      { return m == Mode::Realtime ? "Realtime" : "Physical"; }

float dBuToAmplitudeDigital(float dBu) {
    return std::pow(10.0f, dBu / 20.0f) * 0.1f;
}
float dBuToAmplitude_TC1(float dBu) {
    constexpr float kVrms0dBu = 0.7746f;
    constexpr float kSqrt2    = 1.41421356f;
    constexpr float Rs        = 150.0f;
    constexpr float Rdc_pri   = 19.7f;
    constexpr float Rload_ref = 1500.0f;
    constexpr float Zi        = Rdc_pri + Rload_ref;
    constexpr float divider   = Zi / (Rs + Zi);
    return kVrms0dBu * std::pow(10.0f, dBu / 20.0f) * kSqrt2 * divider;
}
float dBuToAmplitude_ELCF(float dBu) {
    constexpr float kVrms0dBu = 0.7746f;
    constexpr float kSqrt2    = 1.41421356f;
    return kVrms0dBu * std::pow(10.0f, dBu / 20.0f) * kSqrt2;
}
float dBuToAmplitude(Preset p, Mode m, float dBu) {
    if (m == Mode::Realtime) return dBuToAmplitudeDigital(dBu);
    return (p == Preset::JT115KE) ? dBuToAmplitude_TC1(dBu) : dBuToAmplitude_ELCF(dBu);
}

TransformerConfig makeConfig(Preset p, Mode m) {
    TransformerConfig cfg = (p == Preset::JT115KE)
        ? TransformerConfig::Jensen_JT115KE()
        : TransformerConfig::Jensen_JT11ELCF();
    cfg.calibrationMode = (m == Mode::Physical)
        ? CalibrationMode::Physical
        : CalibrationMode::Artistic;
    return cfg;
}

// ── Variant: cascade with optional component disables ─────────────────────
struct Variant {
    const char* tag;
    bool zeroK1;
    bool zeroK2;
    bool zeroKgeo;     // K_geo = 0 disables dynamic-Lm HP coefficient modulation.
    bool noFluxInt;    // Force fluxIntegratorEnabled = false.
    int  osFactor;     // 0 = leave default; otherwise set on the model.
};

struct CascadeResult {
    double thd_percent;
    double peakH;     // Peak |H_applied| during analysis window (A/m).
};

CascadeResult runCascade(Preset p, Mode m, float freq, float dBu, const Variant& v) {
    TransformerConfig cfg = makeConfig(p, m);
    if (v.zeroK1) cfg.material.K1 = 0.0f;
    if (v.zeroK2) cfg.material.K2 = 0.0f;
    if (v.zeroKgeo) cfg.geometry.K_geo = 0.0f;
    if (v.noFluxInt) cfg.fluxIntegratorEnabled = false;

    TransformerModel<CPWLLeaf> model;
    model.setConfig(cfg);
    model.setProcessingMode(m == Mode::Realtime ? ProcessingMode::Realtime
                                                 : ProcessingMode::Physical);
    if (v.osFactor > 0) model.setOversamplingFactor(v.osFactor);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(kSampleRate, kBlockSize);
    model.reset();

    const int warmup   = (m == Mode::Realtime) ? kRealtimeWarmup : kPhysicalWarmup;
    const int analysis = kAnalysis;
    const float amp = dBuToAmplitude(p, m, dBu);
    std::vector<float> in(kBlockSize), out(kBlockSize);

    int sample = 0;
    for (; sample < warmup; sample += kBlockSize) {
        const int n = std::min(kBlockSize, warmup - sample);
        for (int i = 0; i < n; ++i)
            in[i] = amp * std::sin(2.0f * static_cast<float>(M_PI) * freq
                                   * static_cast<float>(sample + i) / kSampleRate);
        model.processBlock(in.data(), out.data(), n);
    }
    // Reset peakH counter so it tracks only the analysis window.
    (void)model.getPeakHApplied();

    std::vector<float> outBuf;
    outBuf.reserve(analysis);
    int collected = 0;
    int idx = warmup;
    while (collected < analysis) {
        const int n = std::min(kBlockSize, analysis - collected);
        for (int i = 0; i < n; ++i)
            in[i] = amp * std::sin(2.0f * static_cast<float>(M_PI) * freq
                                   * static_cast<float>(idx + i) / kSampleRate);
        model.processBlock(in.data(), out.data(), n);
        for (int i = 0; i < n; ++i) outBuf.push_back(out[i]);
        idx += n;
        collected += n;
    }

    auto thd = test::measureTHD(outBuf.data(), static_cast<int>(outBuf.size()),
                                freq, kSampleRate, 9);
    CascadeResult r{};
    r.thd_percent = thd.thdPercent;
    r.peakH = static_cast<double>(model.getPeakHApplied());
    return r;
}

// ── Compute hScale that the cascade would use for this (preset, mode) ─────
// Mirrors TransformerModel::configureCircuit().
struct CascadeCalibration {
    double hScale;     // [A/m per V_peak] (after fluxInt if Physical)
    double f_ref;
    bool   fluxIntegrate;
};

CascadeCalibration calibrationFor(const TransformerConfig& cfg, Mode m) {
    CascadeCalibration c{};
    c.f_ref = static_cast<double>(cfg.calibrationFreqHz);
    c.fluxIntegrate = (m == Mode::Physical);

    if (m == Mode::Physical) {
        const float N_pri = (cfg.windings.N_primary > 0)
            ? static_cast<float>(cfg.windings.N_primary)
            : cfg.estimateNprimary();
        const float l_e = cfg.core.effectiveLength();
        const float Lm  = cfg.windings.Lp_primary;
        if (Lm > 0.0f && l_e > 0.0f && c.f_ref > 0.0f) {
            c.hScale = static_cast<double>(N_pri)
                     / (2.0 * M_PI * c.f_ref * static_cast<double>(Lm)
                        * static_cast<double>(l_e));
        } else {
            c.hScale = static_cast<double>(cfg.material.a) * 5.0;
        }
    } else {
        c.hScale = static_cast<double>(cfg.material.a) * 5.0;
    }
    return c;
}

// ── Drive HysteresisModel<LangevinPade> in isolation with empirical H₀ ───
// H₀ comes from the cascade's measured peakH (so we don't have to recreate
// the integrator/hScale calibration analytically — bug-resistant).
double runIsolatedJA(Preset p, Mode m, float freq, double H0) {
    auto cfg = makeConfig(p, m);

    HysteresisModel<LangevinPade> ja;
    ja.setParameters(cfg.material);
    ja.setSampleRate(kSampleRate);
    ja.setMaxIterations(40);
    ja.setTolerance(1e-9);
    ja.reset();

    const int totalCycles = 16;
    const int analysisCycle = 12;
    const int totalSamples    = static_cast<int>(totalCycles * kSampleRate / freq);
    const int analysisStart   = static_cast<int>(analysisCycle * kSampleRate / freq);
    const int analysisLen     = totalSamples - analysisStart;

    std::vector<float> bBuf;
    bBuf.reserve(analysisLen);

    for (int n = 0; n < totalSamples; ++n) {
        const double t = n / static_cast<double>(kSampleRate);
        const double H = H0 * std::sin(2.0 * M_PI * static_cast<double>(freq) * t);
        const double M = ja.solveImplicitStep(H);
        ja.commitState();
        if (n >= analysisStart) {
            const double B = kMu0 * (H + M);
            bBuf.push_back(static_cast<float>(B));
        }
    }

    auto thd = test::measureTHD(bBuf.data(), static_cast<int>(bBuf.size()),
                                freq, kSampleRate, 9);
    return thd.thdPercent;
}

struct TestPoint {
    Preset preset;
    Mode   mode;
    float  freq_hz;
    float  level_dBu;
};

const std::vector<TestPoint> kPoints = {
    // JT-115K-E
    { Preset::JT115KE, Mode::Realtime,    20.0f, -20.0f },
    { Preset::JT115KE, Mode::Realtime,    20.0f,  -2.5f },
    { Preset::JT115KE, Mode::Realtime,    20.0f,  +1.2f },
    { Preset::JT115KE, Mode::Realtime,  1000.0f, -20.0f },
    { Preset::JT115KE, Mode::Realtime,  1000.0f,  +4.0f },
    { Preset::JT115KE, Mode::Physical,    20.0f, -20.0f },
    { Preset::JT115KE, Mode::Physical,    20.0f,  -2.5f },
    { Preset::JT115KE, Mode::Physical,    20.0f,  +1.2f },
    { Preset::JT115KE, Mode::Physical,  1000.0f, -20.0f },
    { Preset::JT115KE, Mode::Physical,  1000.0f,  +4.0f },
    // JT-11ELCF
    { Preset::JT11ELCF, Mode::Realtime, 1000.0f,  +4.0f },
    { Preset::JT11ELCF, Mode::Realtime,   50.0f,  +4.0f },
    { Preset::JT11ELCF, Mode::Realtime,   20.0f,  +4.0f },
    { Preset::JT11ELCF, Mode::Realtime,   20.0f, +24.0f },
    { Preset::JT11ELCF, Mode::Physical, 1000.0f,  +4.0f },
    { Preset::JT11ELCF, Mode::Physical,   50.0f,  +4.0f },
    { Preset::JT11ELCF, Mode::Physical,   20.0f,  +4.0f },
    { Preset::JT11ELCF, Mode::Physical,   20.0f, +24.0f },
};

} // namespace

int main(int argc, char** argv) {
    const char* outPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outPath = argv[++i];
    }

    std::printf("=== Sprint 2 THD decomposition ===\n");
    std::printf("Compares cascade THD vs cascade-without-Bertotti vs isolated J-A.\n");
    std::printf("All values are THD %% on the analysis window.\n\n");

    Variant base       { "base",      false, false, false, false, 0 };
    Variant noBer      { "noBer",     true,  true,  false, false, 0 };
    Variant noDynLm    { "noDynLm",   false, false, true,  false, 0 };
    Variant os1        { "os1",       false, false, false, false, 1 };  // No OS in Physical
    Variant noAll      { "noAll",     true,  true,  true,  true,  1 };  // Everything off + no OS

    std::ofstream csv;
    if (outPath) {
        csv.open(outPath);
        csv << "preset,mode,freq_hz,level_dbu,thd_base,thd_noBer,thd_noDynLm,thd_os1,thd_noAll,peak_H,thd_JAonly\n";
    }

    std::printf("%-9s %-8s %7s %6s |  %8s  %8s  %8s  %8s  %8s | %9s | %8s\n",
                "preset", "mode", "f_Hz", "dBu",
                "base", "noBer", "noDynLm", "os1", "noAll", "peakH", "JAonly");
    std::printf("%s\n", std::string(125, '-').c_str());

    for (const auto& tp : kPoints) {
        const auto r_base      = runCascade(tp.preset, tp.mode, tp.freq_hz, tp.level_dBu, base);
        const auto r_noBer     = runCascade(tp.preset, tp.mode, tp.freq_hz, tp.level_dBu, noBer);
        const auto r_noDynLm   = runCascade(tp.preset, tp.mode, tp.freq_hz, tp.level_dBu, noDynLm);
        const auto r_os1       = runCascade(tp.preset, tp.mode, tp.freq_hz, tp.level_dBu, os1);
        const auto r_noAll     = runCascade(tp.preset, tp.mode, tp.freq_hz, tp.level_dBu, noAll);
        const double t_JAonly  = runIsolatedJA(tp.preset, tp.mode, tp.freq_hz, r_base.peakH);

        std::printf("%-9s %-8s %7.0f %+6.1f | %8.4f%% %8.4f%% %8.4f%% %8.4f%% %8.4f%% | %9.3e | %7.4f%%\n",
                    presetTag(tp.preset), modeTag(tp.mode), tp.freq_hz, tp.level_dBu,
                    r_base.thd_percent, r_noBer.thd_percent, r_noDynLm.thd_percent,
                    r_os1.thd_percent, r_noAll.thd_percent,
                    r_base.peakH, t_JAonly);
        if (csv.is_open()) {
            csv << presetTag(tp.preset) << ',' << modeTag(tp.mode) << ','
                << tp.freq_hz << ',' << tp.level_dBu << ','
                << r_base.thd_percent << ',' << r_noBer.thd_percent << ','
                << r_noDynLm.thd_percent << ',' << r_os1.thd_percent << ','
                << r_noAll.thd_percent << ',' << r_base.peakH << ','
                << t_JAonly << "\n";
        }
    }

    if (csv.is_open()) {
        std::printf("\nWrote %s\n", outPath);
    }
    std::printf("\nReading guide:\n"
                "  noBer    ≈ base    → Bertotti is negligible (already known)\n"
                "  noDynLm  << base   → dynamic-Lm HP modulation is the wedge cause\n"
                "  noFluxInt << base  → integrate/differentiate chain is the cause\n"
                "  noAll    ≈ JAonly  → these 3 stages explain the cascade-vs-JA gap\n");
    return 0;
}
