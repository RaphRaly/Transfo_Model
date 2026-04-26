# Newton-Raphson Solver Divergence in Jiles-Atherton Hysteresis Model

## CONTEXT

I'm building an audio transformer simulator (C++17, real-time DSP). The core magnetic model is a **Jiles-Atherton (J-A) hysteresis model** solved via implicit Newton-Raphson with H-domain trapezoidal integration.

The model works perfectly in "Artistic" mode (large H values, hScale ≈ 150), but in "Physical" calibration mode (physics-based hScale ≈ 0.121, meaning very small H values around 0.001–0.05 A/m), the NR solver **diverges at sample 2**, producing M = 2363 instead of the expected ≈ 37.

## THE BUG

After `reset()`, all state is zeroed:
```
M_committed_ = 0, M_prev_committed_ = 0, H_prev_ = 0, dMdH_prev_ = 0
```

Processing a 1 kHz sine at small amplitude (Physical mode, hScale = 0.121):

- **Sample 1**: H_new ≈ 0.0017 A/m → NR converges → **M = 18.52** ✓
- **Sample 2**: H_new ≈ 0.0034 A/m → NR **diverges** → **M = 2363** ✗ (expected ≈ 37)

This creates a massive magnetization spike that, after passing through the output normalization (bNorm ≈ 608) and HP filter (τ = Lm/Rs = 10/170 = 59ms), produces a 1.8V transient requiring ~130,000 samples (~3 seconds) to decay.

In Artistic mode (hScale ≈ 150), H values are ~1000× larger, so the same spike (if it occurs) is negligible relative to the signal.

## J-A PARAMETERS (defaultMuMetal — Jensen JT-115K-E transformer)

```cpp
Ms    = 5.5e5f   // Saturation magnetization [A/m]
a     = 30.0f    // Anhysteretic shape [A/m]
alpha = 1e-4f    // Inter-domain coupling [-]
k     = 100.0f   // Pinning coefficient [A/m]
c     = 0.85f    // Reversibility ratio [0,1]
K1    = 1.44e-3f // Classical eddy current loss (DISABLED in Physical mode)
K2    = 0.02f    // Excess loss (DISABLED in Physical mode)
```

**Key derived quantities:**
- χ₀_raw = c·Ms/(3·a) = 0.85 × 5.5e5 / 90 = 5194
- χ_eff = χ₀ / (1 - α·χ₀) = 5194 / (1 - 0.5194) = **10808**
- Stability: k = 100 > α·Ms = 55 ✓

## THE SOLVER CODE (complete — HysteresisModel.h)

```cpp
// ─── Core computation: dM/dH (J-A ODE) ─────────────────────────────────
// dM/dH = [(1-c)(Man - M)] / [delta*k - alpha*(Man - M)]
//          + c * dMan/dHeff
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
    if (std::abs(denominator) < 1e-12)  // kEpsilonD
        denominator = 1e-12;

    return dMdH_total / denominator;
}
```

```cpp
// ─── Analytical Jacobian d(dM/dH)/dM ────────────────────────────────
double computeAnalyticalJacobian(double M, double H, int delta) const
{
    const double Heff = H + params_.alpha * M;
    const double x = Heff / params_.a;

    const double Man = params_.Ms * anhyst_.evaluateD(x);
    const double dManHeff = (params_.Ms / params_.a) * anhyst_.derivativeD(x);

    // d²Man/dHeff² via FD on the anhysteretic derivative
    const double dx = 1e-6;
    const double dMan_p = (params_.Ms / params_.a) * anhyst_.derivativeD(x + dx);
    const double dMan_m = (params_.Ms / params_.a) * anhyst_.derivativeD(x - dx);
    const double d2ManHeff2 = (dMan_p - dMan_m) / (2.0 * dx * params_.a);

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
```

```cpp
// ─── Implicit solve: find M[n] given H[n] ──────────────────────────────
// H-domain trapezoidal rule: ΔM = ½(F[n] + F[n-1])·ΔH
double solveImplicitStep(double H_new)
{
    const double dH = H_new - H_prev_;
    const int newDelta = (dH >= 0.0) ? 1 : -1;

    // Warm-start: extrapolative predictor
    double M_est = 2.0 * M_committed_ - M_prev_committed_;

    // Soft-recovery from deep saturation
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
    for (int i = 0; i < maxIter_; ++i)   // maxIter_ = 8
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

        // Damping when step is too large
        if (std::abs(delta_M) > std::abs(M_est) * 0.5 + 1.0)
        {
            delta_M *= 0.5;
            lastConvMode_ = ConvMode::DampedNR;
        }

        M_est += delta_M;

        const double tol = std::max(tolerance_, tolerance_ * std::abs(M_est));
        if (std::abs(delta_M) < tol)   // tolerance_ = 1e-12
        {
            lastConverged_ = true;
            break;
        }
    }

    // ── Bisection fallback on NR failure ──────────────────────────
    if (!lastConverged_)
    {
        lastConvMode_ = ConvMode::Bisection;
        const double Ms_d = static_cast<double>(params_.Ms);
        double M_lo = -1.1 * Ms_d;
        double M_hi =  1.1 * Ms_d;

        auto gFunc = [&](double M_try) -> double {
            const double dMdH_try = computeRHS(M_try, H_new, newDelta);
            return M_try - M_committed_ - 0.5 * (dMdH_try + dMdH_prev_) * dH;
        };

        double g_lo = gFunc(M_lo);
        for (int bi = 0; bi < 8; ++bi)
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
        lastIterCount_ += 8;
    }

    // Safety clamp
    M_est = std::clamp(M_est, -1.1 * static_cast<double>(params_.Ms),
                                1.1 * static_cast<double>(params_.Ms));

    M_tentative_ = M_est;
    H_tentative_ = H_new;
    delta_ = (dH >= 0.0) ? 1 : -1;

    return M_est;
}
```

```cpp
// ─── State management ─────────────────────────────────────────────────
void commitState()
{
    M_prev_committed_ = M_committed_;
    M_committed_ = M_tentative_;
    H_prev_ = H_tentative_;

    // Store dM/dH for next H-domain trapezoidal step
    dMdH_prev_ = computeRHS(M_committed_, H_tentative_, delta_);
}
```

## ANHYSTERETIC FUNCTION (double-precision path used by solver)

```cpp
// L(x) = coth(x) - 1/x  (Langevin function)
double evaluateDImpl(double x) const {
    const double ax = std::abs(x);
    if (ax < 1e-8)   return x / 3.0;
    if (ax > 20.0)   return (x > 0.0) ? 1.0 : -1.0;
    return 1.0 / std::tanh(x) - 1.0 / x;
}

double derivativeDImpl(double x) const {
    const double ax = std::abs(x);
    if (ax < 1e-8)   return 1.0 / 3.0;
    if (ax > 20.0)   return 0.0;
    const double sh = std::sinh(x);
    return 1.0 / (x * x) - 1.0 / (sh * sh);
}
```

## CALLING CONTEXT (TransformerModel cascade processing)

The solver is called per-sample in the audio processing loop:
```cpp
// Bertotti dynamic losses are DISABLED in Physical mode (already fixed)

const float H_applied = x * hScale_;  // hScale_ = 0.121
double H_eff = static_cast<double>(H_applied);

const double M = directHyst_.solveImplicitStep(H_eff);
directHyst_.commitState();

const float B = kMu0f * (H_applied + static_cast<float>(M));
wet = B * bNorm_;   // bNorm_ ≈ 608

// Then: HP filter → LC filter → output
```

`prepareToPlay()` calls `configureCircuit()` which calls `directHyst_.reset()`, zeroing all state.

## MY ANALYSIS SO FAR

### The dMdH_prev_ discontinuity

The root cause seems to involve the H-domain trapezoidal rule's dependency on `dMdH_prev_`:

1. After `reset()`: `dMdH_prev_ = 0`
2. **Sample 1**: trapezoidal integration uses `0.5*(dMdH_new + 0)*dH`. Since `dMdH_prev_ = 0`, only half the slope is applied. Result: M ≈ 18.52 (should be ≈ 37 for full χ_eff·dH integration).
3. `commitState()` computes `dMdH_prev_ = computeRHS(18.52, H1, 1) ≈ 10808`. This is a **massive discontinuity** from 0 to 10808.
4. **Sample 2**: NR now uses `dMdH_prev_ = 10808`, and the solver diverges.

### What I expect to happen at Sample 2 (but doesn't)

- Warm-start: `M_est = 2×18.52 - 0 = 37.04`
- `dH ≈ 0.0017`
- In the linear region (x = Heff/a ≈ 0.001), `computeRHS` returns ≈ 10808 regardless of M (because L'(x) ≈ 1/3 for all small x)
- So `g(M) ≈ M - 18.52 - 0.5*(10808 + 10808)*0.0017 ≈ M - 18.52 - 18.4`
- Root at M ≈ 36.9, g_prime ≈ 1 (since dMdH barely depends on M in the linear region)
- NR should converge in 1 iteration

**But instead, M = 2363.** I cannot reproduce this divergence by hand calculation.

### Hypotheses

1. **Multiple roots of g(M)**: Could the residual function `g(M) = M - M_c - 0.5*(f(M,H) + f_prev)*dH` have spurious roots at large M? In the linear region dMdH ≈ const ≈ 10808, so g is essentially linear — only one root.

2. **Irreversible component switching**: At M=37, `diff = Man - M = (Ms·L(x) - 37)`. Since Man ≈ Ms·x/3 ≈ 43 at H=0.0034, diff > 0 and irreversible is ON. But if the NR overshoots to large M where diff < 0, irreversible switches OFF, changing the slope. Could this create a parasitic fixed point?

3. **Jacobian singularity at tiny x**: The analytical Jacobian uses FD for d²Man/dHeff². At x ≈ 0.001, the derivative is essentially constant (1/3), so d²Man ≈ 0. Jacobian ≈ 0. Then g_prime ≈ 1, which seems fine.

4. **Warm-start + trapezoidal interaction**: The extrapolative warm-start `2·M_c - M_prev` = 37.04 might land in a region where the NR trajectory diverges before finding the correct root. But g(37) ≈ 0.36 and g_prime ≈ 1, so delta_M ≈ -0.36 → converges.

5. **delta (sign of dH) flipping**: Could there be an issue with the sign logic when `dH` is very small and positive?

6. **The damping condition**: `|delta_M| > |M_est|*0.5 + 1.0`. With M_est = 37 and delta_M = -0.36, threshold = 19.5, no damping triggered. Fine.

## WHAT I NEED

1. **Find the root cause**: Why does the NR solver converge to M = 2363 instead of M ≈ 37 at sample 2? Please trace through the NR iterations step by step with the actual numerical values.

2. **Propose a fix**: Ideally a fix in `solveImplicitStep()` that handles the `dMdH_prev_ = 0 → 10808` discontinuity at startup without breaking steady-state convergence. Possible approaches:
   - Initialize `dMdH_prev_` to the linear-region susceptibility instead of 0 after reset
   - Clamp `|ΔM|` between consecutive samples (what threshold?)
   - Use backward Euler instead of trapezoidal for the first N samples
   - Blend dMdH_prev_ from 0 to actual value over a few samples
   - Something else?

3. **Verify the fix doesn't break Artistic mode**: hScale ≈ 150, H ≈ 1.5–200 A/m, where the solver currently works perfectly.

## CONSTRAINTS

- This runs in a real-time audio DSP context at 44.1kHz: the fix must be O(1) per sample, no allocations.
- `maxIter_ = 8` NR iterations, `tolerance_ = 1e-12`.
- The bisection fallback (8 iterations over [-1.1·Ms, +1.1·Ms]) should catch divergence, but its M range is ±6.05e5, so 8 bisection steps give resolution ≈ 4700, which might converge to a wrong region.
- The fix should be minimal and not require changing the J-A ODE formulation.
