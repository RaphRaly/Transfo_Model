#pragma once

// =============================================================================
// HysteresisModel — Jiles-Atherton hysteresis model with implicit NR solver.
//
// Refactored from HysteresisProcessor: now a template on AnhystereticType
// (CRTP), no audio I/O concern. Pure magnetic domain: H in -> M out.
//
// Key improvements over Phase 1:
//   - Template on anhysteretic function (LangevinPade or CPWLAnhysteretic)
//   - Double-buffering: M_committed_ / M_tentative_ for HSIM rollback
//   - Extrapolative warm-start: M_pred = 2*M_c - M_prev_c [v2]
//   - commitState() / rollbackState() for HSIM iteration compatibility
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
        dMdt_prev_        = 0.0;
        delta_            = 1;
        lastIterCount_    = 0;
        lastConverged_    = true;
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

    // ─── Jacobian: d(dM/dH)/dM ─────────────────────────────────────────────
    double computeJacobian(double M, double H, int delta) const
    {
        const double eps = std::max(1.0, std::abs(M) * 1e-8);
        const double f_plus  = computeRHS(M + eps, H, delta);
        const double f_minus = computeRHS(M - eps, H, delta);
        return (f_plus - f_minus) / (2.0 * eps);
    }

    // ─── Implicit solve: find M[n] given H[n] ──────────────────────────────
    // Trapezoidal rule: M[n] = M[n-1] + (Ts/2)*(f_new + f_old)
    // where f = dM/dt = dM/dH * dH/dt
    //
    // [v2] Warm-start: M_pred = 2*M_committed - M_prev_committed
    double solveImplicitStep(double H_new)
    {
        const double dH = H_new - H_prev_;
        const double dHdt = dH / Ts_;
        const int newDelta = (dHdt >= 0.0) ? 1 : -1;

        // Warm-start: extrapolative predictor [v2]
        double M_est = 2.0 * M_committed_ - M_prev_committed_;

        lastConverged_ = false;
        lastIterCount_ = 0;

        for (int i = 0; i < maxIter_; ++i)
        {
            lastIterCount_ = i + 1;

            const double dMdH_new = computeRHS(M_est, H_new, newDelta);
            const double f_new = dMdH_new * dHdt;

            // g(M_est) = M_est - M_old - (Ts/2)*(f_new + f_old)
            const double g = M_est - M_committed_ - (Ts_ / 2.0) * (f_new + dMdt_prev_);

            // g'(M_est) = 1 - (Ts/2) * df/dM
            const double dfdM = computeJacobian(M_est, H_new, newDelta) * dHdt;
            double g_prime = 1.0 - (Ts_ / 2.0) * dfdM;

            if (std::abs(g_prime) < 1e-15)
                g_prime = 1e-15;

            const double delta_M = -g / g_prime;
            M_est += delta_M;

            // Convergence check (absolute + relative)
            const double tol = std::max(tolerance_, tolerance_ * std::abs(M_est));
            if (std::abs(delta_M) < tol)
            {
                lastConverged_ = true;
                break;
            }
        }

        // Safety clamp
        M_est = std::clamp(M_est, -1.1 * static_cast<double>(params_.Ms),
                                    1.1 * static_cast<double>(params_.Ms));

        // Store tentative state (not committed yet — HSIM may rollback)
        M_tentative_ = M_est;
        H_tentative_ = H_new;
        delta_ = (dHdt >= 0.0) ? 1 : -1;

        return M_est;
    }

    // ─── Warm-start from external prediction ────────────────────────────────
    void setInitialGuess(double M_predicted)
    {
        M_tentative_ = M_predicted;
    }

    // ─── State management for HSIM ──────────────────────────────────────────
    void commitState()
    {
        const double H_old = H_prev_;   // Save before overwriting

        M_prev_committed_ = M_committed_;
        M_committed_ = M_tentative_;
        H_prev_ = H_tentative_;

        // Update dMdt for next trapezoidal step
        const double dH = H_tentative_ - H_old;
        const double dHdt = dH / Ts_;
        dMdt_prev_ = computeRHS(M_committed_, H_tentative_, delta_) * dHdt;
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
    double M_tentative_      = 0.0;    // M during HSIM iterations
    double M_prev_committed_ = 0.0;    // M[k-2] for extrapolative warm-start [v2]
    double H_prev_           = 0.0;    // H[k-1]
    double H_tentative_      = 0.0;    // H during current iteration
    double dMdt_prev_        = 0.0;    // dM/dt at k-1 (for trapezoidal)
    int    delta_            = 1;      // sign(dH/dt)

    // Solver config
    double Ts_        = 1.0 / 44100.0;
    int    maxIter_   = 8;
    double tolerance_ = 1e-12;

    // Debug
    int  lastIterCount_ = 0;
    bool lastConverged_ = true;
};

} // namespace transfo
