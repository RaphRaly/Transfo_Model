# TWISTERION — Known Limitations

## 1. Temperature Modeling
No temperature dependence is currently modeled.

- Mu-metal (80% NiFe) permeability varies significantly with temperature and mechanical stress
- Variations of several tens of percent can be observed over typical studio temperature ranges
- The dependency is material- and treatment-specific (Curie temp ~400-460°C)
- Real-world effect: subtle tonal change over session warmup (first 15-30 minutes)

**Planned:** v4+ (per-material thermal coefficient and thermal time constant)

## 2. Cascade Topology vs Full WDF
The production signal path uses a cascade topology (J-A → HP → LC) rather than a unified WDF tree.

- The cascade is computationally efficient but does not capture all bidirectional energy exchange
- The unified WDF circuit (TransformerCircuitWDF) exists but HSIM solver needs further work
- A1.1 fix: HP filter now applied after J-A output for correct bass-saturation interaction

**Status:** WDF circuit available as experimental feature (useWdfCircuit_ flag)

## 3. Static Speaker Impedance for Guitar OT Presets
Guitar output transformer presets (Fender Deluxe, Vox AC30) use a static load impedance (8 Ω).

- Real guitar speakers have frequency-dependent impedance (resonance ~75-100 Hz, rise above ~2-3 kHz)
- This affects the transformer's frequency response and distortion characteristics
- Most audible in the low-midrange interaction

**Planned:** v4+ (speaker impedance curve model as reactive load)

## 4. HSIM Solver — Intentionally Set Aside
The HSIM (Hybrid Scattering-Impedance Method) solver is **intentionally set aside** — not merely dormant.

- Delay-free algebraic loop in MEJunction causes divergence (spectral radius > 1)
- Absolute convergence criterion fails on both large and small signals
- Epsilon relaxation (up to 64x) renders solutions physically meaningless
- MEJunction scattering matrices lack passivity proof
- The cascade approach (J-A → HP → LC) captures the audibly important behaviors without these issues

**Decision:** Deliberately shelved (see ADR-001). The cascade + modular extensions (dynamic Lm, LC resonance) was chosen as the production architecture. HSIM repair is a potential v4+ workstream but is not planned or scheduled. The commit/rollback interfaces in core classes are retained for future compatibility, not because HSIM is expected to return soon.

## 5. Minor Loop Closure (Zirka 2012)
The J-A model's energy derivation uses coenergy rather than energy.

- Minor loops may not close properly in all cases
- Negative susceptibility possible on some trajectory portions
- Loop area (∮H·dB) may not perfectly match static hysteresis loss
- Diagnostic post-check (V2.3) monitors but does not correct this

**Reference:** Zirka, Moroz, Harrison, Chwastek, J. Appl. Phys. 112, 043916 (2012)

## 6. Bertotti Loss Model Frequency Range
The Bertotti three-term loss separation is historically validated for 0-100 Hz.

- At higher frequencies, d²/ρ scaling for classical eddy losses becomes less accurate
- Skin effect and domain wall dynamics introduce corrections
- For audio transformers (20 Hz-20 kHz), adequate for fundamentals and first harmonics

## 7. Identification Pipeline Data Requirements
The CMA-ES + Levenberg-Marquardt pipeline requires measured B-H curve data.

- Factory presets use manually tuned parameters from datasheets
- Optimal parameters require actual measured magnetization curves
