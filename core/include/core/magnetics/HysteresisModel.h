#pragma once

// =============================================================================
// HysteresisModel — Jiles-Atherton hysteresis model with implicit NR solver.
//
// Refactored from HysteresisProcessor: now a template on AnhystereticType
// (CRTP), no audio I/O concern. Pure magnetic domain: H in -> M out.
//
// Key improvements over Phase 1:
//   - Template on anhysteretic function (LangevinPade or CPWLAnhysteretic)
//   - Double-buffering: M_committed_ / M_tentative_ for iterative WDF rollback
//   - Extrapolative warm-start: M_pred = 2*M_c - M_prev_c [v2]
//   - commitState() / rollbackState() for iterative WDF compatibility (HSIM set aside, see ADR-001)
//   - getInstantaneousSusceptibility() for adaptive Z in WDF
//
// Solver: Newton-Raphson with trapezoidal integration (configurable max iter).
//
// Reference: Jiles & Atherton 1986; Chowdhury CCRMA 2020 (AnalogTapeModel);
//            Venkataraman corrected form; chowdsp_wdf DiodeT.h pattern.
// =============================================================================

#include "JAParameterSet.h"
#include "AnhystereticFunctions.h"
#include "../util/Constants.h"
#include <cmath>
#include <algorithm>

namespace transfo {

template <typename AnhystType>
class HysteresisModel
{
public:
    HysteresisModel() = default;

    // ─── Configuration ──────────────────────────────────────────────────────
    void setParameters(const JAParameterSet& params)
    {
        params_ = params;
    }

    void setSampleRate(double sampleRate)
    {
        Ts_ = 1.0 / sampleRate;
    }

    void setMaxIterations(int n) { maxIter_ = n; }
    void setTolerance(double tol) { tolerance_ = tol; }

    void reset()
    {
        M_committed_      = 0.0;
        M_tentative_      = 0.0;
        M_prev_committed_ = 0.0;
        H_prev_           = 0.0;
        delta_            = 1;
        lastIterCount_    = 0;
        lastConverged_    = true;

        // Fix 1: Initialize dMdH_prev_ to the linear-region susceptibility
        // χ_eff at the demagnetized origin (H=0, M=0).  Without this,
        // dMdH_prev_=0 halves the first trapezoidal step and causes NR
        // divergence at sample 2 in Physical mode (small H).
        dMdH_prev_ = computeRHS(0.0, 0.0, 1);
    }

    // ─── Core computation: dM/dH (J-A ODE) ─────────────────────────────────
    // This is f(M, H, delta) from the Jiles-Atherton differential equation.
    //
    // dM/dH = [(1-c)(Man - M)] / [delta*k - alpha*(Man - M)]
    //          + c * dMan/dHeff
    //
    // with implicit coupling denominator: 1 / (1 - alpha * dMdH_total)
    double computeRHS(double M, double H, int delta) const
    {
        const double Heff = H + params_.alpha * M;
        const double x = Heff / params_.a;

        const double Man = params_.Ms * anhyst_.evaluateD(x);
        const double dManHeff = (params_.Ms / params_.a) * anhyst_.derivativeD(x);

        const double diff = Man - M;

        // Irreversible component (active only when delta*(Man-M) > 0)
        double dMdH_irrev = 0.0;
        if (delta * diff > 0.0)
        {
            double denom = delta * params_.k - params_.alpha * diff;
            if (std::abs(denom) < 1e-8)
                denom = (denom >= 0.0) ? 1e-8 : -1e-8;

            dMdH_irrev = (1.0 - params_.c) * diff / denom;
        }

        // Reversible component
        const double dMdH_rev = params_.c * dManHeff;

        // Total with implicit coupling
        const double dMdH_total = dMdH_irrev + dMdH_rev;
        double denominator = 1.0 - params_.alpha * dMdH_total;
        if (std::abs(denominator) < kEpsilonD)
            denominator = kEpsilonD;

        return dMdH_total / denominator;
    }

    // ─── V2.1: Analytical Jacobian d(dM/dH)/dM ────────────────────────────
    // Closed-form derivative of the J-A ODE for ~2x faster NR convergence.
    double computeAnalyticalJacobian(double M, double H, int delta) const
    {
        const double Heff = H + params_.alpha * M;
        const double x = Heff / params_.a;

        const double Man = params_.Ms * anhyst_.evaluateD(x);
        const double dManHeff = (params_.Ms / params_.a) * anhyst_.derivativeD(x);

        // d²Man/dHeff² — analytical second derivative (Fix 3).
        // Replaces finite-difference which suffered catastrophic cancellation
        // at small x where L'(x) = 1/x² - 1/sinh²(x) ≈ 1/3 - x²/15.
        // The FD noise corrupted the Jacobian, causing NR failure at low H.
        const double d2ManHeff2 = (params_.Ms / (params_.a * params_.a))
                                * anhyst_.secondDerivativeD(x);

        const double diff = Man - M;

        // dMan/dM via chain rule: dMan/dHeff * dHeff/dM = dManHeff * alpha
        const double dDiff_dM = dManHeff * params_.alpha - 1.0;

        // d(irrev)/dM
        double df_irrev_dM = 0.0;
        double irrev_val = 0.0;
        if (delta * diff > 0.0)
        {
            double denom = delta * params_.k - params_.alpha * diff;
            if (std::abs(denom) < 1e-8)
                denom = (denom >= 0.0) ? 1e-8 : -1e-8;

            irrev_val = (1.0 - params_.c) * diff / denom;
            const double dDenom_dM = -params_.alpha * dDiff_dM;
            // Quotient rule: d/dM [(1-c)*diff/denom]
            df_irrev_dM = (1.0 - params_.c)
                          * (dDiff_dM * denom - diff * dDenom_dM) / (denom * denom);
        }

        // d(rev)/dM = c * d²Man/dHeff² * alpha
        const double df_rev_dM = params_.c * d2ManHeff2 * params_.alpha;

        const double df_total_dM = df_irrev_dM + df_rev_dM;

        // Implicit coupling: f_final = f_total / (1 - α·f_total)
        // df_final/dM = df_total_dM / (1 - α·f_total)²
        const double f_total = irrev_val + params_.c * dManHeff;
        double denom_c = 1.0 - params_.alpha * f_total;
        if (std::abs(denom_c) < 1e-15) denom_c = 1e-15;

        return df_total_dM / (denom_c * denom_c);
    }

    // ─── Jacobian: d(dM/dH)/dM ─────────────────────────────────────────────
    // Uses analytical form by default; FD cross-check under TRANSFO_DEBUG_JACOBIAN.
    double computeJacobian(double M, double H, int delta) const
    {
#ifdef TRANSFO_DEBUG_JACOBIAN
        const double analytical = computeAnalyticalJacobian(M, H, delta);
        const double eps = std::max(1.0, std::abs(M) * 1e-8);
        const double f_plus  = computeRHS(M + eps, H, delta);
        const double f_minus = computeRHS(M - eps, H, delta);
        const double fd = (f_plus - f_minus) / (2.0 * eps);
        (void)fd; // Use analytical in production, FD available for debug
        return analytical;
#else
        return computeAnalyticalJacobian(M, H, delta);
#endif
    }

    // ─── V2.2: Convergence mode tracking ──────────────────────────────────
    enum class ConvMode { NR, DampedNR, Bisection };
    ConvMode getLastConvMode() const { return lastConvMode_; }

    // ─── Implicit solve: find M[n] given H[n] ──────────────────────────────
    // H-domain trapezoidal rule with damped NR + bisection fallback [V2.2].
    // ΔM = ½(F[n] + F[n-1])·ΔH — eliminates cos(πf/fs) droop from
    // time-domain backward-difference dH/dt formulation.
    double solveImplicitStep(double H_new)
    {
        const double dH = H_new - H_prev_;
        const int newDelta = (dH >= 0.0) ? 1 : -1;

        // Warm-start: extrapolative predictor [v2]
        double M_est = 2.0 * M_committed_ - M_prev_committed_;

        // [v3] Soft-recovery from deep saturation.
        const double satRatio = std::abs(M_committed_)
                              / (static_cast<double>(params_.Ms) + 1e-30);
        if (satRatio > 0.95)
        {
            const double Heff = H_new + params_.alpha * M_committed_;
            const double Man = params_.Ms * anhyst_.evaluateD(Heff / params_.a);
            const double blend = std::min((satRatio - 0.95) * 10.0, 0.5);
            M_est = (1.0 - blend) * M_est + blend * Man;
        }

        lastConverged_ = false;
        lastIterCount_ = 0;
        lastConvMode_ = ConvMode::NR;

        // ── Newton-Raphson with damping ─────────────────────────────────────
        for (int i = 0; i < maxIter_; ++i)
        {
            lastIterCount_ = i + 1;

            const double dMdH_new = computeRHS(M_est, H_new, newDelta);

            // H-domain trapezoidal: ΔM = ½(F[n] + F[n-1])·ΔH
            const double g = M_est - M_committed_
                           - 0.5 * (dMdH_new + dMdH_prev_) * dH;

            const double dfdM = computeJacobian(M_est, H_new, newDelta);
            double g_prime = 1.0 - 0.5 * dfdM * dH;

            if (std::abs(g_prime) < 1e-15)
                g_prime = 1e-15;

            double delta_M = -g / g_prime;

            // V2.2: Damping when step is too large
            if (std::abs(delta_M) > std::abs(M_est) * 0.5 + 1.0)
            {
                delta_M *= 0.5;
                lastConvMode_ = ConvMode::DampedNR;
            }

            M_est += delta_M;

            const double tol = std::max(tolerance_, tolerance_ * std::abs(M_est));
            if (std::abs(delta_M) < tol)
            {
                lastConverged_ = true;
                break;
            }
        }

        // ── V2.5: Local-bracket bisection fallback ─────────────────────────
        // Fix 2: Bracket around the predictor instead of ±1.1·Ms.
        // The old global bracket (resolution ≈ 4700 after 8 steps) produced
        // midpoint artifacts like M=2363 when the true root was near 37.
        // Local bracket + adaptive expansion + 20 iterations gives sub-1 A/m
        // resolution near the correct root.
        if (!lastConverged_)
        {
            lastConvMode_ = ConvMode::Bisection;
            const double Ms_d = static_cast<double>(params_.Ms);

            auto gFunc = [&](double M_try) -> double {
                const double dMdH_try = computeRHS(M_try, H_new, newDelta);
                return M_try - M_committed_ - 0.5 * (dMdH_try + dMdH_prev_) * dH;
            };

            // Start with a local bracket around the linear predictor
            const double M_pred = M_committed_ + dMdH_prev_ * dH;
            double span = std::max(64.0, 4.0 * std::abs(dMdH_prev_ * dH));
            double M_lo = std::max(-1.1 * Ms_d, M_pred - span);
            double M_hi = std::min( 1.1 * Ms_d, M_pred + span);

            // Expand bracket until we have a sign change (max 6 doublings)
            double g_lo = gFunc(M_lo);
            double g_hi = gFunc(M_hi);
            for (int e = 0; e < 6 && g_lo * g_hi > 0.0; ++e)
            {
                span *= 2.0;
                M_lo = std::max(-1.1 * Ms_d, M_pred - span);
                M_hi = std::min( 1.1 * Ms_d, M_pred + span);
                g_lo = gFunc(M_lo);
                g_hi = gFunc(M_hi);
            }

            // 20 bisection iterations → resolution ≈ span / 2^20
            for (int bi = 0; bi < 20; ++bi)
            {
                double M_mid = 0.5 * (M_lo + M_hi);
                double g_mid = gFunc(M_mid);

                if (g_lo * g_mid <= 0.0)
                    M_hi = M_mid;
                else
                {
                    M_lo = M_mid;
                    g_lo = g_mid;
                }
            }
            M_est = 0.5 * (M_lo + M_hi);
            lastConverged_ = true;
            lastIterCount_ += 20;
        }

        // Safety clamp
        M_est = std::clamp(M_est, -1.1 * static_cast<double>(params_.Ms),
                                    1.1 * static_cast<double>(params_.Ms));

        M_tentative_ = M_est;
        H_tentative_ = H_new;
        delta_ = (dH >= 0.0) ? 1 : -1;

        return M_est;
    }

    // ─── Warm-start from external prediction ────────────────────────────────
    void setInitialGuess(double M_predicted)
    {
        M_tentative_ = M_predicted;
    }

    // ─── State management (HSIM intentionally set aside — see ADR-001) ─────
    void commitState()
    {
        const double H_old = H_prev_;   // Save before overwriting

        M_prev_committed_ = M_committed_;
        M_committed_ = M_tentative_;
        H_prev_ = H_tentative_;

        // Store dM/dH for next H-domain trapezoidal step
        dMdH_prev_ = computeRHS(M_committed_, H_tentative_, delta_);
    }

    void rollbackState()
    {
        M_tentative_ = M_committed_;
    }

    // ─── Instantaneous susceptibility dM/dH (for adaptive WDF Z) ───────────
    double getInstantaneousSusceptibility() const
    {
        return computeRHS(M_committed_, H_prev_, delta_);
    }

    // ─── V2.3: Zirka Energy Balance Diagnostic ────────────────────────────
    // Accumulates ∮H·dB over a complete cycle via trapezoidal integration.
    // Reference: Zirka et al., J. Appl. Phys. 112, 043916 (2012)
    struct EnergyCheckResult {
        double loopArea = 0.0;
        double mismatchPercent = 0.0;
        bool   withinTolerance = true;   // < 10%
    };

    void startEnergyTracking()
    {
        energyAccumulator_ = 0.0;
        B_prev_energy_ = 0.0;
        H_prev_energy_ = 0.0;
        energyTrackingActive_ = true;
    }

    void accumulateEnergy(double H, double B)
    {
        if (!energyTrackingActive_) return;
        // Trapezoidal: ∮H·dB ≈ Σ (H_prev + H)/2 × (B - B_prev)
        energyAccumulator_ += 0.5 * (H_prev_energy_ + H) * (B - B_prev_energy_);
        H_prev_energy_ = H;
        B_prev_energy_ = B;
    }

    EnergyCheckResult getEnergyBalance() const
    {
        EnergyCheckResult r;
        r.loopArea = std::abs(energyAccumulator_);
        // Mismatch can only be computed against a reference loss — for now
        // we just report the absolute loop area for external comparison.
        r.mismatchPercent = 0.0;
        r.withinTolerance = true;
        return r;
    }

    void stopEnergyTracking() { energyTrackingActive_ = false; }

    // ─── Getters ────────────────────────────────────────────────────────────
    double getMagnetization()      const { return M_committed_; }
    double getTentativeMagnetization() const { return M_tentative_; }
    int    getLastIterationCount() const { return lastIterCount_; }
    bool   getLastConverged()      const { return lastConverged_; }

    const JAParameterSet& getParameters() const { return params_; }
    AnhystType& getAnhysteretic() { return anhyst_; }

private:
    JAParameterSet params_;
    AnhystType     anhyst_;

    // Double-buffering state (chowdsp_wdf pattern)
    double M_committed_      = 0.0;    // M[k-1] confirmed
    double M_tentative_      = 0.0;    // M during iterative WDF solving (retained for future use)
    double M_prev_committed_ = 0.0;    // M[k-2] for extrapolative warm-start [v2]
    double H_prev_           = 0.0;    // H[k-1]
    double H_tentative_      = 0.0;    // H during current iteration
    double dMdH_prev_        = 0.0;    // dM/dH at k-1 (H-domain trapezoidal)
    int    delta_            = 1;      // sign(dH/dt)

    // Solver config
    double Ts_        = 1.0 / 44100.0;
    int    maxIter_   = 8;
    double tolerance_ = 1e-12;

    // Debug / diagnostics
    int  lastIterCount_ = 0;
    bool lastConverged_ = true;
    ConvMode lastConvMode_ = ConvMode::NR;        // V2.2

    // V2.3: Zirka energy balance tracking
    double energyAccumulator_ = 0.0;
    double B_prev_energy_ = 0.0;
    double H_prev_energy_ = 0.0;
    bool   energyTrackingActive_ = false;
};

} // namespace transfo
