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

## 3. HSIM Solver — Intentionally Set Aside
The HSIM (Hybrid Scattering-Impedance Method) solver is **intentionally set aside** — not merely dormant.

- Delay-free algebraic loop in MEJunction causes divergence (spectral radius > 1)
- Absolute convergence criterion fails on both large and small signals
- Epsilon relaxation (up to 64x) renders solutions physically meaningless
- MEJunction scattering matrices lack passivity proof
- The cascade approach (J-A → HP → LC) captures the audibly important behaviors without these issues

**Decision:** Deliberately shelved (see ADR-001). The cascade + modular extensions (dynamic Lm, LC resonance) was chosen as the production architecture. HSIM repair is a potential v4+ workstream but is not planned or scheduled. The commit/rollback interfaces in core classes are retained for future compatibility, not because HSIM is expected to return soon.

## 4. Minor Loop Closure (Zirka 2012)
The J-A model's energy derivation uses coenergy rather than energy.

- Minor loops may not close properly in all cases
- Negative susceptibility possible on some trajectory portions
- Loop area (∮H·dB) may not perfectly match static hysteresis loss
- Diagnostic post-check (V2.3) monitors but does not correct this

**Reference:** Zirka, Moroz, Harrison, Chwastek, J. Appl. Phys. 112, 043916 (2012)

## 5. Bertotti Loss Model Frequency Range
The Bertotti three-term loss separation is historically validated for 0-100 Hz.

- At higher frequencies, d²/ρ scaling for classical eddy losses becomes less accurate
- Skin effect and domain wall dynamics introduce corrections
- For audio transformers (20 Hz-20 kHz), adequate for fundamentals and first harmonics

## 6. Identification Pipeline Data Requirements
The CMA-ES + Levenberg-Marquardt pipeline requires measured B-H curve data.

- Factory presets use manually tuned parameters from datasheets
- Optimal parameters require actual measured magnetization curves

## 7. JE-990 Harmonic Signature (H2/H3 Inversion)

The JE-990 model produces H2 > H3, whereas the real circuit produces H3 > H2 (push-pull signature).

**Root cause analysis:**
- The VAS (Q6, single-ended PNP common-emitter) is the dominant distortion source
- VAS naturally produces even-order harmonics (H2) due to asymmetric transfer characteristic
- In the real circuit, the feedback loop gain (~125 dB) suppresses H2 by ~60 dB
- Our model achieves ~60 dB loop gain → H2 suppression is only ~30 dB
- The ClassAB output stage generates H3 from crossover distortion, but the model's
  crossover region was too smooth (gm-weighted averaging) to produce significant H3

**Sprint B mitigation:**
- ClassAB now uses Ic-weighted current steering for sharper crossover → more H3
- ClassAB gain is signal-dependent (not fixed 0.95)
- ClassAB is integrated into the Newton solver forward path

**Remaining gap:**
- Loop gain is limited by the WDF one-port BJT model precision
- Full resolution requires multi-port WDF solver (HSIM) — see limitation #3
- Expected: H2/H3 ratio improves from 2.84 to ~1.0-1.5 (partial fix)

## 8. T2 Output Transformer Default Load

The T2 output transformer (Jensen JT-11ELCF) now defaults to 10 kΩ bridging load.

- 600 Ω broadcast line termination is available as a plugin parameter
- 10 kΩ bridging is the modern studio standard (console, DAW, recorder inputs)
- 47 kΩ Hi-Z option available for high-impedance inputs
- The load impedance affects insertion loss and frequency response of T2
