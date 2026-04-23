#include "Core/HysteresisProcessor.h"
#include "Core/HysteresisUtils.h"
#include "core/magnetics/DynamicLosses.h"
#include <limits>

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
    enforceStability();
    recalibrate();
    reset();
}

void HysteresisProcessor::reset()
{
    M_prev    = 0.0;
    H_prev    = 0.0;
    dMdt_prev = 0.0;
    Bprev_    = 0.0;
    Hdyn_prev = 0.0;
    lastIterCount = 0;
    lastConverged = true;
    firstSample   = true;
    if (dynLosses_) dynLosses_->reset();
}

// ─── Dynamic losses mix / preset ──────────────────────────────────────────────
void HysteresisProcessor::refreshDynamicCoefficients()
{
    if (dynLosses_)
    {
        dynLosses_->setCoefficients(static_cast<float>(K1_preset_ * dynMix_),
                                     static_cast<float>(K2_preset_ * dynMix_));
    }
}

void HysteresisProcessor::setDynamicLossAmount(double mix01)
{
    dynMix_ = std::clamp(mix01, 0.0, 1.0);
    refreshDynamicCoefficients();
}

void HysteresisProcessor::setDynamicLossPreset(double K1p, double K2p)
{
    K1_preset_ = std::max(0.0, K1p);
    K2_preset_ = std::max(0.0, K2p);
    refreshDynamicCoefficients();
}

// ─── Parameter setters ────────────────────────────────────────────────────────

void HysteresisProcessor::setMs(double value)          { Ms    = std::clamp(value, 1.0e5, 2.0e6);  enforceStability(); }
void HysteresisProcessor::setA(double value)           { a     = std::clamp(value, 1.0,   500.0);  }
void HysteresisProcessor::setK(double value)           { k     = std::clamp(value, 1.0,   1000.0); enforceStability(); }
void HysteresisProcessor::setC(double value)           { c     = std::clamp(value, 0.0,   0.5);    }
void HysteresisProcessor::setAlpha(double value)       { alpha = std::clamp(value, 1e-6,  1e-2);   enforceStability(); }
void HysteresisProcessor::setInputScaling(double s)    { inputScale = s; }
void HysteresisProcessor::setOutputScaling(double s)   { outputScale = s; }
void HysteresisProcessor::setMaxIterations(int n)      { maxIterations = std::max(1, n); }
void HysteresisProcessor::setTolerance(double tol)     { tolerance = std::max(1e-18, tol); }

void HysteresisProcessor::setTransformerGeometry(double N2, double coreArea_m2, double V_FS_peak)
{
    N2_        = std::max(1.0,    N2);
    coreArea_  = std::max(1e-8,   coreArea_m2);
    V_FS_peak_ = std::max(1e-3,   V_FS_peak);
}

void HysteresisProcessor::setCalibrationTarget(double B_target_T, double A_sat)
{
    B_target_ = std::clamp(B_target_T, 0.01,  2.0);
    A_sat_    = std::clamp(A_sat,      1e-3,  10.0);
}

// Enforce k >= 1.5 * alpha * Ms. Without this, the irreversible susceptibility
// denominator (delta*k - alpha*(Man-M)) can collapse near saturation.
void HysteresisProcessor::enforceStability()
{
    const double kMin = 1.5 * alpha * Ms;
    if (k < kMin) k = kMin;
}

// Virgin-curve sweep: ramp H from 0 upwards, tracking M and B = mu_0*(H+M),
// and return the smallest H that reaches B_target. This is a deterministic
// quasi-static solve — used only at calibration time, not in the audio loop.
double HysteresisProcessor::findHForBTarget(double B_target) const
{
    double H = 0.0;
    double M = 0.0;
    const double H_max = 1.0e6;
    const int    nSteps = 20000;
    const double dH = H_max / nSteps;

    for (int i = 0; i < nSteps; ++i)
    {
        const double Heff = H + alpha * M;
        const double x = Heff / a;
        const double Man = Ms * langevin(x);
        const double dManHeff = (Ms / a) * langevinDeriv(x);

        // Virgin curve: dH > 0 → delta = +1, diff = Man - M is positive.
        const double diff = Man - M;
        double dMdH_irrev = 0.0;
        if (diff > 0.0)
        {
            double denom = k - alpha * diff;
            if (std::abs(denom) < 1e-8)
                denom = (denom >= 0.0) ? 1e-8 : -1e-8;
            dMdH_irrev = (1.0 - c) * diff / denom;
        }

        const double dMdH_rev = c * dManHeff;
        double dMdH_total = dMdH_irrev + dMdH_rev;
        double denom2 = 1.0 - alpha * dMdH_total;
        if (denom2 < 1e-3) denom2 = 1e-3;

        M += (dMdH_total / denom2) * dH;
        H += dH;

        const double B = mu_0 * (H + M);
        if (B >= B_target)
            return H;
    }
    return H_max;   // saturation never reached — return the upper bound
}

// Compute inputScale and outputScale from the calibration target.
// With flux-density output (y = outputScale * B), we want B = B_target to
// yield |y| = 1.0 — i.e. outputScale = 1 / B_target. The transformer geometry
// (N2, A_c, V_FS) is retained for a future Faraday voltage mode but is not
// used for the current B-mode normalization.
// Must be called after any change to Ms/a/k/c/alpha or the target fields.
void HysteresisProcessor::recalibrate()
{
    outputScale = 1.0 / std::max(1e-6, B_target_);

    // inputScale : virgin-curve sweep to find H reaching B_target, then map
    // an input of magnitude A_sat to that H.
    const double H_sat = findHForBTarget(B_target_);
    inputScale = H_sat / std::max(1e-6, A_sat_);
}

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
    // Physically the self-coupling denominator 1 - alpha*dMdH must remain > 0.
    // A sign flip (alpha*dMdH >= 1) inverts dM/dH and drives the solver to
    // diverge. Clamp to a strictly positive floor instead of merely avoiding
    // division by zero. Ref: Chua & Stromsmoe (1971).
    constexpr double kMinDenom = 1e-3;
    double denominator = 1.0 - alpha * dMdH_total;
    if (denominator < kMinDenom)
        denominator = kMinDenom;

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
    // When DynamicLosses is wired and enabled, the residual is evaluated on the
    // effective field H_eff = H_appl − H_dyn(Ḃ(M)) (Baghel/Zirka field separation).
    // H_eff_old uses the committed Hdyn_prev from the previous sample's solve.
    const bool dynActive = (dynLosses_ != nullptr) && dynLosses_->isEnabled();
    const double H_eff_old = H_old - Hdyn_prev;

    // Initial guess: linear extrapolation (same as quasi-static path).
    double M_est = M_old + Ts * dMdt_old;

    lastConverged = false;
    lastIterCount = 0;

    for (int i = 0; i < maxIterations; ++i)
    {
        lastIterCount = i + 1;

        // ── Dynamic coupling at the current trial point ──
        // H_eff depends on M through B = μ₀·(H_appl + M) and Ḃ trapezoidal.
        double H_dyn_trial = 0.0;
        double dH_dyn_dM   = 0.0;
        if (dynActive)
        {
            const double B_trial = mu_0 * (H_new + M_est);
            const auto jac = dynLosses_->evalWithJacobianForNR(B_trial);
            H_dyn_trial = jac.H_dyn;
            dH_dyn_dM   = jac.dH_dyn_dM;
        }
        const double H_eff_new = H_new - H_dyn_trial;
        const double dHdt_eff  = (H_eff_new - H_eff_old) / Ts;

        // Residual g(M) = M − M_old − (Ts/2)·(f(H_eff_new,M,Ḣ_eff) + f_old)
        const double f_new = computeDMdt(H_eff_new, M_est, dHdt_eff);
        const double g = M_est - M_old - (Ts / 2.0) * (f_new + dMdt_old);

        // Base Jacobian: ∂f/∂M at fixed (H_eff, Ḣ_eff) via numerical central diff.
        const double df_dM = computeDMdt_dM(H_eff_new, M_est, dHdt_eff);
        double g_prime = 1.0 - (Ts / 2.0) * df_dM;

        // Analytical dynamic coupling contribution (Perplexity §3.3):
        //   Δr'(M) = +(1/2) · χ(H_eff, M) · dH_dyn/dM
        // χ = dM/dH is the instantaneous susceptibility from the J-A formulas.
        // Compute it via a unit Ḣ probe so it stays well-defined even when the
        // actual Ḣ_eff is momentarily near zero (turnaround, steady state).
        if (dynActive && dH_dyn_dM != 0.0)
        {
            const double probe   = (dHdt_eff < 0.0) ? -1.0 : 1.0;
            const double chi     = computeDMdt(H_eff_new, M_est, probe) / probe;
            g_prime += 0.5 * chi * dH_dyn_dM;
        }

        // Division-by-zero protection
        if (std::abs(g_prime) < 1e-15)
            g_prime = 1e-15;

        const double delta_M_full = -g / g_prime;

        // Damped Newton (Armijo-style line search): accept the full step only
        // if the residual decreases. Otherwise halve lambda up to 4 times.
        double lambda = 1.0;
        double M_trial = M_est + delta_M_full;
        double g_trial = std::numeric_limits<double>::infinity();
        for (int j = 0; j < 4; ++j)
        {
            if (!std::isfinite(M_trial)) { lambda *= 0.5; M_trial = M_est + lambda * delta_M_full; continue; }

            // Re-evaluate dynamic coupling at the trial point.
            double H_dyn_tr = 0.0;
            if (dynActive)
            {
                const double B_try = mu_0 * (H_new + M_trial);
                H_dyn_tr = dynLosses_->evalWithJacobianForNR(B_try).H_dyn;
            }
            const double H_eff_tr   = H_new - H_dyn_tr;
            const double dHdt_eff_t = (H_eff_tr - H_eff_old) / Ts;
            const double f_trial    = computeDMdt(H_eff_tr, M_trial, dHdt_eff_t);
            g_trial = M_trial - M_old - (Ts / 2.0) * (f_trial + dMdt_old);

            if (std::isfinite(g_trial) && std::abs(g_trial) <= std::abs(g))
                break;
            lambda *= 0.5;
            M_trial = M_est + lambda * delta_M_full;
        }

        const double delta_M = lambda * delta_M_full;
        M_est = M_trial;

        // NaN/Inf guard: reject the iteration rather than poison state.
        if (!std::isfinite(M_est))
        {
            M_est = M_old;
            lastConverged = false;
            break;
        }

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

    // Safety clamp: M cannot exceed ±1.1 * Ms (std::clamp passes NaN through,
    // but the isfinite guard above ensures M_est is finite here).
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

    // Bootstrap: on the very first sample after reset(), H_prev = 0 would
    // produce an artificial dH/dt = H/Ts step impulse (at 352.8 kHz oversampled,
    // |dH/dt| ≈ 3.5e7 per unit input — enough to diverge the solver immediately).
    // Seed H_prev with the current H and emit a single silent sample.
    if (firstSample)
    {
        H_prev      = H;
        M_prev      = 0.0;
        dMdt_prev   = 0.0;
        // Seed B_prev with the initial flux at M=0 so the first real sample's
        // trapezoidal Ḃ is computed off a realistic baseline instead of zero.
        Bprev_      = mu_0 * H;
        Hdyn_prev   = 0.0;
        firstSample = false;
        if (dynLosses_)
        {
            // Align DynamicLosses committed state with our bootstrap without
            // advancing Ḃ (would otherwise produce a spurious spike).
            dynLosses_->seedCommittedState(Bprev_, 0.0);
        }
        return 0.0;
    }

    // 2. Solve hysteresis: find M[n] via Newton-Raphson (with H_eff coupling
    //    when DynamicLosses is enabled).
    double M = solveImplicit(H, H_prev, M_prev, dMdt_prev);
    if (!std::isfinite(M))
        M = M_prev;

    // 3. Compute the final B and the committed H_dyn at the solved M.
    double B = mu_0 * (H + M);
    if (!std::isfinite(B))
        B = Bprev_;

    const bool dynActive = (dynLosses_ != nullptr) && dynLosses_->isEnabled();
    double H_dyn_n = 0.0;
    if (dynActive)
    {
        H_dyn_n = dynLosses_->evalWithJacobianForNR(B).H_dyn;
        if (!std::isfinite(H_dyn_n)) H_dyn_n = 0.0;
    }
    const double H_eff_new = H - H_dyn_n;
    const double H_eff_old = H_prev - Hdyn_prev;
    const double dHdt_eff  = (H_eff_new - H_eff_old) / Ts;

    // 4. Memoize f(H_eff_n, M_n, Ḣ_eff_n) for the next trapezoidal step.
    double dMdt = computeDMdt(H_eff_new, M, dHdt_eff);
    if (!std::isfinite(dMdt))
        dMdt = 0.0;

    // 5. Commit DynamicLosses state (advances B_prev and Ḃ_prev in lockstep).
    if (dynActive)
        dynLosses_->commitState(B);

    // 6. Output the flux density B = mu_0 * (H + M), not dB/dt.
    //    Rationale: dB/dt has intrinsic +6 dB/oct response (Faraday). Without
    //    the surrounding circuit that compensates in a real transformer, raw
    //    dB/dt blows up at HF. Outputting B directly gives a frequency-flat
    //    saturating transfer — the "transformer flux saturation" behavior.

    // 7. Update state for next sample (all values verified finite above)
    M_prev    = M;
    H_prev    = H;
    dMdt_prev = dMdt;
    Bprev_    = B;
    Hdyn_prev = H_dyn_n;

    // 8. Magnetic → audio. outputScale = 1 / B_target so that reaching
    //    B = B_target (by calibration design) yields |y| = 1.
    return B * outputScale;
}
