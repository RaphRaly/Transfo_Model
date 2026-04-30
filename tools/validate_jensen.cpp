// =============================================================================
// validate_jensen.cpp — Sprint 1 baseline measurement pipeline.
//
// Sweeps the Jensen JT-115K-E and JT-11ELCF reference points required by
// docs/VALIDATION_REPORT.md, in BOTH calibration modes (Artistic/Realtime and
// Physical), and writes results to data/measurements/<file>_<date>.csv.
//
// Outputs:
//   data/measurements/thd_JT115KE_<YYYY-MM-DD>.csv
//   data/measurements/thd_JT11ELCF_<YYYY-MM-DD>.csv
//   data/measurements/fr_JT115KE_<YYYY-MM-DD>.csv
//   data/measurements/fr_JT11ELCF_<YYYY-MM-DD>.csv
//
// Usage:
//   validate_jensen [--smoke] [--out-dir <path>] [--date <YYYY-MM-DD>]
//
//   --smoke      Run a 3-point subset (used by CTest test_validation_smoke).
//   --out-dir    Override output directory (default: data/measurements).
//   --date       Override date suffix (default: today, system local time).
//
// Reuses:
//   - test::measureTHD / goertzelMagnitude from Tests/test_common.h
//   - dBu↔amplitude helpers mirrored from Tests/test_thd_validation.cpp
// =============================================================================

#include "../Tests/test_common.h"

#include <core/magnetics/CPWLLeaf.h>
#include <core/model/TransformerConfig.h>
#include <core/model/TransformerModel.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace transfo;

// ── Constants ───────────────────────────────────────────────────────────────
namespace {
constexpr int kBlockSize          = 512;
constexpr int kRealtimeWarmup     = 8192;
constexpr int kRealtimeAnalysis   = 65536;
constexpr int kPhysicalWarmup     = 131072;
constexpr int kPhysicalAnalysis   = 65536;
constexpr int kFRWarmupRealtime   = 8192;
constexpr int kFRWarmupPhysical   = 65536;
constexpr int kFRMinAnalysis      = 16384;
constexpr float kSampleRate       = 44100.0f;

// FR sweep level: deeply linear region so the response measures the LTI
// transfer, not nonlinear gain compression.
constexpr float kFRLevel_dBu      = -40.0f;

// ── dBu → digital amplitude (cascade Artistic mode) ────────────────────────
// Matches Tests/test_thd_validation.cpp::dBuToAmplitude. The Artistic cascade
// uses hScale = a*5 with a digital amplitude convention (0 dBu ≈ 0.1 peak).
float dBuToAmplitudeDigital(float dBu) {
    return std::pow(10.0f, dBu / 20.0f) * 0.1f;
}

// ── dBu → physical V_peak at primary, JT-115K-E TC1 ────────────────────────
// Test Circuit 1: Rs = 150 Ω, Z_in ≈ Rdc + Rload_reflected.
// Mirrors Tests/test_thd_validation.cpp::dBuToAmplitude_TC1.
float dBuToAmplitude_TC1(float dBu) {
    constexpr float kVrms0dBu = 0.7746f;
    constexpr float kSqrt2    = 1.41421356f;
    constexpr float Rs        = 150.0f;
    constexpr float Rdc_pri   = 19.7f;
    constexpr float Rload_ref = 1500.0f;
    constexpr float Zi        = Rdc_pri + Rload_ref;
    constexpr float divider   = Zi / (Rs + Zi);
    float Vrms = kVrms0dBu * std::pow(10.0f, dBu / 20.0f);
    return Vrms * kSqrt2 * divider;
}

// ── dBu → physical V_peak at primary, JT-11ELCF (Rs = 0) ───────────────────
float dBuToAmplitude_ELCF(float dBu) {
    constexpr float kVrms0dBu = 0.7746f;
    constexpr float kSqrt2    = 1.41421356f;
    float Vrms = kVrms0dBu * std::pow(10.0f, dBu / 20.0f);
    return Vrms * kSqrt2;
}

enum class Preset { JT115KE, JT11ELCF };
enum class Mode   { Realtime, Physical };

const char* presetTag(Preset p)  { return p == Preset::JT115KE ? "JT115KE" : "JT11ELCF"; }
const char* presetName(Preset p) { return p == Preset::JT115KE ? "Jensen JT-115K-E" : "Jensen JT-11ELCF"; }
const char* modeTag(Mode m)      { return m == Mode::Realtime ? "Realtime" : "Physical"; }

TransformerConfig makeConfig(Preset p, Mode m) {
    TransformerConfig cfg = (p == Preset::JT115KE)
        ? TransformerConfig::Jensen_JT115KE()
        : TransformerConfig::Jensen_JT11ELCF();
    cfg.calibrationMode = (m == Mode::Physical)
        ? CalibrationMode::Physical
        : CalibrationMode::Artistic;
    return cfg;
}

float dBuToAmplitude(Preset p, Mode m, float dBu) {
    if (m == Mode::Realtime) return dBuToAmplitudeDigital(dBu);
    return (p == Preset::JT115KE) ? dBuToAmplitude_TC1(dBu)
                                   : dBuToAmplitude_ELCF(dBu);
}

// ── Run a single sine through the model, return THD result + measured RMS ──
struct PointResult {
    test::THDResult thd;
    double inRms  = 0.0;
    double outRms = 0.0;
    double gainDb = 0.0;
};

PointResult runPoint(Preset p, Mode m, float freq, float dBu) {
    TransformerModel<CPWLLeaf> model;
    model.setConfig(makeConfig(p, m));
    model.setProcessingMode(m == Mode::Realtime ? ProcessingMode::Realtime
                                                 : ProcessingMode::Physical);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(kSampleRate, kBlockSize);
    model.reset();
    model.prepareToPlay(kSampleRate, kBlockSize);

    const int warmup   = (m == Mode::Realtime) ? kRealtimeWarmup   : kPhysicalWarmup;
    const int analysis = (m == Mode::Realtime) ? kRealtimeAnalysis : kPhysicalAnalysis;
    const int total    = warmup + analysis;

    const float amplitude = dBuToAmplitude(p, m, dBu);
    const float w = 2.0f * static_cast<float>(test::kPi) * freq / kSampleRate;

    std::vector<float> input(static_cast<size_t>(total));
    std::vector<float> output(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        input[static_cast<size_t>(i)] =
            amplitude * std::sin(w * static_cast<float>(i));
    }

    int offset = 0, remaining = total;
    while (remaining > 0) {
        int b = std::min(remaining, kBlockSize);
        model.processBlock(input.data() + offset, output.data() + offset, b);
        offset    += b;
        remaining -= b;
    }

    PointResult r;
    r.thd = test::measureTHD(output.data() + warmup, analysis,
                              static_cast<double>(freq),
                              static_cast<double>(kSampleRate));
    r.inRms  = test::computeRMS(input.data()  + warmup, analysis);
    r.outRms = test::computeRMS(output.data() + warmup, analysis);
    r.gainDb = test::amplitudeToDb(r.outRms / (r.inRms + 1e-30));
    return r;
}

// ── FR sweep: log-spaced, returns {freq, magnitude_dB} normalized to 1 kHz ──
struct FRPoint { double freq; double mag_db; };

std::vector<FRPoint> runFRSweep(Preset p, Mode m, double fLo, double fHi, int nPoints) {
    std::vector<FRPoint> out;
    out.reserve(static_cast<size_t>(nPoints));

    TransformerModel<CPWLLeaf> model;
    model.setConfig(makeConfig(p, m));
    model.setProcessingMode(m == Mode::Realtime ? ProcessingMode::Realtime
                                                 : ProcessingMode::Physical);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(kSampleRate, kBlockSize);

    const int warmup = (m == Mode::Realtime) ? kFRWarmupRealtime : kFRWarmupPhysical;
    const float amplitude = dBuToAmplitude(p, m, kFRLevel_dBu);

    auto measureAt = [&](double freq) -> double {
        // Need at least kFRMinAnalysis samples AND >= 30 cycles at this freq
        // for a reliable Goertzel magnitude.
        int analysis = std::max(kFRMinAnalysis,
                                static_cast<int>(30.0 * kSampleRate / freq));
        analysis = std::min(analysis, 131072);
        const int total = warmup + analysis;

        std::vector<float> input(static_cast<size_t>(total));
        std::vector<float> output(static_cast<size_t>(total));
        const float w = 2.0f * static_cast<float>(test::kPi)
                      * static_cast<float>(freq) / kSampleRate;
        for (int i = 0; i < total; ++i) {
            input[static_cast<size_t>(i)] =
                amplitude * std::sin(w * static_cast<float>(i));
        }

        model.reset();
        model.prepareToPlay(kSampleRate, kBlockSize);

        int off = 0, rem = total;
        while (rem > 0) {
            int b = std::min(rem, kBlockSize);
            model.processBlock(input.data() + off, output.data() + off, b);
            off += b; rem -= b;
        }
        const double H1_out = test::goertzelMagnitude(
            output.data() + warmup, analysis, freq,
            static_cast<double>(kSampleRate));
        const double H1_in  = test::goertzelMagnitude(
            input.data()  + warmup, analysis, freq,
            static_cast<double>(kSampleRate));
        return (H1_in > 1e-30 && H1_out > 1e-30)
               ? 20.0 * std::log10(H1_out / H1_in)
               : -999.0;
    };

    // Build the log-spaced grid first so we can ratio against 1 kHz at the end.
    std::vector<double> freqs(static_cast<size_t>(nPoints));
    const double logLo = std::log10(fLo);
    const double logHi = std::log10(fHi);
    for (int i = 0; i < nPoints; ++i) {
        const double t = static_cast<double>(i) / (nPoints - 1);
        freqs[static_cast<size_t>(i)] = std::pow(10.0, logLo + t * (logHi - logLo));
    }

    // Reference at 1 kHz (added explicitly so the grid is normalised on a
    // measured point, not a log-grid neighbour that may not land exactly).
    const double refDb = measureAt(1000.0);

    for (double f : freqs) {
        const double abs_db = measureAt(f);
        const double rel_db = (abs_db > -990.0 && refDb > -990.0)
                              ? abs_db - refDb : -999.0;
        out.push_back({f, rel_db});
    }
    return out;
}

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

// ── THD CSV writer ──────────────────────────────────────────────────────────
struct THDPoint {
    Mode  mode;
    float freq_hz;
    float level_dbu;
};

void writeTHDCsv(const std::filesystem::path& path, Preset p,
                  const std::vector<THDPoint>& points) {
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open %s for writing\n",
                     path.string().c_str());
        return;
    }
    f << "preset,mode,freq_hz,level_dbu,amplitude,thd_percent,gain_db,"
         "h1_db,h2_db,h3_db,h4_db,h5_db,h6_db,h7_db,h8_db,h9_db,h10_db\n";
    f.setf(std::ios::scientific);
    f.precision(6);

    for (const auto& pt : points) {
        std::printf("  [%s/%s] %6.0f Hz / %+5.1f dBu ... ",
                    presetTag(p), modeTag(pt.mode), pt.freq_hz, pt.level_dbu);
        std::fflush(stdout);

        auto r = runPoint(p, pt.mode, pt.freq_hz, pt.level_dbu);
        const float amp = dBuToAmplitude(p, pt.mode, pt.level_dbu);

        std::printf("THD=%.4f%%  G=%+.2f dB\n",
                    r.thd.thdPercent, r.gainDb);

        f << presetTag(p) << ',' << modeTag(pt.mode) << ','
          << pt.freq_hz << ',' << pt.level_dbu << ',' << amp << ','
          << r.thd.thdPercent << ',' << r.gainDb;
        for (int k = 0; k < 10; ++k) f << ',' << r.thd.harmonicDB[k];
        f << '\n';
    }
}

void writeFRCsv(const std::filesystem::path& path, Preset p,
                 const std::vector<Mode>& modes,
                 double fLo, double fHi, int nPoints) {
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open %s for writing\n",
                     path.string().c_str());
        return;
    }
    f << "preset,mode,frequency_hz,magnitude_db\n";
    f.setf(std::ios::scientific);
    f.precision(6);

    for (Mode m : modes) {
        std::printf("  [%s/%s] FR sweep %.1f-%.0f Hz (%d pts) ... ",
                    presetTag(p), modeTag(m), fLo, fHi, nPoints);
        std::fflush(stdout);
        auto sweep = runFRSweep(p, m, fLo, fHi, nPoints);
        std::printf("done\n");
        for (const auto& s : sweep) {
            f << presetTag(p) << ',' << modeTag(m) << ','
              << s.freq << ',' << s.mag_db << '\n';
        }
    }
}

// ── Reference point lists from VALIDATION_REPORT.md ─────────────────────────
std::vector<THDPoint> jt115ke_points(bool smoke) {
    std::vector<THDPoint> base;
    // 5 datasheet points × 2 modes = 10 rows
    for (Mode m : {Mode::Realtime, Mode::Physical}) {
        base.push_back({m, 20.0f,    -20.0f});
        base.push_back({m, 20.0f,    -2.5f });
        base.push_back({m, 20.0f,    +1.2f });
        base.push_back({m, 1000.0f,  -20.0f});
        base.push_back({m, 1000.0f,  +4.0f });
    }
    if (!smoke) return base;
    // Smoke: one Realtime + one Physical = 2 rows
    return { base[0], base[5] };
}

std::vector<THDPoint> jt11elcf_points(bool smoke) {
    std::vector<THDPoint> base;
    for (Mode m : {Mode::Realtime, Mode::Physical}) {
        base.push_back({m, 1000.0f, +4.0f });
        base.push_back({m, 50.0f,   +4.0f });
        base.push_back({m, 20.0f,   +4.0f });
        base.push_back({m, 20.0f,   +24.0f});
    }
    if (!smoke) return base;
    return { base[0] };
}

} // namespace

int main(int argc, char* argv[]) {
    bool        smoke   = false;
    std::string outDir  = "data/measurements";
    std::string dateStr = today();

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) {
            smoke = true;
        } else if (std::strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            outDir = argv[++i];
        } else if (std::strcmp(argv[i], "--date") == 0 && i + 1 < argc) {
            dateStr = argv[++i];
        } else {
            std::fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            return 2;
        }
    }

    std::filesystem::create_directories(outDir);

    std::printf("validate_jensen — Sprint 1 baseline (smoke=%d, date=%s)\n",
                smoke ? 1 : 0, dateStr.c_str());
    std::printf("Output directory: %s\n", outDir.c_str());

    // ── THD ─────────────────────────────────────────────────────────────────
    {
        auto pts = jt115ke_points(smoke);
        std::printf("\n[1/4] JT-115K-E THD sweep (%zu points)\n", pts.size());
        std::filesystem::path p = std::filesystem::path(outDir)
            / ("thd_JT115KE_" + dateStr + ".csv");
        writeTHDCsv(p, Preset::JT115KE, pts);
        std::printf("Wrote %s\n", p.string().c_str());
    }
    {
        auto pts = jt11elcf_points(smoke);
        std::printf("\n[2/4] JT-11ELCF THD sweep (%zu points)\n", pts.size());
        std::filesystem::path p = std::filesystem::path(outDir)
            / ("thd_JT11ELCF_" + dateStr + ".csv");
        writeTHDCsv(p, Preset::JT11ELCF, pts);
        std::printf("Wrote %s\n", p.string().c_str());
    }

    // ── FR sweep ────────────────────────────────────────────────────────────
    // Smoke run uses a coarse grid to keep CTest fast.
    const int    nPts = smoke ? 9    : 41;
    const double fLo  = 10.0;
    // Cap at 0.45 fs so the bin stays comfortably below Nyquist.
    const double fHi  = std::min(100000.0, 0.45 * static_cast<double>(kSampleRate));
    {
        std::printf("\n[3/4] JT-115K-E FR sweep\n");
        std::filesystem::path p = std::filesystem::path(outDir)
            / ("fr_JT115KE_" + dateStr + ".csv");
        std::vector<Mode> modes = smoke ? std::vector<Mode>{Mode::Realtime}
                                         : std::vector<Mode>{Mode::Realtime, Mode::Physical};
        writeFRCsv(p, Preset::JT115KE, modes, fLo, fHi, nPts);
        std::printf("Wrote %s\n", p.string().c_str());
    }
    {
        std::printf("\n[4/4] JT-11ELCF FR sweep\n");
        std::filesystem::path p = std::filesystem::path(outDir)
            / ("fr_JT11ELCF_" + dateStr + ".csv");
        std::vector<Mode> modes = smoke ? std::vector<Mode>{Mode::Realtime}
                                         : std::vector<Mode>{Mode::Realtime, Mode::Physical};
        writeFRCsv(p, Preset::JT11ELCF, modes, fLo, fHi, nPts);
        std::printf("Wrote %s\n", p.string().c_str());
    }

    std::printf("\nDone.\n");
    return 0;
}
