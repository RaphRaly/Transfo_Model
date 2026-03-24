# TWISTERION Validation Report

## Test Configuration
- Sample Rate: 44100 Hz | FFT Size: 65536 (Hann window) | THD: H1-H10
- Pass Criteria: Model THD within ±3 dB of datasheet target

## 1. Jensen JT-115K-E (Mic Input, 1:10, Mu-Metal)

### THD vs Input Level @ 20 Hz
| Input (dBu) | Datasheet (%) | Model (%) | Delta (dB) | Pass |
|---|---|---|---|---|
| -20 | 0.065 | TBD | TBD | TBD |
| -2.5 | 1.0 | TBD | TBD | TBD |
| -4 | ~4.0 | TBD | TBD | TBD |

### THD @ 1 kHz
| Input (dBu) | Datasheet (%) | Model (%) | Pass |
|---|---|---|---|
| -20 | 0.001 | TBD | TBD |

### Frequency Response
- Target: ±0.25 dB 20-20kHz, -3dB @ 2.5Hz / 90kHz
- DLP: +3.5/0° 20-20kHz

## 2. Lundahl LL1538 (Mic Input, Amorphous Mu-Metal)
| Condition | Datasheet (%) | Model (%) | Pass |
|---|---|---|---|
| 50 Hz, 0 dBu | 0.2 | TBD | TBD |
| 50 Hz, +10 dBu | 1.0 | TBD | TBD |

FR: ±0.3 dB 10Hz-100kHz. K1=9e-5 (d=0.025mm amorphous ribbon).

## 3. Neve 10468 / T1444 (Mic Input, 50% NiFe)
| Condition | Marinair Catalogue (%) | Model (%) | Pass |
|---|---|---|---|
| 40 Hz, max input | <0.1 | TBD | TBD |
| 500 Hz, nominal | <0.01 | TBD | TBD |
| 1 kHz, nominal | <0.01 | TBD | TBD |
| 10 kHz, nominal | <0.01 | TBD | TBD |

FR: ±0.3 dB 20-20kHz. Max level: +10 dB @ 40Hz.

## 4. API AP2503 (Line Output, GO SiFe)
- Ms corrected: 1.2e6 → 1.5e6 A/m (Bsat = µ₀×Ms = 1.88 T, matches M6)
- k adjusted: 200 → 300 (stability margin k/α·Ms = 2.0×)
- Impedances: 75Ω → 600Ω (1:3)

## 5. Jensen JT-11ELCF (Line Output, 50% NiFe, 1:1)
| Condition | Datasheet (%) | Model (%) | Pass |
|---|---|---|---|
| 20 Hz, +4 dBu | 0.028 | TBD | TBD |
| 20 Hz, +24 dBu | ~1.0 | TBD | TBD |

FR: 0.18Hz-15MHz. Insertion loss: ~1.1 dB.

## 6. K1 Differentiation Table
| Material | Lamination (mm) | K1 | Relative Eddy Loss |
|---|---|---|---|
| Jensen (Mu-Metal) | 0.10 | 1.44e-3 | 1.0× |
| Lundahl (Amorphous) | 0.025 | 9e-5 | 1/16× |
| Neve (50% NiFe) | 0.15 | 4.08e-3 | 2.8× |
| API (GO SiFe) | 0.30 | 1.56e-2 | 10.8× |
| Fender (M6 CRGO) | 0.35 | 2.17e-2 | 15.1× |

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
| 10-14 | Musical presets | (inherit from base) | — | — | — | — | — | — | — |
