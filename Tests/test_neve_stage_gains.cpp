// =============================================================================
// Per-stage gain diagnostic for NeveClassAPath
//
// Drives Q1, Q2, Q3 independently and in cascade to find where the 5.82×
// discrepancy between getGainInstantaneous() and actual WDF gain originates.
// =============================================================================
#include <cmath>
#include <cstdio>
#include "core/preamp/CEStageWDF.h"
#include "core/preamp/EFStageWDF.h"
#include "core/preamp/NeveClassAPath.h"
#include "core/preamp/GainTable.h"
#include "core/model/PreampConfig.h"

static constexpr float kSampleRate = 96000.0f;
static constexpr float kFreq       = 1000.0f;
static constexpr float kAmp        = 0.001f;   // 1 mV
static constexpr float kTs         = 1.0f / kSampleRate;
static constexpr float kPi         = 3.14159265358979f;

// Measure RMS gain over steady-state cycles
struct GainResult {
    double rmsIn, rmsOut, gainLinear, gainDB;
};

template <typename F>
static GainResult measureGain(F& processFunc, int totalSamples = 4000, int skipSamples = 1000)
{
    double sumSqIn = 0, sumSqOut = 0;
    for (int i = 0; i < totalSamples; ++i) {
        float t = static_cast<float>(i) * kTs;
        float input = kAmp * std::sin(2.0f * kPi * kFreq * t);
        float output = processFunc(input);
        if (i >= skipSamples) {
            sumSqIn  += static_cast<double>(input)  * input;
            sumSqOut += static_cast<double>(output) * output;
        }
    }
    int n = totalSamples - skipSamples;
    double rmsIn  = std::sqrt(sumSqIn / n);
    double rmsOut = std::sqrt(sumSqOut / n);
    double gain   = rmsOut / (rmsIn + 1e-30);
    return { rmsIn, rmsOut, gain, 20.0 * std::log10(gain + 1e-30) };
}

int main()
{
    transfo::NevePathConfig config;

    printf("=== Per-Stage Gain Diagnostic ===\n\n");
    printf("Config: Rc1=%g, Re1(Rg)=47, Rc2=%g, Re2=%g\n",
           config.R_collector_q1, config.R_collector_q2, config.R_emitter_q2);

    // ─── Q1 Standalone ───────────────────────────────────────────────────────
    {
        printf("\n--- Q1 (BC184C NPN CE, Rc=%g, Re=47) ---\n", config.R_collector_q1);
        transfo::CEStageWDF<transfo::BJTLeaf> q1;
        transfo::CEStageConfig q1Cfg;
        q1Cfg.bjt          = config.q1;
        q1Cfg.R_collector  = config.R_collector_q1;
        q1Cfg.R_emitter    = 47.0f;             // Rg
        q1Cfg.R_base_bias  = config.R_bias_base_q1;
        q1Cfg.C_input      = config.C_input;     // 100 µF
        q1Cfg.C_miller     = config.C_miller;    // 100 pF
        q1Cfg.C_bypass     = 0.0f;
        q1Cfg.Vcc          = config.Vcc;
        q1.prepare(kSampleRate, q1Cfg);

        float gm  = q1.getGm();
        float Ic  = q1.getIc();
        float analytical = q1.getGainInstantaneous();
        printf("  Ic = %.4f mA, gm = %.3f mS\n", Ic * 1000, gm * 1000);
        printf("  getGainInstantaneous() = %.2f (%.1f dB)\n",
               analytical, 20.0f * std::log10(std::abs(analytical) + 1e-30f));

        auto proc = [&](float in) { return q1.processSample(in); };
        auto r = measureGain(proc);
        printf("  Actual WDF RMS gain    = %.2f (%.1f dB)\n", r.gainLinear, r.gainDB);
        printf("  Ratio actual/analytical = %.3f\n",
               r.gainLinear / (std::abs(analytical) + 1e-30f));

        // Also print Vc_dc and degeneration factor
        float degen = 1.0f / (1.0f + gm * 47.0f);
        float undegen_gain = gm * config.R_collector_q1;
        printf("  degen factor = %.4f, undegen gain = %.1f\n", degen, undegen_gain);
        printf("  undegen * degen = %.2f (should match analytical)\n",
               undegen_gain * degen);
    }

    // ─── Q2 Standalone ───────────────────────────────────────────────────────
    {
        printf("\n--- Q2 (BC214C PNP CE, Rc=%g, Re=%g) ---\n",
               config.R_collector_q2, config.R_emitter_q2);
        transfo::CEStageWDF<transfo::BJTLeaf> q2;
        transfo::CEStageConfig q2Cfg;
        q2Cfg.bjt          = config.q2;
        q2Cfg.R_collector  = config.R_collector_q2;
        q2Cfg.R_emitter    = config.R_emitter_q2;
        q2Cfg.R_base_bias  = config.R_collector_q1;
        q2Cfg.C_input      = 0.0f;              // DC coupled
        q2Cfg.C_miller     = 0.0f;
        q2Cfg.C_bypass     = 0.0f;
        q2Cfg.Vcc          = config.Vcc;
        q2.prepare(kSampleRate, q2Cfg);

        float gm  = q2.getGm();
        float Ic  = q2.getIc();
        float analytical = q2.getGainInstantaneous();
        printf("  Ic = %.4f mA, gm = %.3f mS\n", Ic * 1000, gm * 1000);
        printf("  getGainInstantaneous() = %.4f (%.1f dB)\n",
               analytical, 20.0f * std::log10(std::abs(analytical) + 1e-30f));

        // Drive Q2 with a larger signal since its gain < 1
        auto proc = [&](float in) { return q2.processSample(in); };
        auto r = measureGain(proc);
        printf("  Actual WDF RMS gain    = %.4f (%.1f dB)\n", r.gainLinear, r.gainDB);
        printf("  Ratio actual/analytical = %.3f\n",
               r.gainLinear / (std::abs(analytical) + 1e-30f));

        float degen = 1.0f / (1.0f + gm * config.R_emitter_q2);
        float undegen_gain = gm * config.R_collector_q2;
        printf("  degen factor = %.6f, undegen gain = %.1f\n", degen, undegen_gain);
        printf("  undegen * degen = %.4f (should match analytical)\n",
               undegen_gain * degen);
    }

    // ─── Q3 Standalone ───────────────────────────────────────────────────────
    {
        printf("\n--- Q3 (BD139 NPN EF) ---\n");
        transfo::EFStageWDF<transfo::BJTLeaf> q3;
        transfo::EFStageConfig q3Cfg;
        q3Cfg.bjt          = config.q3;
        q3Cfg.R_bias       = config.R_bias_q3;
        q3Cfg.C_out        = config.C_out;
        q3Cfg.C_out_film   = config.C_out_film;
        q3Cfg.R_series_out = config.R_series_out;
        q3Cfg.Vcc          = config.Vcc;
        q3.prepare(kSampleRate, q3Cfg);

        float gm = q3.getGm();
        float Ic = q3.getIc();
        printf("  Ic = %.3f mA, gm = %.1f mS\n", Ic * 1000, gm * 1000);

        auto proc = [&](float in) { return q3.processSample(in); };
        auto r = measureGain(proc);
        printf("  Actual WDF gain = %.4f (%.2f dB) [expected ~1.0]\n",
               r.gainLinear, r.gainDB);
    }

    // ─── Q1→Q2 Cascade ──────────────────────────────────────────────────────
    {
        printf("\n--- Q1→Q2 Cascade ---\n");
        transfo::CEStageWDF<transfo::BJTLeaf> q1, q2;

        transfo::CEStageConfig q1Cfg;
        q1Cfg.bjt = config.q1; q1Cfg.R_collector = config.R_collector_q1;
        q1Cfg.R_emitter = 47.0f; q1Cfg.R_base_bias = config.R_bias_base_q1;
        q1Cfg.C_input = config.C_input; q1Cfg.C_miller = config.C_miller;
        q1Cfg.C_bypass = 0.0f; q1Cfg.Vcc = config.Vcc;
        q1.prepare(kSampleRate, q1Cfg);

        transfo::CEStageConfig q2Cfg;
        q2Cfg.bjt = config.q2; q2Cfg.R_collector = config.R_collector_q2;
        q2Cfg.R_emitter = config.R_emitter_q2; q2Cfg.R_base_bias = config.R_collector_q1;
        q2Cfg.C_input = 0.0f; q2Cfg.C_miller = 0.0f;
        q2Cfg.C_bypass = 0.0f; q2Cfg.Vcc = config.Vcc;
        q2.prepare(kSampleRate, q2Cfg);

        float Aol_analytical = q1.getGainInstantaneous() * q2.getGainInstantaneous();
        printf("  Analytical Aol = %.2f (%.1f dB)\n",
               Aol_analytical, 20.0f * std::log10(std::abs(Aol_analytical) + 1e-30f));

        auto proc = [&](float in) {
            float v1 = q1.processSample(in);
            float v2 = q2.processSample(v1);
            return v2;
        };
        auto r = measureGain(proc);
        printf("  Actual WDF cascade gain = %.2f (%.1f dB)\n", r.gainLinear, r.gainDB);
        printf("  Ratio actual/analytical = %.3f\n",
               r.gainLinear / (std::abs(Aol_analytical) + 1e-30f));
    }

    // ─── Q1→Q2→Q3 Cascade ───────────────────────────────────────────────────
    {
        printf("\n--- Q1→Q2→Q3 Full Cascade ---\n");
        transfo::CEStageWDF<transfo::BJTLeaf> q1, q2;
        transfo::EFStageWDF<transfo::BJTLeaf> q3;

        transfo::CEStageConfig q1Cfg;
        q1Cfg.bjt = config.q1; q1Cfg.R_collector = config.R_collector_q1;
        q1Cfg.R_emitter = 47.0f; q1Cfg.R_base_bias = config.R_bias_base_q1;
        q1Cfg.C_input = config.C_input; q1Cfg.C_miller = config.C_miller;
        q1Cfg.C_bypass = 0.0f; q1Cfg.Vcc = config.Vcc;
        q1.prepare(kSampleRate, q1Cfg);

        transfo::CEStageConfig q2Cfg;
        q2Cfg.bjt = config.q2; q2Cfg.R_collector = config.R_collector_q2;
        q2Cfg.R_emitter = config.R_emitter_q2; q2Cfg.R_base_bias = config.R_collector_q1;
        q2Cfg.C_input = 0.0f; q2Cfg.C_miller = 0.0f;
        q2Cfg.C_bypass = 0.0f; q2Cfg.Vcc = config.Vcc;
        q2.prepare(kSampleRate, q2Cfg);

        transfo::EFStageConfig q3Cfg;
        q3Cfg.bjt = config.q3; q3Cfg.R_bias = config.R_bias_q3;
        q3Cfg.C_out = config.C_out; q3Cfg.C_out_film = config.C_out_film;
        q3Cfg.R_series_out = config.R_series_out; q3Cfg.Vcc = config.Vcc;
        q3.prepare(kSampleRate, q3Cfg);

        float Aol_analytical = q1.getGainInstantaneous() * q2.getGainInstantaneous();
        printf("  Analytical Aol (Q1*Q2) = %.2f (%.1f dB)\n",
               Aol_analytical, 20.0f * std::log10(std::abs(Aol_analytical) + 1e-30f));

        // Also capture per-stage peak values at a sine peak
        float peakIn = 0, peakV1 = 0, peakV2 = 0, peakV3 = 0;
        int peakSample = -1;

        auto proc = [&](float in) {
            float v1 = q1.processSample(in);
            float v2 = q2.processSample(v1);
            float v3 = q3.processSample(v2);
            return v3;
        };

        // Run warmup
        for (int i = 0; i < 2000; ++i)
            proc(kAmp * std::sin(2.0f * kPi * kFreq * static_cast<float>(i) * kTs));

        // Now measure
        double sumSqIn = 0, sumSqOut = 0;
        int nMeas = 4000;
        for (int i = 0; i < nMeas; ++i) {
            float t = static_cast<float>(i + 2000) * kTs;
            float input = kAmp * std::sin(2.0f * kPi * kFreq * t);
            float v1 = q1.processSample(input);
            float v2 = q2.processSample(v1);
            float v3 = q3.processSample(v2);
            sumSqIn  += static_cast<double>(input) * input;
            sumSqOut += static_cast<double>(v3) * v3;

            // Track positive peaks (input near maximum)
            if (input > peakIn) {
                peakIn = input;
                peakV1 = v1;
                peakV2 = v2;
                peakV3 = v3;
                peakSample = i + 2000;
            }
        }

        double rmsIn  = std::sqrt(sumSqIn / nMeas);
        double rmsOut = std::sqrt(sumSqOut / nMeas);
        double gainDB = 20.0 * std::log10(rmsOut / rmsIn);
        printf("  Actual WDF cascade gain = %.2f (%.1f dB)\n", rmsOut / rmsIn, gainDB);
        printf("  Ratio actual/analytical = %.3f\n",
               (rmsOut / rmsIn) / (std::abs(Aol_analytical) + 1e-30f));

        printf("\n  At positive peak (sample %d):\n", peakSample);
        printf("    input  = %.6f V\n", peakIn);
        printf("    v1(Q1) = %.6f V  [gain: %.2f]\n",
               peakV1, std::abs(peakV1 / (peakIn + 1e-30f)));
        printf("    v2(Q2) = %.6f V  [gain: %.2f from v1]\n",
               peakV2, std::abs(peakV2 / (peakV1 + 1e-30f)));
        printf("    v3(Q3) = %.6f V  [gain: %.4f from v2]\n",
               peakV3, std::abs(peakV3 / (peakV2 + 1e-30f)));
        printf("    total  = %.2f (%.1f dB)\n",
               std::abs(peakV3 / (peakIn + 1e-30f)),
               20.0f * std::log10(std::abs(peakV3 / (peakIn + 1e-30f)) + 1e-30f));

        printf("\n  Per-stage analytical:\n");
        printf("    Q1: %.2f (%.1f dB)\n",
               q1.getGainInstantaneous(),
               20.0f * std::log10(std::abs(q1.getGainInstantaneous()) + 1e-30f));
        printf("    Q2: %.4f (%.1f dB)\n",
               q2.getGainInstantaneous(),
               20.0f * std::log10(std::abs(q2.getGainInstantaneous()) + 1e-30f));
    }

    // ─── Full NeveClassAPath (with Acl/Aol correction) ──────────────────────
    {
        printf("\n--- Full NeveClassAPath (Rfb=1430, Acl=31.43) ---\n");
        transfo::NeveClassAPath path;
        path.configure(config);
        path.prepare(kSampleRate, 512);
        float rfb = transfo::GainTable::getRfb(5);
        path.setGain(rfb);
        path.reset();

        float Acl = 1.0f + rfb / 47.0f;
        float Aol = std::abs(path.getOpenLoopGain());
        printf("  Acl = %.2f (%.1f dB), Aol = %.2f (%.1f dB)\n",
               Acl, 20.0f * std::log10(Acl),
               Aol, 20.0f * std::log10(Aol + 1e-30f));
        printf("  Acl/Aol correction = %.4f\n", Acl / Aol);

        auto proc = [&](float in) { return path.processSample(in); };
        auto r = measureGain(proc);
        printf("  Measured closed-loop gain = %.2f (%.1f dB)\n", r.gainLinear, r.gainDB);
        printf("  Expected = %.2f (%.1f dB)\n", Acl, 20.0f * std::log10(Acl));
        printf("  Offset = %.1f dB, Factor = %.2fx\n",
               r.gainDB - 20.0 * std::log10(Acl),
               r.gainLinear / Acl);
    }

    printf("\n=== Done ===\n");
    return 0;
}
