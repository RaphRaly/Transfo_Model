// =============================================================================
// diag_chi_diff.cpp — Sprint 2 diagnostic: measure J-A differential
// susceptibility at minor-loop M=0 crossings in steady state.
//
// Drives HysteresisModel<LangevinPade> directly with H = H0·sin(2πft) for
// several H0 levels, lets it reach steady state (5 cycles), then in the
// last cycle finds the M-zero crossings and reports the χ_diff there.
//
// Compares the measured value to:
//   - chiEff_code = chi0 / (1 - α·chi0)       with chi0 = Ms·c/(3a)
//   - chiAnEff    = chi0_an / (1 - α·chi0_an) with chi0_an = Ms/(3a)
//   - chiMinJiles = c·chi0_an / (1 - α·c·chi0_an)
//
// All three formulas evaluated at the demagnetized origin only — the goal
// is to see which one (if any) matches the steady-state minor-loop value.
//
// Output: stdout table per (preset, H0_norm).
// =============================================================================

#include <core/magnetics/AnhystereticFunctions.h>
#include <core/magnetics/HysteresisModel.h>
#include <core/magnetics/JAParameterSet.h>
#include <core/model/TransformerConfig.h>

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace transfo;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kFreqHz     = 50.0;     // Steady-state probe frequency.
constexpr int    kCycles     = 8;
constexpr int    kAnalysisCycle = 7;     // 0-indexed: last cycle.

struct ChiTriple {
    double chiEff_code;
    double chiAnEff;
    double chiMinJiles;
};

ChiTriple analyticalSusceptibilities(const JAParameterSet& p) {
    const double chi0_an = static_cast<double>(p.Ms)
                         / (3.0 * static_cast<double>(p.a));
    const double chi0_code = chi0_an * static_cast<double>(p.c);
    const double a = static_cast<double>(p.alpha);
    ChiTriple t{};
    t.chiEff_code  = chi0_code / std::max(1e-12, 1.0 - a * chi0_code);
    t.chiAnEff     = chi0_an   / std::max(1e-12, 1.0 - a * chi0_an);
    t.chiMinJiles  = static_cast<double>(p.c) * chi0_an
                   / std::max(1e-12, 1.0 - a * static_cast<double>(p.c) * chi0_an);
    return t;
}

struct Measurement {
    double H0;            // Drive amplitude (A/m peak).
    double H0_over_a;     // H0 normalized by anhysteretic a.
    double chiAtZero;     // Measured χ_diff at M=0 crossing in last cycle.
    double H_atCrossing;  // Value of H at that crossing (A/m).
    double Mpeak;         // |M| peak in last cycle (A/m), saturation indicator.
    int    crossings;     // Number of M=0 crossings counted in last cycle.
};

Measurement runOne(const JAParameterSet& params, double H0) {
    HysteresisModel<LangevinPade> model;
    model.setParameters(params);
    model.setSampleRate(kSampleRate);
    model.setMaxIterations(40);
    model.setTolerance(1e-9);
    model.reset();

    const int totalSamples = static_cast<int>(kCycles * kSampleRate / kFreqHz);
    const int analysisStart = static_cast<int>(kAnalysisCycle * kSampleRate / kFreqHz);
    const int analysisEnd   = totalSamples;

    double M_prev = 0.0;
    double H_prev = 0.0;
    double chi_at_zero = 0.0;
    double H_at_zero = 0.0;
    int crossings = 0;
    double Mpeak = 0.0;

    for (int n = 0; n < totalSamples; ++n) {
        const double t = n / kSampleRate;
        const double H = H0 * std::sin(2.0 * M_PI * kFreqHz * t);

        const double M = model.solveImplicitStep(H);
        model.commitState();

        if (n >= analysisStart && n < analysisEnd) {
            if (std::abs(M) > Mpeak) Mpeak = std::abs(M);

            // Detect M-zero crossing (sign change between previous and current).
            if (n > analysisStart && M_prev * M < 0.0) {
                // Linear interpolation factor in [0,1].
                const double frac = M_prev / (M_prev - M);
                const double H_cross = H_prev + frac * (H - H_prev);

                // Sample χ_diff at the exact crossing using model's RHS via
                // its public accessor — but getInstantaneousSusceptibility()
                // uses (M_committed, H_prev, delta) which is the just-committed
                // state. That's M = current sample, not M=0 exactly. So we
                // re-run a virtual probe via setInitialGuess+solveImplicitStep
                // would perturb state. Simpler: read χ at the closer endpoint.
                const double chi_now = model.getInstantaneousSusceptibility();
                if (crossings == 0) {
                    chi_at_zero = chi_now;
                    H_at_zero   = H_cross;
                }
                ++crossings;
            }
        }

        M_prev = M;
        H_prev = H;
    }

    Measurement m{};
    m.H0 = H0;
    m.H0_over_a = H0 / static_cast<double>(params.a);
    m.chiAtZero = chi_at_zero;
    m.H_atCrossing = H_at_zero;
    m.Mpeak = Mpeak;
    m.crossings = crossings;
    return m;
}

void runPreset(const char* name, const JAParameterSet& params) {
    const auto t = analyticalSusceptibilities(params);
    std::printf("\n=== %s ===\n", name);
    std::printf("J-A params : Ms=%.3e  a=%.2f  alpha=%.2e  k=%.2f  c=%.3f\n",
                params.Ms, params.a, params.alpha, params.k, params.c);
    std::printf("Analytical at origin (virgin):\n");
    std::printf("   chiEff_code  = chi0_c/(1-α·chi0_c)        = %10.2f  [used by code]\n",
                t.chiEff_code);
    std::printf("   chiAnEff     = chi0_an/(1-α·chi0_an)      = %10.2f  [audit assumption]\n",
                t.chiAnEff);
    std::printf("   chiMinJiles  = c·chi0_an/(1-α·c·chi0_an)  = %10.2f  [Jiles 1992 minor]\n",
                t.chiMinJiles);
    std::printf("   ratio chiAnEff/chiEff_code = %.3f (= %.2f dB)\n",
                t.chiAnEff / t.chiEff_code,
                20.0 * std::log10(t.chiAnEff / t.chiEff_code));

    std::printf("\nSteady-state measurement at f=%.0f Hz, fs=%.0f Hz, %d cycles:\n",
                kFreqHz, kSampleRate, kCycles);
    std::printf("  %8s  %8s  %12s  %12s  %12s  %5s   ratio_to_code  ratio_to_anEff\n",
                "H0[A/m]", "H0/a", "Mpeak[A/m]", "chi@M=0", "H@cross", "#zc");

    const std::vector<double> levels = { 0.05, 0.1, 0.3, 0.5, 1.0, 2.0, 4.0 };
    for (double mult : levels) {
        const double H0 = mult * static_cast<double>(params.a);
        const auto m = runOne(params, H0);
        const double r_code = m.chiAtZero / t.chiEff_code;
        const double r_anEff = m.chiAtZero / t.chiAnEff;
        std::printf("  %8.2f  %8.2f  %12.3e  %12.2f  %12.4f  %5d   %6.3f (%+.2f dB)   %6.3f\n",
                    m.H0, m.H0_over_a, m.Mpeak, m.chiAtZero, m.H_atCrossing,
                    m.crossings, r_code, 20.0 * std::log10(std::max(1e-12, r_code)),
                    r_anEff);
    }
}

} // namespace

int main() {
    std::printf("=== Sprint 2 diagnostic: J-A χ_diff at minor-loop M=0 ===\n");
    std::printf("Measures dM/dH at first M-zero crossing in steady-state cycle 8.\n");
    std::printf("Compares to three analytical candidates (origin-only formulas).\n");

    auto cfg115 = TransformerConfig::Jensen_JT115KE();
    runPreset("JT-115K-E (defaultMuMetal)", cfg115.material);

    auto cfg11 = TransformerConfig::Jensen_JT11ELCF();
    runPreset("JT-11ELCF", cfg11.material);

    return 0;
}
