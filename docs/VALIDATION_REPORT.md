# TWISTERION Validation Report

## Test Configuration
- Sample Rate: 44100 Hz | FFT Size: 65536 (Hann window) | THD: H1-H10
- Pass Criteria: Model THD within ±3 dB of datasheet target

## 1. Jensen JT-115K-E (Mic Input, 1:10, Mu-Metal)

**Datasheet reference:** Jensen JT-115K-E application datasheet (Rs=150 Ohm, Rload=150k Ohm referred to secondary)

### Physical Specifications (Datasheet)
| Parameter | Value |
|---|---|
| Turns ratio | 1:10 |
| Core material | 80% NiFe mu-metal (Alloy 4, ASTM A753) |
| Bsat | ~0.70 T |
| Rdc primary | 19.7 Ohm |
| Rdc secondary | 2465 Ohm |
| Source impedance (test) | 150 Ohm |
| Load impedance (test) | 150k Ohm (referred to secondary) |
| Primary inductance (Lp) | ~10 H |

### THD vs Input Level @ 20 Hz (Rs=150 Ohm)
| Input (dBu) | Datasheet (%) | Model (%) | Delta (dB) | Pass |
|---|---|---|---|---|
| -20 | 0.065 | Pending measurement | -- | -- |
| -2.5 | 1.0 | Pending measurement | -- | -- |
| +1.2 | ~4.0 | Pending measurement | -- | -- |

### THD @ 1 kHz (Rs=150 Ohm)
| Input (dBu) | Datasheet (%) | Model (%) | Delta (dB) | Pass |
|---|---|---|---|---|
| -20 | <0.001 | Pending measurement | -- | -- |
| +4 | ~0.01 | Pending measurement | -- | -- |

### Frequency Response
| Parameter | Datasheet Target | Model Status |
|---|---|---|
| Passband flatness (20 Hz - 20 kHz) | +/-0.25 dB | Pending measurement |
| Low -3 dB point | ~2.5 Hz | Pending measurement |
| High -3 dB point | ~140 kHz | Pending measurement |
| CMRR @ 60 Hz | ~110 dB | Out of scope (model is single-ended) |

**Notes:**
- THD at 20 Hz is dominated by core saturation (B-H nonlinearity). This is the primary J-A validation target.
- THD at 1 kHz is extremely low; model accuracy here validates the linear operating region.
- The +1.2 dBu / 20 Hz condition drives the core well into saturation (~4% THD) and exercises the full hysteresis loop.
- The high -3 dB point (140 kHz) is set by the LC resonance network (Lleak=5 mH, Cw=50 pF, Zobel 4.7k/220pF).

## 2. Lundahl LL1538 (Mic Input, Amorphous Mu-Metal)

**Status: Out of scope -- Jensen focus.** Lundahl validation deferred to a future sprint.

Reference targets retained for future use:
| Condition | Datasheet (%) |
|---|---|
| 50 Hz, 0 dBu | 0.2 |
| 50 Hz, +10 dBu | 1.0 |

FR: +/-0.3 dB 10 Hz - 100 kHz. K1=9e-5 (d=0.025 mm amorphous ribbon).

## 3. Neve 10468 / T1444 (Mic Input, 50% NiFe)

**Status: Out of scope -- Jensen focus.** Neve validation deferred to a future sprint.

Reference targets retained for future use:
| Condition | Marinair Catalogue (%) |
|---|---|
| 40 Hz, max input | <0.1 |
| 500 Hz, nominal | <0.01 |
| 1 kHz, nominal | <0.01 |
| 10 kHz, nominal | <0.01 |

FR: +/-0.3 dB 20-20 kHz. Max level: +10 dB @ 40 Hz.

## 4. API AP2503 (Line Output, GO SiFe)

**Status: Out of scope -- Jensen focus.** API validation deferred to a future sprint.

Reference notes retained:
- Ms corrected: 1.2e6 -> 1.5e6 A/m (Bsat = mu_0 x Ms = 1.88 T, matches M6)
- k adjusted: 200 -> 300 (stability margin k / alpha*Ms = 2.0x)
- Impedances: 75 Ohm -> 600 Ohm (1:3)

## 5. Jensen JT-11ELCF (Line Output, 50% NiFe, 1:1)

**Datasheet reference:** Jensen JT-11ELCF application datasheet (bifilar 1:1, Rs=600 Ohm)

### Physical Specifications (Datasheet)
| Parameter | Value |
|---|---|
| Turns ratio | 1:1 (bifilar wound) |
| Core material | 50% NiFe (Alloy 2, ASTM A753) |
| Bsat | ~1.4 T |
| Rdc per winding | 40 Ohm |
| Source impedance (test) | 600 Ohm |
| Load impedance (test) | 600 Ohm |
| Insertion loss | -1.1 dB (600 Ohm bridging) |
| Winding capacitance (Cw) | 22 nF |

### THD vs Frequency (Rs=600 Ohm, Rload=600 Ohm)
| Frequency | Level (dBu) | Datasheet (%) | Model (%) | Delta (dB) | Pass |
|---|---|---|---|---|---|
| 1 kHz | +4 | ~0.005 | Pending measurement | -- | -- |
| 50 Hz | +4 | ~0.05 | Pending measurement | -- | -- |
| 20 Hz | +4 | 0.028 | Pending measurement | -- | -- |
| 20 Hz | +24 | ~1.0 | Pending measurement | -- | -- |

### Frequency Response
| Parameter | Datasheet Target | Model Status |
|---|---|---|
| Low -3 dB (Rs=0) | 0.18 Hz | Pending measurement |
| High -3 dB (Rs=0) | 15 MHz | Pending measurement |
| Low -3 dB (Rs=600 Ohm) | ~1 Hz | Pending measurement |
| High -3 dB (Rs=600 Ohm) | ~100 kHz | Pending measurement |
| Insertion loss (600 Ohm) | -1.1 dB | Pending measurement |

**Notes:**
- The bifilar winding gives ultra-low leakage inductance (2 uH) and very wide bandwidth.
- The 50% NiFe core (Bsat ~1.4 T) can handle +24 dBu at 20 Hz before significant saturation.
- THD at 1 kHz (+4 dBu) should be extremely low (~0.005%), validating the linear region of the J-A model.
- The 20 Hz / +24 dBu test point is a stress test that drives the core near saturation.
- Insertion loss of -1.1 dB is primarily resistive (Rdc=40 Ohm in 600 Ohm circuit).

## 6. K1 Differentiation Table

**Formula:** K1 = d^2 / (12 * rho), where d = lamination thickness [m], rho = resistivity [Ohm*m].

| Material | d (mm) | rho (uOhm*cm) | K1 (computed) | K1 (code) | Relative Eddy Loss | Verified |
|---|---|---|---|---|---|---|
| Jensen (Mu-Metal, 80% NiFe) | 0.10 | 58 | 1.44e-3 | 1.44e-3 | 1.0x (reference) | YES |
| Jensen JT-11ELCF (50% NiFe) | 0.15 | 46 | 4.08e-3 | 4.08e-3 | 2.8x | YES |
| Lundahl (Amorphous ribbon) | 0.025 | 58 | 9.0e-5 | 9e-5 | 1/16x | YES |
| Neve (50% NiFe) | 0.15 | 46 | 4.08e-3 | 4.08e-3 | 2.8x | YES |
| API (GO SiFe) | 0.30 | 48 | 1.56e-2 | 1.56e-2 | 10.8x | YES |
| Fender (M6 CRGO) | 0.35 | 47 | 2.17e-2 | 2.17e-2 | 15.1x | YES |

**Verification:** All K1 values in JAParameterSet.h match the d^2/(12*rho) formula to within rounding precision. The two Jensen presets use different K1 values because they use different core materials (80% NiFe vs 50% NiFe).

## 7. Preset Parameter Summary
| # | Preset | Material | Ms | a | α | k | c | K1 | K2 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Jensen JT-115K-E | Mu-Metal | 5.5e5 | 30 | 1e-4 | 100 | 0.85 | 1.44e-3 | 0.02 |
| 1 | Jensen JT-11ELCF | 50% NiFe | 1.15e6 | 55 | 1e-4 | 150 | 0.70 | 4.08e-3 | 0.06 |
| 2 | Neve 10468 | 50% NiFe | 1.05e6 | 80 | 1e-4 | 500 | 0.70 | 4.08e-3 | 0.06 |
| 3 | Neve LI1166 | 50% NiFe | 1.05e6 | 80 | 1e-4 | 500 | 0.70 | 4.08e-3 | 0.06 |
| 4 | API AP2503 | GO SiFe | 1.5e6 | 100 | 1e-4 | 300 | 0.10 | 1.56e-2 | 0.12 |
| 5 | Lundahl LL1538 | Mu-Metal | 5.5e5 | 25 | 1e-4 | 80 | 0.88 | 9e-5 | 0.005 |
| 6 | Fender Deluxe OT | M6 SiFe | 1.2e6 | 80 | 1e-5 | 50 | 0.20 | 2.17e-2 | 0.15 |
| 7 | Vox AC30 OT | CRGO | 1.1e6 | 180 | 1e-5 | 45 | 0.25 | 1.60e-2 | 0.12 |
| 8 | UTC HA-100X | Mu-Metal | 5.5e5 | 30 | 1e-4 | 150 | 0.85 | 1.44e-3 | 0.02 |
| 9 | Clean DI | Mu-Metal | 5.5e5 | 50 | 1e-4 | 100 | 0.85 | 1.44e-3 | 0.02 |
| 10-14 | Musical presets | (inherit from base) | -- | -- | -- | -- | -- | -- | -- |

## 8. Model vs Datasheet Comparison Summary

### Validated (datasheet targets established)

1. **Jensen JT-115K-E THD @ 20 Hz** -- 3 test points (-20, -2.5, +1.2 dBu) covering linear through heavy saturation. Datasheet targets: 0.065%, 1.0%, ~4.0%.
2. **Jensen JT-115K-E THD @ 1 kHz** -- 2 test points (-20, +4 dBu). Datasheet targets: <0.001%, ~0.01%.
3. **Jensen JT-115K-E frequency response** -- Passband flatness +/-0.25 dB (20-20 kHz), -3 dB at 2.5 Hz and 140 kHz.
4. **Jensen JT-11ELCF THD** -- 4 test points (1 kHz/+4 dBu, 50 Hz/+4 dBu, 20 Hz/+4 dBu, 20 Hz/+24 dBu). Datasheet targets: 0.005%, 0.05%, 0.028%, ~1.0%.
5. **Jensen JT-11ELCF frequency response** -- -3 dB at 0.18 Hz / 15 MHz (Rs=0) and ~1 Hz / ~100 kHz (Rs=600 Ohm). Insertion loss -1.1 dB.
6. **K1 eddy current coefficients** -- All 6 materials verified against d^2/(12*rho) formula. Code matches computed values.

### Pending (model measurements not yet run)

1. All THD model measurements for JT-115K-E (5 test points)
2. All THD model measurements for JT-11ELCF (4 test points)
3. All frequency response measurements for both Jensen transformers
4. Insertion loss measurement for JT-11ELCF
5. Harmonic spectrum analysis (H2/H3 ratio, odd/even balance)
6. Dynamic response (transient fidelity, group delay)

### Out of Scope (deferred -- non-Jensen)

- Lundahl LL1538 -- datasheet targets retained, measurement deferred
- Neve 10468 / T1444 -- Marinair catalogue targets retained, measurement deferred
- API AP2503 -- parameter notes retained, measurement deferred
- Fender Deluxe OT -- no dedicated validation section (guitar amp output, different domain)
- Vox AC30 OT -- no dedicated validation section (guitar amp output, different domain)
- UTC HA-100X -- no dedicated validation section (vintage character preset)
- Clean DI -- no dedicated validation section (transparency preset)

### Known Model Limitations (relevant to Jensen validation)

1. **Single-ended topology:** The model does not simulate differential/balanced operation. CMRR measurements (e.g., JT-115K-E 110 dB @ 60 Hz) are out of scope.
2. **Static J-A hysteresis:** The Jiles-Atherton model is quasi-static with added loss terms (K1, K2). It does not capture rate-dependent hysteresis shape changes that occur at very high drive levels or frequencies above ~50 kHz.
3. **Temperature dependence:** All parameters are room-temperature values. No thermal derating is modeled (mu-metal permeability drops ~10% at 85 degC).
4. **Core geometry approximation:** Both Jensen presets use fitted K_geo values (736 m for JT-115K-E, 5300 m for JT-11ELCF) rather than exact geometric dimensions. This affects Lm accuracy at the margins.
5. **Bifilar winding model:** The JT-11ELCF bifilar winding is approximated as two lumped windings with very low leakage (2 uH). Distributed effects (skin effect, proximity effect at MHz frequencies) are not modeled.
6. **THD floor:** The model's numerical noise floor may limit the ability to accurately reproduce THD values below ~0.001% (relevant for JT-115K-E at 1 kHz / -20 dBu and JT-11ELCF at 1 kHz / +4 dBu).
