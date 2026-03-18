#pragma once

// =============================================================================
// DiodeLeaf — Shockley diode as WDF one-port nonlinear element.
//
// Implements the Shockley diode equation:
//   Id = Is * (exp(Vd / (N * Vt)) - 1)
//
// solved in the wave domain via Newton-Raphson iteration.
//
// WDF scattering:
//   V = (a + b) / 2     (Kirchhoff voltage)
//   I = (a - b) / (2Z)  (Kirchhoff current)
//   Diode: I = Is * (exp(V / (N*Vt)) - 1)
//
//   Substituting: V = a - Z*Is*(exp(V/(N*Vt)) - 1)
//   Define f(V) = V - a + Z*Is*(exp(V/(N*Vt)) - 1) = 0
//   f'(V) = 1 + Z*Is/(N*Vt) * exp(V/(N*Vt))
//   Newton: V_{k+1} = V_k - f(V_k) / f'(V_k)
//   Then: b = 2*V - a
//
// Warm-start: V_prev from previous sample (temporal correlation exploited).
// Typical convergence: 2-3 iterations per sample.
//
// Passivity: guaranteed by the diode's monotonic I-V characteristic and
// the trapezoidal WDF discretization. Energy is always dissipated.
//
// Pattern: CRTP via WDOnePort<DiodeLeaf> (zero virtual overhead).
//
// Reference: chowdsp_wdf DiodeT.h; Werner Stanford thesis ch.4;
//            Yeh "Automated Physical Modeling" DAFx-2006
// =============================================================================

#include "../util/Constants.h"
#include "WDOnePort.h"
#include <algorithm>
#include <cmath>

namespace transfo {

class DiodeLeaf : public WDOnePort<DiodeLeaf>
{
public:
    struct Params
    {
        float Is = 1e-14f;     // Saturation current [A]
        float Vt = 0.02585f;   // Thermal voltage kT/q [V]
        float N  = 1.0f;       // Ideality factor (1.0 for Si, 1.5-2.0 for Schottky/LED)

        bool isValid() const { return Is > 0.0f && Vt > 0.0f && N > 0.0f; }
    };

    DiodeLeaf() = default;

    // ── Configuration ────────────────────────────────────────────────────────
    void configure(const Params& p)
    {
        params_ = p;
        nVt_ = p.N * p.Vt;
        // Precompute exp limit to prevent overflow
        // exp(V/(N*Vt)) overflows float for V > ~20 (with N*Vt ~26mV)
        V_max_ = nVt_ * 35.0f;   // exp(35) ~ 1.59e15, safe in double
    }

    void prepare(float sampleRate)
    {
        sampleRate_ = sampleRate;
        // Adaptive port resistance: initial guess = 1K (moderate bias)
        Z_port_ = 1000.0f;
    }

    void reset()
    {
        V_prev_ = 0.0f;
        a_prev_ = 0.0f;
        a_incident_ = 0.0f;
        b_reflected_ = 0.0f;
    }

    // ── WDOnePort CRTP interface ────────────────────────────────────────────

    float scatterImpl(float a)
    {
        const float V = solveNewtonRaphson(a);
        const float b = 2.0f * V - a;
        V_prev_ = V;
        a_prev_ = a;
        return b;
    }

    float getPortResistanceImpl() const
    {
        // Dynamic port resistance from small-signal conductance at operating point:
        //   gd = dId/dVd = Is/(N*Vt) * exp(Vd/(N*Vt))
        //   Z = 1 / gd
        // For stability, clamp between physically meaningful limits.
        const float V = V_prev_;
        const float V_clamped = std::clamp(V, -1.0f, V_max_);
        const double expTerm = std::exp(static_cast<double>(V_clamped) / nVt_);
        const double gd = static_cast<double>(params_.Is) / nVt_ * expTerm;

        if (gd < 1e-15)
            return 1e8f;  // Reverse bias: very high impedance

        return static_cast<float>(std::clamp(1.0 / gd, 1.0, 1e8));
    }

    // ── State management (HSIM interface) ───────────────────────────────────
    void commitState()  { /* Diode is memoryless — nothing to commit */ }
    void rollbackState() { /* Nothing to rollback */ }

    // ── Monitoring ──────────────────────────────────────────────────────────
    float getVoltage() const { return V_prev_; }
    float getCurrent() const
    {
        return params_.Is * (std::exp(std::clamp(V_prev_, -1.0f, V_max_) / nVt_) - 1.0f);
    }
    int getLastIterCount() const { return lastIterCount_; }

    const Params& getParams() const { return params_; }

private:
    Params params_;
    float nVt_        = 0.02585f;   // N * Vt (precomputed)
    float V_max_      = 0.9f;       // Clamp to prevent exp overflow
    float V_prev_     = 0.0f;       // Warm-start: previous sample solution
    float a_prev_     = 0.0f;       // Previous incident wave (for predictor)
    float sampleRate_ = 44100.0f;
    int   lastIterCount_ = 0;

    // ── Newton-Raphson solver ───────────────────────────────────────────────
    // Solve: f(V) = V - a + Z * Is * (exp(V / (N*Vt)) - 1) = 0
    // f'(V) = 1 + Z * Is / (N*Vt) * exp(V / (N*Vt))
    float solveNewtonRaphson(float a)
    {
        const double Z   = static_cast<double>(Z_port_);
        const double Is  = static_cast<double>(params_.Is);
        const double nVt = static_cast<double>(nVt_);
        const double a_d = static_cast<double>(a);
        const double Vmax = static_cast<double>(V_max_);

        // Warm-start: linear predictor using implicit function theorem.
        //
        // From f(V,a) = V - a + Z*Is*exp(V/nVt) = 0:
        //   dV/da = 1 / (1 + Z*Is/nVt * exp(V/nVt)) = 1 / f'(V)
        //
        // Predict: V_init = V_prev + dV/da * (a - a_prev)
        //
        // This tracks the solution through both forward and reverse bias,
        // giving a much closer initial guess than raw V_prev_ when the
        // signal changes significantly between samples.
        const double V_p = static_cast<double>(V_prev_);
        const double da = a_d - static_cast<double>(a_prev_);
        double V;
        if (a_d < 0.0 && V_p > 0.0) {
            // Forward-to-reverse: jump directly (V ≈ a in reverse bias)
            V = a_d;
        } else if (a_d > 0.0 && V_p < -0.5) {
            // Reverse-to-forward: start at diode knee
            V = 0.0;
        } else {
            // Linear predictor using sensitivity at previous operating point
            const double V_pc = std::clamp(V_p, -2.0, Vmax);
            const double expPrev = std::exp(V_pc / nVt);
            const double fpPrev = 1.0 + Z * Is / nVt * expPrev;
            const double dVda = 1.0 / fpPrev;
            V = V_p + dVda * da;
        }

        // Clamp initial guess to prevent divergence
        V = std::clamp(V, -2.0, Vmax);

        int iter = 0;
        for (; iter < kMaxNRIter; ++iter) {
            const double V_safe = std::clamp(V, -2.0, Vmax);
            const double expV = std::exp(V_safe / nVt);

            // f(V) = V - a + Z * Is * (exp(V/(N*Vt)) - 1)
            const double f = V_safe - a_d + Z * Is * (expV - 1.0);

            // f'(V) = 1 + Z * Is / (N*Vt) * exp(V/(N*Vt))
            const double fp = 1.0 + Z * Is / nVt * expV;

            // Newton step with damping
            double dV = f / fp;

            // Asymmetric damping based on SPICE diode voltage limiting:
            //
            // Forward direction (dV < 0 → V increasing → exp grows):
            //   Limit step so exp changes by at most e^2 per iteration.
            //   maxStep = 2*nVt prevents NR overshoot in the exponential
            //   region while still allowing ~52mV steps (converges in 2-3
            //   iterations for typical inter-sample voltage changes).
            //
            // Reverse direction (dV > 0 → V decreasing → exp shrinks):
            //   Exponential vanishes so f(V) ≈ V - a is linear.
            //   Large steps are safe; allow up to 50*nVt (~1.3V).
            const double maxStepFwd = 2.0 * nVt;    // ~52 mV (exp ratio ≤ e²)
            const double maxStepRev = 50.0 * nVt;   // ~1.29 V (linear regime)
            if (dV > 0.0)
                dV = std::min(dV, maxStepRev);   // V decreasing (toward reverse)
            else
                dV = std::max(dV, -maxStepFwd);  // V increasing (toward forward)

            V = V_safe - dV;

            // Convergence check
            if (std::abs(dV) < 1e-6 * nVt) {
                ++iter;
                break;
            }
        }

        lastIterCount_ = iter;
        return static_cast<float>(std::clamp(V, -2.0, Vmax));
    }
};

} // namespace transfo
