# ADR-001: Hybrid Architecture for Nonlinear Lm & LC Resonance

**Status**: Accepted
**Date**: 2026-03-15
**Decision makers**: DSP team audit

---

## Context

The project needs two extensions (Sprint Plan Lm/LC):
1. **Nonlinear magnetizing inductance (Lm)** — level-dependent bass response
2. **LC parasitic resonance** — HF character, filter alignment (Bessel/Butterworth)

Two competing architectures exist:

### Architecture A — Existing (v3): TopologicalJunction + MEJunction + HSIM
- 9-port magnetic junction with 3 nonlinear reluctances
- MEJunction magneto-electric coupling (Faraday/Ampere with memory waves)
- **Currently bypassed** (commit 5ada102, 2026-03-01) due to delay-free loop causing divergence
- Physically accurate for 3-legged EI cores
- Cannot natively support LC parasitic elements

### Architecture B — Plan: Binary WDF Tree
- Standard series/parallel adaptors with DynamicParallelAdaptor
- WDFResonanceFilter for LC second-order filter
- Supports LC and Zobel naturally
- Loses 3-leg core model (single nonlinear element)

## Decision

**Adopt a Hybrid approach**: Keep the direct J-A bypass as the core nonlinear engine, integrate v4 features (dynamic Lm, LC resonance) as modular extensions.

### Phase 1: Dynamic Lm (Sprint 2)
- Extract J-A susceptibility `dM/dH` from existing HysteresisModel
- Compute `mu_inc = mu0 * (1 + dM/dH)` and `Lm = K_geo * mu_inc`
- Use Lm to dynamically adjust the HP filter cutoff: `fc = R_source / (2*pi*Lm)`
- No HSIM dependency — works with the direct J-A bypass

### Phase 2: LC Resonance Filter (Sprint 3)
- Implement WDFResonanceFilter as a post-stage after J-A processing
- Use DynamicParallelAdaptor for the LC network (Lleak, Ctotal, Zobel)
- Feed Lm-dependent impedance as load reference for Q calculation
- No HSIM dependency — standalone WDF filter

### Phase 3 (Future): HSIM Repair (v4+)
- Fix delay-free loop: add explicit unit delays in backward scan
- Validate MEJunction passivity (prove ||b|| <= ||a||)
- Switch convergence criterion to relative error
- Only attempt after Phase 1-2 are stable and validated

## Rationale

| Criterion | Hybrid | Full HSIM Fix | Full WDF Migration |
|-----------|--------|---------------|-------------------|
| Risk | Low | High | Medium |
| Effort | 2-3 weeks | 4-6 weeks | 4-6 weeks |
| 3-leg model | Preserved (intentionally set aside) | Full | Lost |
| LC support | Yes (post-filter) | Needs extension | Native |
| Dynamic Lm | Yes (HP coupling) | Yes (port Z) | Yes (tree port) |
| Test coverage | Incremental | Major rewrite | Major rewrite |

## HSIM Bypass Root Cause (for future reference)

The HSIM solver diverges because:

1. **Delay-free loop**: MEJunction's instantaneous scattering terms (`S_ME_`) create a same-sample algebraic loop coupling all 3 nonlinear reluctances through the 9-port junction.

2. **Convergence criterion**: Uses absolute error (`|b - a_prev| < eps`) without normalization. Fails on both very large and very small signals.

3. **Epsilon relaxation**: After 3 consecutive failures, epsilon doubles (up to 64x). At `64 * 1e-5 = 6.4e-4`, convergence is effectively disabled.

4. **MEJunction validation**: The 2x2 scattering matrices lack a published derivation or passivity proof. Numerical conditioning is poor when `Ze` and `nt^2/Ts^2` span many orders of magnitude.

### Repair path (when attempted):
- Break delay-free loop: option A (unit delays in backward scan) or option B (memory-only MEJunction scattering)
- Switch to relative convergence: `|b - a_prev| / (|a_prev| + eps_floor)`
- Cap epsilon relaxation at 4x, not 64x
- Add spectral radius monitoring in Release builds
- Validate MEJunction against SPICE reference

## K_geo Units Clarification

K_geo = N^2 * A_eff / l_eff has units of **meters [m]**, not Henries.
Lm = K_geo [m] * mu_inc [H/m] = [H].
Preset values are **fitted constants**, not computed from geometry.
JSON key renamed from `K_geo_H` to `K_geo_m` (backward-compatible loading).

## Consequences

- HSIM code is intentionally set aside — commit/rollback interfaces retained for future compatibility
- Sprint 2-3 can proceed without blocking on HSIM repair
- Architecture B components (DynamicParallelAdaptor, WDFResonanceFilter) are developed as standalone modules
- Future HSIM repair is an independent workstream

## Files Affected

- `TransformerGeometry.h` — K_geo documentation fixed (P0-3, done)
- `TransformerConfig.h` — Preset unit comments fixed (P0-3, done)
- `PresetSerializer.h` — JSON key `K_geo_H` -> `K_geo_m` (P0-3, done)
- `PresetLoader.h` — Backward-compatible loading (P0-3, done)
- `TransformerModel.h` — Will integrate dynamic Lm (Sprint 2) and LC filter (Sprint 3)
