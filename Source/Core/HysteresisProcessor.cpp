#include "Core/HysteresisProcessor.h"
#include "Core/HysteresisUtils.h"

// =============================================================================
// HysteresisProcessor implementation
//
// Jiles-Atherton model with Newton-Raphson implicit solver (NR8).
// Trapezoidal rule integration for M(t).
//
// Key equations (Venkataraman corrected form):
//   dM/dH = [(1-c)(Man-M)] / [delta*k - alpha*(Man-M)]
//          + c * dMan/dH
//
//   with physical constraint: if delta*(Man-M) < 0, irreversible term = 0
//
// STABILITY CONDITION: k > alpha * Ms  (MANDATORY)
//   If violated, the denominator (delta*k - alpha*(Man-M)) can flip sign
//   when |Man-M| approaches Ms, causing numerical divergence.
//   With defaults: alpha*Ms = 1e-4 * 1.2e6 = 120 < k = 200 ✓
//
// The implicit coupling (denominator 1 - alpha*dM/dH_total) ensures that
// the effective field Heff = H + alpha*M is properly accounted for.
// =============================================================================

using namespace HysteresisUtils;

HysteresisProcessor::HysteresisProcessor() = default;

void HysteresisProcessor::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    Ts = 1.0 / sampleRate;
    reset();
}

void HysteresisProcessor::reset()
{
    M_prev    = 0.0;
    H_prev    = 0.0;
    dMdt_prev = 0.0;
    lastIterCount = 0;
    lastConverged = true;
}

// ─── Parameter setters ────────────────────────────────────────────────────────

void HysteresisProcessor::setMs(double value)          { Ms = value; }
void HysteresisProcessor::setA(double value)           { a = value; }
void HysteresisProcessor::setK(double value)           { k = value; }
void HysteresisProcessor::setC(double value)           { c = value; }
void HysteresisProcessor::setAlpha(double value)       { alpha = value; }
void HysteresisProcessor::setInputScaling(double s)    { inputScale = s; }
void HysteresisProcessor::setOutputScaling(double s)   { outputScale = s; }
void HysteresisProcessor::setMaxIterations(int n)      { maxIterations = n; }
void HysteresisProcessor::setTolerance(double tol)     { tolerance = tol; }

double HysteresisProcessor::getLastFluxDensity() const
{
    // B = mu_0 * (H + M)
    return mu_0 * (H_prev + M_prev);
}

// ─── Anhysteretic magnetization ───────────────────────────────────────────────
// Man = Ms * L(Heff / a), where Heff = H + alpha * M
double HysteresisProcessor::computeMan(double H, double M) const
{
    const double Heff = H + alpha * M;
    const double x = Heff / a;
    return Ms * langevin(x);
}

// ─── dM/dt computation ────────────────────────────────────────────────────────
// This is the core J-A differential equation transformed to time domain:
//   dM/dt = (dM/dH) * (dH/dt)
//
// dM/dH has two components:
//   1. Irreversible: (1-c)(Man-M) / (delta*k - alpha*(Man-M))
//      — active only when delta*(Man-M) > 0 (positive susceptibility constraint)
//   2. Reversible: c * dMan/dHeff * (1 + alpha * dM/dH)
//      — simplified here as c * dMan/dHeff
//
// The implicit coupling denominator (1 - alpha*dMdH_total) accounts for the
// feedback loop Heff = H + alpha*M → Man(Heff) → M → Heff ...
double HysteresisProcessor::computeDMdt(double H, double M, double dHdt) const
{
    const double Heff = H + alpha * M;
    const double x = Heff / a;

    const double Man = Ms * langevin(x);
    const double dManHeff = (Ms / a) * langevinDeriv(x);  // dMan/dHeff

    const double delta = (dHdt >= 0.0) ? 1.0 : -1.0;
    const double diff = Man - M;

    // ── Irreversible component ──
    // Physical constraint: susceptibility must remain positive.
    // When delta*(Man - M) < 0, only the reversible term contributes.
    double dMdH_irreversible = 0.0;
    if (delta * diff > 0.0)
    {
        double denom = delta * k - alpha * diff;

        // Numerical protection against near-zero denominator
        if (std::abs(denom) < 1e-8)
            denom = (denom >= 0.0) ? 1e-8 : -1e-8;

        dMdH_irreversible = (1.0 - c) * diff / denom;
    }

    // ── Reversible component ──
    const double dMdH_reversible = c * dManHeff;

    // ── Total dM/dH with implicit coupling ──
    const double dMdH_total = dMdH_irreversible + dMdH_reversible;
    double denominator = 1.0 - alpha * dMdH_total;

    if (std::abs(denominator) < 1e-12)
        denominator = 1e-12;

    const double dMdH = dMdH_total / denominator;

    // Convert to time domain: dM/dt = dM/dH * dH/dt
    return dMdH * dHdt;
}

// ─── Jacobian: partial derivative of dM/dt w.r.t. M ──────────────────────────
// Needed for Newton-Raphson convergence.
// Using numerical central difference for now — analytical version can replace
// this in production for better performance.
double HysteresisProcessor::computeDMdt_dM(double H, double M, double dHdt) const
{
    // Epsilon must be scaled to the magnitude of M (which is ~Ms ~ 1e6).
    // A fixed eps=1e-6 gives relative perturbation of 1e-12 — below float noise.
    // Use eps proportional to Ms but with a minimum floor.
    const double eps = std::max(1.0, std::abs(M) * 1e-8);
    const double f_plus  = computeDMdt(H, M + eps, dHdt);
    const double f_minus = computeDMdt(H, M - eps, dHdt);
    return (f_plus - f_minus) / (2.0 * eps);
}

// ─── Newton-Raphson implicit solver ───────────────────────────────────────────
// Solves for M[n] using trapezoidal integration:
//   M[n] = M[n-1] + (Ts/2) * (f(H[n], M[n], dHdt[n]) + f(H[n-1], M[n-1], dHdt[n-1]))
//
// This is an implicit equation because M[n] appears on both sides.
// We define g(M_est) = M_est - M_old - (Ts/2)*(f_new + f_old)
// and iterate: M_est(k+1) = M_est(k) - g / g'
//
// Initial guess: linear extrapolation from previous sample.
// Convergence is typically 2-4 iterations thanks to oversampling.
double HysteresisProcessor::solveImplicit(double H_new, double H_old,
                                           double M_old, double dMdt_old)
{
    const double dH = H_new - H_old;
    const double dHdt = dH / Ts;

    // Initial guess: linear extrapolation
    double M_est = M_old + Ts * dMdt_old;

    lastConverged = false;
    lastIterCount = 0;

    for (int i = 0; i < maxIterations; ++i)
    {
        lastIterCount = i + 1;

        // f(H_new, M_est, dHdt) = dM/dt evaluated at estimated point
        const double f_new = computeDMdt(H_new, M_est, dHdt);

        // g(M_est) = M_est - M_old - (Ts/2) * (f_new + f_old)
        const double g = M_est - M_old - (Ts / 2.0) * (f_new + dMdt_old);

        // g'(M_est) = 1 - (Ts/2) * df/dM
        const double df_dM = computeDMdt_dM(H_new, M_est, dHdt);
        double g_prime = 1.0 - (Ts / 2.0) * df_dM;

        // Division-by-zero protection
        if (std::abs(g_prime) < 1e-15)
            g_prime = 1e-15;

        const double delta_M = -g / g_prime;
        M_est += delta_M;

        // Convergence check: absolute tolerance + relative tolerance
        // Absolute alone (1e-12) is meaningless when M ~ 1e6
        const double absTol = tolerance;
        const double relTol = tolerance * std::max(1.0, std::abs(M_est));
        if (std::abs(delta_M) < std::max(absTol, relTol))
        {
            lastConverged = true;
            break;
        }
    }

    // Safety clamp: M cannot exceed ±1.1 * Ms
    M_est = std::clamp(M_est, -1.1 * Ms, 1.1 * Ms);

    return M_est;
}

// ─── Main processing function ─────────────────────────────────────────────────
// Pipeline for each sample:
//   1. Convert audio → H field
//   2. Solve hysteresis (NR implicit) → M
//   3. Compute dM/dt for next trapezoidal step
//   4. Output ∝ dB/dt (Faraday's law)
double HysteresisProcessor::process(double input)
{
    // 1. Audio → magnetic field H
    const double H = input * inputScale;

    // 2. Solve hysteresis: find M[n] via Newton-Raphson
    const double M = solveImplicit(H, H_prev, M_prev, dMdt_prev);

    // 3. Compute dM/dt for next step (trapezoidal integration memory)
    const double dH = H - H_prev;
    const double dHdt = dH / Ts;
    const double dMdt = computeDMdt(H, M, dHdt);

    // 4. Induced voltage proportional to dB/dt (Faraday's law)
    //    B = mu_0 * (H + M)
    //    dB/dt ≈ mu_0 * (dH/dt + dM/dt)
    const double dBdt = mu_0 * (dHdt + dMdt);

    // 5. Update state for next sample
    M_prev    = M;
    H_prev    = H;
    dMdt_prev = dMdt;

    // 6. Magnetic → audio
    return dBdt * outputScale;
}
