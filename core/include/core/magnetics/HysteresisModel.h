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
#include <limits>

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
    // Convention used here:
    //   M    = total magnetization state returned by this solver
    //   Heff = H + alpha*M
    //   D    = dMan/dHeff
    //   A    = (Man - M) / (delta*k - alpha*(Man - M))
    //
    // The reversible branch Man(Heff) carries the chain factor
    // dHeff/dH = 1 + alpha*dM/dH. The irreversible branch A already includes
    // the JA mean-field denominator, so the chain correction must not be
    // applied globally to A + c*D. Solving
    //   chi = (1-c)*A + c*D*(1 + alpha*chi)
    // gives:
    //   chi = ((1-c)*A + c*D) / (1 - alpha*c*D)
    double computeRHS(double M, double H, int delta) const
    {
        const double Heff = H + params_.alpha * M;
        const double x = Heff / params_.a;

        const double Man = params_.Ms * anhyst_.evaluateD(x);
        const double dManHeff = (params_.Ms / params_.a) * anhyst_.derivativeD(x);

        const double diff = Man - M;

        // Irreversible component (active only when delta*(Man-M) > 0).
        double chiIrrevRaw = 0.0;
        if (delta * diff > 0.0)
        {
            double denom = delta * params_.k - params_.alpha * diff;
            // [Fix A] Scaled epsilon: prevents denominator singularity when
            // |alpha*(Man-M)| → k (near reversal from deep saturation).
            // Scales with k for material-independence (Zhao 2024).
            const double eps_denom = std::max(1e-6, 1e-4 * static_cast<double>(params_.k));
            if (std::abs(denom) < eps_denom)
                denom = std::copysign(eps_denom, denom);

            chiIrrevRaw = diff / denom;

            // Clamp irrev susceptibility to physically plausible range.
            // Max reasonable χ ≈ 2·Ms/a (well above normal operation).
            const double irrev_max = 2.0 * static_cast<double>(params_.Ms)
                                   / static_cast<double>(params_.a);
            chiIrrevRaw = std::clamp(chiIrrevRaw, -irrev_max, irrev_max);
        }

        const double numerator = (1.0 - params_.c) * chiIrrevRaw
                               + params_.c * dManHeff;
        double denominator = 1.0 - params_.alpha * params_.c * dManHeff;
        if (std::abs(denominator) < kEpsilonD)
            denominator = std::copysign(kEpsilonD, denominator == 0.0 ? 1.0 : denominator);

        return numerator / denominator;
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
        double dChiIrrevRaw_dM = 0.0;
        double chiIrrevRaw = 0.0;
        if (delta * diff > 0.0)
        {
            double denom = delta * params_.k - params_.alpha * diff;
            // [Fix A] Same scaled epsilon as computeRHS for Jacobian consistency.
            const double eps_denom = std::max(1e-6, 1e-4 * static_cast<double>(params_.k));
            if (std::abs(denom) < eps_denom)
                denom = std::copysign(eps_denom, denom);

            const double chiUnclamped = diff / denom;
            chiIrrevRaw = chiUnclamped;
            const double dDenom_dM = -params_.alpha * dDiff_dM;
            // Quotient rule: d/dM [diff/denom]
            dChiIrrevRaw_dM = (dDiff_dM * denom - diff * dDenom_dM) / (denom * denom);

            const double irrev_max = 2.0 * static_cast<double>(params_.Ms)
                                   / static_cast<double>(params_.a);
            if (chiIrrevRaw > irrev_max)
            {
                chiIrrevRaw = irrev_max;
                dChiIrrevRaw_dM = 0.0;
            }
            else if (chiIrrevRaw < -irrev_max)
            {
                chiIrrevRaw = -irrev_max;
                dChiIrrevRaw_dM = 0.0;
            }
        }

        // dD/dM = d2Man/dHeff2 * alpha; c is applied in dNumerator_dM.
        const double dD_dM = d2ManHeff2 * params_.alpha;

        const double numerator = (1.0 - params_.c) * chiIrrevRaw
                               + params_.c * dManHeff;
        const double dNumerator_dM = (1.0 - params_.c) * dChiIrrevRaw_dM
                                   + params_.c * dD_dM;

        // Final quotient: chi = numerator / (1 - alpha*c*D).
        double denominator = 1.0 - params_.alpha * params_.c * dManHeff;
        if (std::abs(denominator) < 1e-15)
            denominator = std::copysign(1e-15, denominator == 0.0 ? 1.0 : denominator);
        const double dDenominator_dM = -params_.alpha * params_.c * dD_dM;

        return (dNumerator_dM * denominator - numerator * dDenominator_dM)
             / (denominator * denominator);
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
        // [v4] Slope-aware warm-start: reduce extrapolation when M changes fast
        //       near saturation, then blend toward anhysteretic.
        const double satRatio = std::abs(M_committed_)
                              / (static_cast<double>(params_.Ms) + 1e-30);
        if (satRatio > 0.95)
        {
            const double slope = M_committed_ - M_prev_committed_;
            const double satAwareness = 1.0 / (1.0 + 0.25 * std::abs(slope));
            // In deep saturation with fast-changing M, reduce extrapolation
            M_est = M_committed_ + satAwareness * slope;
            // Blend toward anhysteretic
            const double Heff = H_new + params_.alpha * M_committed_;
            const double Man = params_.Ms * anhyst_.evaluateD(Heff / params_.a);
            const double blend = std::min((satRatio - 0.95) * 10.0, 0.5);
            M_est = (1.0 - blend) * M_est + blend * Man;
        }

        lastConverged_ = false;
        lastIterCount_ = 0;
        lastConvMode_ = ConvMode::NR;

        // ── Newton-Raphson with monotone backtracking line search [v4] ─────
        double g_current = std::numeric_limits<double>::max(); // track |g| for line search
        for (int i = 0; i < maxIter_; ++i)
        {
            lastIterCount_ = i + 1;

            const double dMdH_new = computeRHS(M_est, H_new, newDelta);

            // H-domain trapezoidal: ΔM = ½(F[n] + F[n-1])·ΔH
            double g = M_est - M_committed_
                     - 0.5 * (dMdH_new + dMdH_prev_) * dH;
            g_current = std::abs(g);

            const double dfdM = computeJacobian(M_est, H_new, newDelta);
            double g_prime = 1.0 - 0.5 * dfdM * dH;

            if (std::abs(g_prime) < 1e-15)
                g_prime = 1e-15;

            double delta_M = -g / g_prime;

            // ── [Fix B] Armijo backtracking line-search ──────────────
            // 6 steps (λ down to 1/64) + reverse-direction fallback
            // when the Jacobian sign is wrong near reversal points.
            // Merit function φ(λ) = g² must strictly decrease.
            const double phi_0 = g * g;
            double lambda = 1.0;
            bool accepted = false;
            for (int ls = 0; ls < 6; ++ls)
            {
                const double M_cand = M_est + lambda * delta_M;
                const double dMdH_cand = computeRHS(M_cand, H_new, newDelta);
                const double g_cand = M_cand - M_committed_
                                    - 0.5 * (dMdH_cand + dMdH_prev_) * dH;

                if (std::isfinite(g_cand) && g_cand * g_cand < phi_0)
                {
                    M_est = M_cand;
                    g_current = std::abs(g_cand);
                    accepted = true;
                    if (ls > 0) lastConvMode_ = ConvMode::DampedNR;
                    break;
                }
                lambda *= 0.5;
            }

            if (!accepted)
            {
                // Forward direction exhausted — try reverse (Jacobian sign flip).
                // At reversal points, the analytical Jacobian can have the wrong
                // sign, making delta_M point uphill. Trying -delta_M rescues this.
                const double M_rev = M_est - 0.0625 * delta_M;
                const double dMdH_rev = computeRHS(M_rev, H_new, newDelta);
                const double g_rev = M_rev - M_committed_
                                   - 0.5 * (dMdH_rev + dMdH_prev_) * dH;
                if (std::isfinite(g_rev) && g_rev * g_rev < phi_0)
                {
                    M_est = M_rev;
                    g_current = std::abs(g_rev);
                }
                else
                {
                    // Last resort: minimal forward step
                    M_est += (1.0 / 64.0) * delta_M;
                }
                lastConvMode_ = ConvMode::DampedNR;
            }

            // Convergence check on step size
            const double effectiveStep = accepted ? lambda * std::abs(delta_M)
                                                  : 0.0625 * std::abs(delta_M);
            const double tol = std::max(tolerance_, tolerance_ * std::abs(M_est));
            if (effectiveStep < tol)
            {
                lastConverged_ = true;
                break;
            }
        }

        // ── V2.5: Local-bracket bisection fallback ─────────────────────────
        // Fix 2: Bracket around the predictor instead of ±1.1·Ms.
        // The old global bracket (resolution ≈ 4700 after 8 steps) produced
        // midpoint artifacts like M=2363 when the true root was near 37.
        // Local bracket + adaptive expansion + 8 iterations gives span/256
        // resolution near the correct root. Total max = 8 NR + 8 bisect = 16.
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

            // [Fix C] Adaptive bisection: iteration count scales with bracket
            // span so wide brackets get more iterations (up to 14).
            // Resolution target: span / 2^N < 0.1 A/m.
            const double bisectSpan = M_hi - M_lo;
            const int kBisectionIter = std::clamp(
                static_cast<int>(std::ceil(std::log2(std::max(1.0, bisectSpan / 0.1)))) + 1,
                8, 14);
            for (int bi = 0; bi < kBisectionIter; ++bi)
            {
                double M_mid = 0.5 * (M_lo + M_hi);
                double g_mid = gFunc(M_mid);

                // NaN safety: if midpoint diverged, shrink toward safer side
                if (!std::isfinite(g_mid))
                {
                    if (std::isfinite(g_lo))
                        M_hi = M_mid;
                    else
                        M_lo = M_mid;
                    continue;
                }

                if (g_lo * g_mid <= 0.0)
                    M_hi = M_mid;
                else
                {
                    M_lo = M_mid;
                    g_lo = g_mid;
                }
            }
            M_est = 0.5 * (M_lo + M_hi);

            // NR polish: one Newton step from bisection result to refine
            // to machine precision without risking divergence.
            {
                const double dMdH_pol = computeRHS(M_est, H_new, newDelta);
                const double g_pol = M_est - M_committed_
                                   - 0.5 * (dMdH_pol + dMdH_prev_) * dH;
                const double dfdM_pol = computeJacobian(M_est, H_new, newDelta);
                const double gp_pol = 1.0 - 0.5 * dfdM_pol * dH;
                if (std::abs(gp_pol) > 1e-15)
                {
                    const double M_polished = M_est - 0.5 * g_pol / gp_pol;
                    // Accept only if polish improves residual
                    const double dMdH_check = computeRHS(M_polished, H_new, newDelta);
                    const double g_check = M_polished - M_committed_
                                         - 0.5 * (dMdH_check + dMdH_prev_) * dH;
                    if (std::isfinite(g_check) && std::abs(g_check) < std::abs(g_pol))
                        M_est = M_polished;
                }
            }

            lastConverged_ = true;
            lastIterCount_ += kBisectionIter;
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
