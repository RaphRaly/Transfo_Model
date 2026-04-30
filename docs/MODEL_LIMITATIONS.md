# TWISTERION — Model Limitations & Honesty Disclaimer

This document lists explicitly what TWISTERION is **not**, so that engineers,
DAW users and reviewers can frame expectations correctly. It is the
companion to `KNOWN_LIMITATIONS.md` (which lists technical edge cases and
roadmap items) — this file focuses on **scope honesty**.

> Status — Sprint A3 (2026-04-30) : the calibration mode previously called
> `Physical` has been renamed to `Artistic`. The name change is deliberate :
> the engine is a coloring-grade transformer simulator, not a predictive
> coupled-circuit solver. The token `Physical` is reserved for a future
> `PhysicalDAE` engine (Sprint A3.5) that will solve the source/winding/core
> system as a true differential-algebraic equation.

---

## 1. What TWISTERION is

A real-time and offline audio transformer **coloring** engine that combines :

- A scalar Jiles-Atherton hysteresis model with corrected α/c convention
  (Sprint A1, see `JA_DERIVATION.md`),
- Bertotti dynamic loss separation as an opposing field pre-J-A
  (Sprint A2, Voie C, with Baghel-Kulkarni implicit decoupling),
- A high-pass / LC resonance shaping cascade for source-impedance and
  parasitic effects,
- Per-preset calibration shims (e.g. `kCascadeEddyFactor`) tuned against
  Jensen / Neve / API datasheets within ±20 % at one or two reference
  operating points.

This is sufficient for production DAW use — mixing, mastering, vocal /
bass / drum coloring, mid-bus glue. It is **not** sufficient for hardware
dimensioning, EMI prediction, or magnetic circuit research.

---

## 2. What TWISTERION is **not**

The current engine **does not** include any of the following. None of these
are bugs ; they are out of scope and require a different architecture
(tracked under Sprint A3.5+).

### 2.1 No coupled source / load / core solver

The cascade `J-A → HP → LC` evaluates the magnetic core, the source
impedance and the load impedance **sequentially**, not as a single coupled
system. Real transformers solve

```
v_source(t) = R_s·i_p + R_cu_p·i_p + L_σp·di_p/dt + N_p·dφ/dt
0           = R_load·i_s + R_cu_s·i_s + L_σs·di_s/dt − N_s·dφ/dt
B(t)        = μ₀·(H(t) + M(t))
H(t)        = (N_p·i_p − N_s·i_s) / l_e
```

**all simultaneously** at each sample. TWISTERION instead uses a fixed
`hScale` calibration plus Bertotti losses, which captures the salient
audio-band coloration but gets dimensional / impedance-loaded behaviour
only approximately right.

→ Real coupled solver tracked as Sprint A3.5 (`PhysicalDAE`).

### 2.2 Scalar J-A only, not vector / not Preisach

The hysteresis model is **scalar** (single-axis B-H). It does not capture :

- **Vector hysteresis** (rotational losses on three-phase or radial-flux
  cores).
- **Asymmetric minor loops** (e.g. Preisach-style memory of every reversal
  point — J-A approximates with M_committed only).
- **Anisotropy** (grain-orientation differences in GO-SiFe).

For pure audio applications this is generally inaudible, but it limits
the model's accuracy in non-sinusoidal regimes with biased excitation.

### 2.3 Bertotti excess loss is an approximation

The excess loss term `H_excess = K2·sign(dB/dt)·√|dB/dt|` is a
phenomenological square-root law (Bertotti 1988). It does **not** model :

- Domain-wall pinning physics in detail,
- The actual statistical distribution of magnetic objects (MOs)
  beyond the lump K2 coefficient,
- Frequency-dependent K2 that real materials sometimes exhibit.

The implicit decoupling factor `(1+χ)/(1+G)` is a one-step linearisation
of the M-feedback loop (Baghel & Kulkarni 2014), not a full Newton-coupled
iteration. Acceptable accuracy for audio-band predictor lag, but degrades
in highly non-linear transients (large-signal step responses).

### 2.4 No explicit copper / leakage coupling

Primary / secondary copper resistance and leakage inductance are
**implicit** in the cascade calibration (`kCascadeEddyFactor`,
`hScale`, `kHpFloorFreqHz`). They are not solved as discrete circuit
elements that exchange energy with the core in the same time step.

Practical consequence : changing the load impedance (`config_.loadImpedance`)
modifies the LC stage and the HP cutoff via cached parameters, but does
**not** modify the J-A solution simultaneously. The bass response and the
saturation knee are calibrated for one load condition per preset.

### 2.5 No magnetostriction, no mechanical resonance

Real transformers exhibit small mechanical deformation under flux (Joule's
magnetostriction), audible as the 50/60 Hz hum of a power transformer and
as subtle harmonic content from the core lamination resonances. None of
this is modeled. Bands of "transformer warmth" attributed to mechanical
vibrations in some hardware analyses are out of scope.

### 2.6 No temperature dependence

Mu-metal and 50 % NiFe permeability changes by tens of percent over
typical studio temperature ranges, and the J-A coercivity / Ms shift with
Curie-point proximity. Temperature is not a parameter (see
`KNOWN_LIMITATIONS.md` §1).

### 2.7 No saturation due to DC offset / remanence

The model has no `H_DC` ampère-turn injection and no measured remanence
`M_r` initial state. Long DC offsets present in microphone-signal chains
will not gradually push the core into asymmetric saturation as a hardware
unit does (this is tracked as Sprint B1 in the engine validity plan).

---

## 3. Tier classification

Following the loose convention used in audio-DSP literature :

| Tier | Definition | TWISTERION |
|------|-----------|------------|
| **Coloring grade** | Reproduces audible character (THD spectrum, FR shape, saturation knee) within ±2 dB / ±20 % vs hardware reference at chosen operating points. | ✅ |
| **Tone-matching grade** | Tracks reference under arbitrary input level, frequency, and load within ±0.5 dB / ±5 % across the entire audio band. | ⚠️ partial — some presets close, others rely on calibration shims that break under load changes. |
| **Predictive grade** | Correctly predicts THD vs level, FR vs load, and B-H trajectory under any plausible excitation, including transients with biased DC. | ❌ — would require A3.5 `PhysicalDAE` solver and per-material identification with full excitation coverage. |
| **Hardware-design grade** | Replaces breadboarding for transformer dimensioning (turns count, lamination thickness, gap geometry). | ❌ out of scope. |

TWISTERION targets **coloring grade for production audio**. Reviewers,
mastering engineers, A/B testers and educators should frame their
listening accordingly.

---

## 4. Where Sprint A3 fits

The Sprint A3 rename of `CalibrationMode::Physical` → `CalibrationMode::Artistic`
(and the analogous `ProcessingMode` rename) is the lexical commitment to
the disclaimer above. The `[[deprecated]]` alias on the old `Physical`
identifier preserves binary compatibility for projects saved with the v3
APVTS state, while signalling to all new code that the symbol is
reserved for the future coupled DAE engine.

If you read this file in 2027 and the code now exposes a real
`Physical` engine again, that engine is the `PhysicalDAE` solver from
Sprint A3.5 ; consult its dedicated docs for its own limitations
(it will have different ones).

---

## 5. References

- `docs/JA_DERIVATION.md` — A1 J-A α/c convention lock.
- `docs/RESEARCH_LM_DYNAMIC.md` — load-dependent LF pole derivation.
- `docs/RESEARCH_MASTERING_DEPTH.md` — five mastering mechanisms scope.
- `docs/SPRINT_THEORY_NOTES.md` — Diag-A / Diag-B theoretical priors.
- `docs/KNOWN_LIMITATIONS.md` — technical edge cases and roadmap.
- Sprint plan : `~/.claude/plans/Sprint Plan — Engine Validity A1-A3,5.txt`
- Bertotti, *J. Appl. Phys.* 64 (1988), DOI 10.1063/1.341783.
- Baghel & Kulkarni, *IEEE Trans. Magn.* 50 (2014), DOI 10.1109/TMAG.2013.2287534.
- Jiles & Atherton, *J. Appl. Phys.* 55 (1984) ; *J. Magn. Magn. Mater.* 61 (1986).
