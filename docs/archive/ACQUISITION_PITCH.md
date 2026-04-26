# TWISTERION — Acquisition Pitch

## Executive Summary
Physics-based audio transformer modeling plugin delivering studio-grade saturation matched to datasheets within ±3 dB THD accuracy. 15 factory presets spanning mic inputs, line outputs, and guitar amp OTs.

## Unique Selling Propositions

### 1. Dual-Topology Processing
- **Realtime**: CPWL + ADAA, <1 sample latency, ~18% stereo CPU
- **Physical**: Full J-A implicit solver + 4x oversampling, reference-grade accuracy

### 2. Physics-Based (Not Convolution)
- Jiles-Atherton hysteresis with analytical Jacobian NR solver + bisection fallback
- Bertotti dynamic loss separation (classical eddy + excess) coupled to LC Q
- Dynamic magnetizing inductance (Lm varies with signal level)
- LC parasitic resonance with optional Zobel damping

### 3. Validated Against Datasheets
| Transformer | THD Target | Validation |
|---|---|---|
| Jensen JT-115K-E | 0.065% @ 20Hz/-20dBu | ±3 dB |
| Lundahl LL1538 | 0.2% @ 50Hz/0dBu | K1 differentiated (16× less eddy) |
| Neve 10468 (T1444) | <0.1% @ 40Hz | Marinair catalogue specs |
| Jensen JT-11ELCF | 0.028% @ 20Hz/+4dBu | ±3 dB |

### 4. Real-Time Visualization
- Live B-H hysteresis loop display
- Saturation percentage meter (green→yellow→red)
- Engineering info panel (NR iterations, dynamic Lm)

### 5. Platform Scalability
- CMA-ES + LM identification pipeline fits any measured transformer
- JSON preset format for community sharing
- 260+ unit tests, pluginval level 5 CI/CD

## Competitive Differentiators
| Feature | TWISTERION | IR-based | Static Waveshaper |
|---|---|---|---|
| Signal-dependent response | Yes (J-A) | No | Partial |
| Frequency-dependent saturation | Yes (dynamic Lm + LC) | No | No |
| Dynamic losses (Bertotti) | Yes | No | No |
| Physical parameter fitting | Yes (CMA-ES) | N/A | No |
| Real-time B-H scope | Yes | No | No |

## CPU Benchmarks (i7, 44.1 kHz, 512 samples)
| Mode | Stereo | Target |
|---|---|---|
| Realtime | ~18% | < 25% |
| Physical 2x OS | ~35% | < 40% |
| Physical 4x OS | ~60% | Reference |

## Market Position
- **Target**: Professional audio engineers valuing authentic transformer coloration
- **Price**: $149-$199
- **Format**: VST3, AU, AAX, Standalone
- **15 factory presets** with musical naming (Vintage Warm, Crystal Clear, Tweed Breakup, etc.)
