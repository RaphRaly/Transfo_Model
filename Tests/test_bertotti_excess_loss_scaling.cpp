// =============================================================================
// test_bertotti_excess_loss_scaling.cpp — Bertotti excess-loss frequency scaling
//
// At constant B_peak in the linear region, the EXCESS contribution to
// the dynamic B-H loop scales as:
//
//   Energy per cycle (loop area) :  W_excess(f) ∝ sqrt(f)
//   Power dissipated             :  P_excess(f) = W·f ∝ f^1.5
//
// (Correction préface 2026-04-29 du Sprint Plan A2 : la puissance excess loss
// suit f^1.5, mais l'énergie par cycle suit sqrt(f), pas f^1.5.)
//
// Derivation : with H_excess = K2·sign(dB/dt)·sqrt(|dB/dt|) and a steady-state
// sinusoidal B(t) = B_peak·sin(ω·t),
//
//   W_excess = ∮ H_excess · dB
//            = K2 · ∫₀^T |dB/dt|^1.5 dt
//            = K2 · (2π)^1.5 · B_peak^1.5 · sqrt(f) · ⟨|cos|^1.5⟩
//
//   P_excess = W_excess · f ∝ f^1.5
//
// Test plan :
//   1. K1 = 0 (kill eddy term) — isolates excess scaling.
//   2. Sweep f ∈ {50, 200, 1000} Hz with the same H_peak in the linear region.
//   3. Steady-state simulate using the SAME Voie C algorithm as the cascade
//      (Baghel-Kulkarni decoupling + safety clamp). Standalone path so the
//      scaling property of the formula is the unit under test, not the
//      cascade integration.
//   4. Compute loop area via shoelace; verify ratios within ±20 %.
//
// Sprint reference : A2 Voie C task #6 (renommé "excess_loss_scaling", split
// en deux critères : aire ∝ sqrt(f) ET puissance ∝ f^1.5).
// =============================================================================

#include "test_common.h"

#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/magnetics/DynamicLosses.h"
#include "../core/include/core/util/Constants.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace transfo;

namespace {

constexpr double kSampleRate = 192000.0;  // High SR to resolve dB/dt at 1 kHz
constexpr int    kNumCycles  = 12;

struct LoopMetrics {
    double areaJoulePerM3 = 0.0;   // ∮ H · dB on the last cycle
    double B_peak         = 0.0;
    double H_peak         = 0.0;
    double H_dyn_peak     = 0.0;
};

// Simulate `numCycles` of a sine H_drive(t) and return loop metrics for the
// LAST cycle (transient dropped). Uses the cascade Voie C algorithm verbatim
// (Baghel-Kulkarni decoupling + safety clamp), but standalone — no fluxInt,
// no HP, no LC.
LoopMetrics simulateOneSine(double freq, double H_drive_peak,
                            double K1, double K2)
{
    auto params = JAParameterSet::defaultMuMetal();
    params.K1 = static_cast<float>(K1);
    params.K2 = static_cast<float>(K2);

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(params);
    hyst.setSampleRate(kSampleRate);
    hyst.reset();

    DynamicLosses dyn;
    dyn.setCoefficients(static_cast<float>(K1), static_cast<float>(K2));
    dyn.setSampleRate(kSampleRate);
    dyn.reset();

    constexpr double kSafety = 0.8;
    const double kMu0 = 1.2566370614359173e-6;

    int samplesPerCycle = std::max(8, static_cast<int>(std::round(kSampleRate / freq)));
    int totalSamples = samplesPerCycle * kNumCycles;
    int firstMeasureSample = samplesPerCycle * (kNumCycles - 1);

    std::vector<double> Hvals;
    std::vector<double> Bvals;
    Hvals.reserve(static_cast<size_t>(samplesPerCycle));
    Bvals.reserve(static_cast<size_t>(samplesPerCycle));

    LoopMetrics m{};

    for (int n = 0; n < totalSamples; ++n) {
        double t = static_cast<double>(n) / kSampleRate;
        double H_drive = H_drive_peak * std::sin(2.0 * test::kPi * freq * t);

        // ── Voie C predictor + Baghel-Kulkarni decoupling ────────────────
        double H_eff = H_drive;
        if (dyn.isEnabled()) {
            const double M_committed = hyst.getMagnetization();
            const double chi = std::max(0.0,
                hyst.getInstantaneousSusceptibility());
            const double B_pred = kMu0 * (H_drive + M_committed);
            const double dBdt_raw = dyn.computeDBdt(B_pred);
            const double G = dyn.getK1() * dyn.getSampleRate() * kMu0 * chi;
            const double dBdt_dec = dBdt_raw * (1.0 + chi) / (1.0 + G);
            double H_dyn = dyn.computeHfromDBdt(dBdt_dec);
            const double H_abs = std::abs(H_drive);
            if (H_abs > 1e-10) {
                const double H_limit = kSafety * H_abs;
                if (std::abs(H_dyn) > H_limit) {
                    H_dyn = std::copysign(H_limit, H_dyn);
                }
            }
            if (n >= firstMeasureSample) {
                if (std::abs(H_dyn) > m.H_dyn_peak) m.H_dyn_peak = std::abs(H_dyn);
            }
            H_eff = H_drive - H_dyn;
        }

        // J-A solve + commit
        double M = hyst.solveImplicitStep(H_eff);
        hyst.commitState();

        // B = μ₀(H_drive + M) — macroscopic flux density
        double B = kMu0 * (H_drive + M);
        dyn.commitState(B);

        if (n >= firstMeasureSample) {
            Hvals.push_back(H_drive);
            Bvals.push_back(B);
            if (std::abs(H_drive) > m.H_peak) m.H_peak = std::abs(H_drive);
            if (std::abs(B) > m.B_peak) m.B_peak = std::abs(B);
        }
    }

    // ── Loop area via shoelace formula (Σ Hᵢ·B_{i+1} − H_{i+1}·Bᵢ) / 2 ──
    int N = static_cast<int>(Hvals.size());
    double area = 0.0;
    for (int i = 0; i < N; ++i) {
        int j = (i + 1) % N;
        area += Hvals[static_cast<size_t>(i)] * Bvals[static_cast<size_t>(j)]
              - Hvals[static_cast<size_t>(j)] * Bvals[static_cast<size_t>(i)];
    }
    m.areaJoulePerM3 = std::abs(area) / 2.0;
    return m;
}

// ── Test 1 : energy per cycle (loop area) scales as sqrt(f) ────────────────

void test_loop_area_scales_as_sqrt_f()
{
    std::printf("\n=== Bertotti Excess-Loss Scaling: Loop Area ∝ sqrt(f) ===\n");

    // Drive in linear region of mu-metal: H_peak ~ 0.5·a = 15 A/m → B_peak
    // dominated by μ₀·χ·H ≈ 0.20 T (well below saturation).
    constexpr double kHpeak = 15.0;
    constexpr double kK1    = 0.0;     // Kill eddy term — isolate excess
    constexpr double kK2    = 0.05;    // High K2 to make excess clearly measurable

    const double freqs[] = {50.0, 200.0, 1000.0};
    LoopMetrics metrics_with[3];
    LoopMetrics metrics_without[3];
    for (int i = 0; i < 3; ++i) {
        metrics_with[i]    = simulateOneSine(freqs[i], kHpeak, kK1, kK2);
        metrics_without[i] = simulateOneSine(freqs[i], kHpeak, 0.0,  0.0);
    }

    // Excess contribution = total area − static J-A area (at same f, same drive)
    double areaExcess[3];
    for (int i = 0; i < 3; ++i) {
        areaExcess[i] = metrics_with[i].areaJoulePerM3
                      - metrics_without[i].areaJoulePerM3;
        std::printf("  f=%5.0f Hz: Bpk=%.4f T, Hpk=%.2f A/m, Hdyn_pk=%.4f, "
                    "area_total=%.4e, area_static=%.4e, area_excess=%.4e\n",
                    freqs[i], metrics_with[i].B_peak,
                    metrics_with[i].H_peak, metrics_with[i].H_dyn_peak,
                    metrics_with[i].areaJoulePerM3,
                    metrics_without[i].areaJoulePerM3,
                    areaExcess[i]);
    }

    // B_peak should stay roughly constant across f (linear region).
    double Bpk_min = metrics_with[0].B_peak;
    double Bpk_max = Bpk_min;
    for (int i = 1; i < 3; ++i) {
        if (metrics_with[i].B_peak < Bpk_min) Bpk_min = metrics_with[i].B_peak;
        if (metrics_with[i].B_peak > Bpk_max) Bpk_max = metrics_with[i].B_peak;
    }
    double Bpk_spread = (Bpk_max - Bpk_min) / Bpk_min;
    std::printf("  B_peak spread across freqs: %.2f%%\n", Bpk_spread * 100.0);
    CHECK(Bpk_spread < 0.30,
        "B_peak constant within 30% across freqs (linear-region drive)");

    // Excess area should be strictly positive at each freq (otherwise nothing
    // to measure scaling on).
    for (int i = 0; i < 3; ++i) {
        CHECK(areaExcess[i] > 0.0,
            "Excess loop area > 0 at each frequency");
    }

    // Ratio test : area(f) / area(f_ref) ≈ sqrt(f / f_ref) ± 20 %
    for (int i = 1; i < 3; ++i) {
        double measuredRatio = areaExcess[i] / areaExcess[0];
        double predictedRatio = std::sqrt(freqs[i] / freqs[0]);
        double err = std::abs(measuredRatio - predictedRatio) / predictedRatio;
        std::printf("  f=%5.0f vs %5.0f Hz : measured area ratio=%.3f, "
                    "predicted sqrt(f)=%.3f, err=%.1f%%\n",
                    freqs[i], freqs[0], measuredRatio, predictedRatio,
                    err * 100.0);
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "Loop area ratio (f=%.0f / f=%.0f) within 20%% of sqrt(f) law",
            freqs[i], freqs[0]);
        CHECK(err < 0.20, msg);
    }
}

// ── Test 2 : power dissipated scales as f^1.5 ─────────────────────────────

void test_power_scales_as_f15()
{
    std::printf("\n=== Bertotti Excess-Loss Scaling: Power ∝ f^1.5 ===\n");

    constexpr double kHpeak = 15.0;
    constexpr double kK1    = 0.0;
    constexpr double kK2    = 0.05;

    const double freqs[] = {50.0, 200.0, 1000.0};
    double powerExcess[3];
    for (int i = 0; i < 3; ++i) {
        LoopMetrics with_   = simulateOneSine(freqs[i], kHpeak, kK1, kK2);
        LoopMetrics without = simulateOneSine(freqs[i], kHpeak, 0.0, 0.0);
        double areaExcess = with_.areaJoulePerM3 - without.areaJoulePerM3;
        powerExcess[i] = areaExcess * freqs[i];
        std::printf("  f=%5.0f Hz: power_excess = area·f = %.4e [W/m³]\n",
                    freqs[i], powerExcess[i]);
    }

    for (int i = 1; i < 3; ++i) {
        double measuredRatio  = powerExcess[i] / powerExcess[0];
        double predictedRatio = std::pow(freqs[i] / freqs[0], 1.5);
        double err = std::abs(measuredRatio - predictedRatio) / predictedRatio;
        std::printf("  f=%5.0f vs %5.0f Hz : measured power ratio=%.3f, "
                    "predicted f^1.5=%.3f, err=%.1f%%\n",
                    freqs[i], freqs[0], measuredRatio, predictedRatio,
                    err * 100.0);
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "Power ratio (f=%.0f / f=%.0f) within 20%% of f^1.5 law",
            freqs[i], freqs[0]);
        CHECK(err < 0.20, msg);
    }
}

} // namespace

int main()
{
    std::printf("================================================================\n");
    std::printf("  A2 Bertotti Excess-Loss Frequency Scaling Test\n");
    std::printf("  (split corrected per préface 2026-04-29 :\n");
    std::printf("    aire/cycle ∝ sqrt(f), puissance ∝ f^1.5)\n");
    std::printf("================================================================\n");

    test_loop_area_scales_as_sqrt_f();
    test_power_scales_as_f15();

    return test::printSummary("test_bertotti_excess_loss_scaling");
}
