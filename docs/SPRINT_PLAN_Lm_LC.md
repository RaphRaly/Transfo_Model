# Sprint Plan: Nonlinear Lm & LC Parasitic Resonance

**Project**: Transformer Model v3 Extensions
**Date**: 2026-03-15
**Spec**: `nonlinear-lm-and-lc-resonance-extension.md`
**Total effort**: 5 sprints

---

## Sprint 0 — Foundation: Data Structs & Config (no audio changes)

**Goal**: Add all new parameter structs and extend presets without touching the audio path.

### Tickets

| ID | Task | File | Type | Est |
|----|------|------|------|-----|
| S0-1 | Create `LCResonanceParams.h` | `core/include/core/model/LCResonanceParams.h` | NEW | S |
| S0-2 | Create `TransformerGeometry.h` | `core/include/core/model/TransformerGeometry.h` | NEW | S |
| S0-3 | Add `geometry` + `lcParams` fields to `TransformerConfig` | `core/include/core/model/TransformerConfig.h` | MODIFY | M |
| S0-4 | Populate K_geo + LC params in all 9 factory presets | `TransformerConfig.h` factory methods | MODIFY | L |
| S0-5 | Add `defaultHammondSiFe()` material preset | `JAParameterSet.h` | MODIFY | S |
| S0-6 | Add `hammondVoxAC30()` core geometry | `CoreGeometry.h` | MODIFY | S |
| S0-7 | Add Hammond winding config | `WindingConfig.h` | MODIFY | S |
| S0-8 | Add `Hammond_VoxAC30` preset (factory #10) | `Presets.h`, `TransformerConfig.h` | MODIFY | M |
| S0-9 | Extend JSON serializer/loader for new fields | `PresetSerializer.h`, `PresetLoader.h` | MODIFY | M |
| S0-10 | Update existing JSON preset files with LC params | `data/transformers/*.json` | MODIFY | M |

### Acceptance Criteria
- [ ] All 10 factory presets compile and return valid configs
- [ ] `LCResonanceParams::isPhysicallyValid()` passes for all presets
- [ ] `LCResonanceParams::computeFres()` returns expected values (Jensen ~320kHz, Fender ~5kHz)
- [ ] JSON round-trip: save + load preserves all new fields
- [ ] All 6 existing tests still pass (zero regression)

### Preset Parameter Values (from spec)

```
                     K_geo   Lleak     Cw       Cp_s     Rz      Cz       CL     Fres     Q
Jensen JT-115K-E     50.0H   5mH      50pF     10pF     4.7kΩ   220pF    0      ~320kHz  0.577
Jensen Harrison      50.0H   5mH      50pF     10pF     4.7kΩ   220pF    0      ~320kHz  0.577
Neve 1073 Input      30.0H   80µH     200pF    100pF    2.2kΩ   330pF    0      ~40kHz   0.65
Neve 1073 Output     20.0H   1mH      100pF    6pF      0       0        0      (natural) >1
Neve LO2567 Hot      20.0H   1mH      100pF    6pF      0       0        0      (natural) >1
Neve LO1173 Output   15.0H   0.3mH    80pF     5pF      0       0        0      (natural) >1
API AP2503           8.0H    0.5mH    400pF    150pF    1.0kΩ   470pF    0      ~56kHz   0.70
Fender Output        200.0H  500µH    2nF      500pF    0       0        0      ~5kHz    1.5-3.0
Lundahl LL1538       40.0H   50µH     100pF    30pF     3.3kΩ   180pF    0      ~71kHz   0.577
Hammond Vox AC30     150.0H  200µH    1nF      300pF    0       0        0      ~11kHz   1.2-2.0
```

---

## Sprint 1 — WDF Components (standalone, no integration)

**Goal**: Build the two new WDF processing components and validate in isolation.

### Tickets

| ID | Task | File | Type | Est |
|----|------|------|------|-----|
| S1-1 | Create `DynamicParallelAdaptor<N>` | `core/include/core/wdf/DynamicParallelAdaptor.h` | NEW | M |
| S1-2 | Create `WDFSeriesAdaptor` (2-port) | `core/include/core/wdf/WDFSeriesAdaptor.h` | NEW | M |
| S1-3 | Create `WDFResonanceFilter` | `core/include/core/wdf/WDFResonanceFilter.h` | NEW | L |
| S1-4 | Test: DynamicParallelAdaptor unit tests | `Tests/test_dynamic_parallel_adaptor.cpp` | NEW | M |
| S1-5 | Test: LC resonance freq response + Q verification | `Tests/test_lc_resonance.cpp` | NEW | L |
| S1-6 | Register new tests in CMake | `Tests/CMakeLists.txt` | MODIFY | S |

### DynamicParallelAdaptor Design

```cpp
template <int N = 3>
class DynamicParallelAdaptor {
public:
    void setPortImpedance(int port, float R);  // Update single port Z
    void recalculateScattering();               // Recompute alpha[] from R[]
    void scatter(const float* a, float* b);     // b[i] = sum(alpha*a) - a[i]
    float getAdaptedImpedance() const;           // 1/sum(1/R[i])
private:
    std::array<float, N> R_, alpha_;
    float G_sum_;
};
```

### WDFResonanceFilter Topology

```
Input ──[Rs+Rw1]──┬──[Lleak]──┬──[Rw2_ref+RL_ref]──┬── Output
                  │           │                     │
                  │          [Ctotal]            [Zobel RC]
                  │           │                     │
                 GND         GND                   GND

WDF tree:
    Series Adaptor (S1: source + Lleak)
       ├── [Rs+Rw1] AdaptedResistor
       └── Parallel Adaptor (P1: shunt path)
              ├── [Lleak] AdaptedInductor
              ├── [Ctotal] AdaptedCapacitor
              └── Series Adaptor (S2: load + Zobel)
                     ├── [RL_ref] AdaptedResistor
                     └── [Zobel: Rz + Cz] (optional)
```

### Acceptance Criteria
- [ ] DynamicParallelAdaptor: alpha sums to 2.0 for 3-port
- [ ] DynamicParallelAdaptor: passivity (||b|| <= ||a||) for random inputs
- [ ] DynamicParallelAdaptor: impedance update produces correct new alphas
- [ ] WDFResonanceFilter with Jensen params: flat to 20kHz, no peak (Bessel)
- [ ] WDFResonanceFilter with Fender params: peak at ~5kHz, +6-10dB
- [ ] WDFResonanceFilter step response: Jensen no overshoot, Fender ringing
- [ ] All 6 existing tests still pass

---

## Sprint 2 — Part A: Nonlinear Magnetizing Inductance (dynamic HP)

**Goal**: Replace static HP filter coefficient with per-sample dynamic Lm based on J-A dM/dH.

### Tickets

| ID | Task | File | Type | Est |
|----|------|------|------|-----|
| S2-1 | Add `geometry_`, `Lm_smoothed_`, `Lm_min_/max_` to TransformerModel | `TransformerModel.h` | MODIFY | M |
| S2-2 | Add `enableDynamicLm_` feature flag | `TransformerModel.h` | MODIFY | S |
| S2-3 | Compute K_geo from config in `configureCircuit()` | `TransformerModel.h` | MODIFY | S |
| S2-4 | Implement per-sample Lm update in `processBlockPhysical()` | `TransformerModel.h` | MODIFY | L |
| S2-5 | Implement per-sample Lm update in `processBlockRealtime()` | `TransformerModel.h` | MODIFY | L |
| S2-6 | Add `getLastSlope()` to CPWLLeaf for Realtime mu_inc | `CPWLLeaf.h` | MODIFY | S |
| S2-7 | Test: level-dependent frequency response | `Tests/test_nonlinear_lm.cpp` | NEW | L |
| S2-8 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S |

### Core Algorithm (per sample, after J-A solve)

```cpp
// Extract instantaneous susceptibility
const double dMdH = directHyst_.getInstantaneousSusceptibility();
const float mu_inc = kMu0f * (1.0f + static_cast<float>(std::max(0.0, dMdH)));

// Compute dynamic Lm
float Lm = geometry_.K_geo * mu_inc;
Lm = std::clamp(Lm, Lm_min_, Lm_max_);

// Smooth (prevent clicks)
Lm_smoothed_ = smoothCoeff_ * Lm_smoothed_ + (1.0f - smoothCoeff_) * Lm;

// Update HP coefficient
const float RC = Lm_smoothed_ / Rsource_;
hpAlpha_ = RC / (RC + Ts_);
```

### Test: Level-Dependent Frequency Response

```
1. Generate 50 Hz sine at amplitudes: -60, -40, -20, 0, +10, +20 dBu
2. For each, measure gain at 50 Hz relative to 1 kHz (reference)
3. Expected: gain at 50 Hz increases from -60 to 0 dBu (Regime 1→2)
             gain at 50 Hz decreases above +10 dBu (Regime 3, saturation)
4. Material comparison: SiFe should show 5x variation, MuMetal ~2x
```

### Acceptance Criteria
- [ ] At nominal level (0 dBu), HP cutoff matches static Lm baseline (< 1dB diff)
- [ ] At -50 dBu, HP cutoff is measurably higher (bass attenuation)
- [ ] At +20 dBu, HP cutoff rises again (saturation)
- [ ] SiFe preset shows stronger level dependence than MuMetal
- [ ] No clicks or instability on transient input (kick drum)
- [ ] Feature flag OFF: behavior identical to current code
- [ ] All existing tests still pass

---

## Sprint 3 — Part B: LC Parasitic Resonance (WDF filter)

**Goal**: Replace static first-order LP filter with WDFResonanceFilter for accurate HF modeling.

### Tickets

| ID | Task | File | Type | Est |
|----|------|------|------|-----|
| S3-1 | Add `WDFResonanceFilter lcFilter_` to TransformerModel | `TransformerModel.h` | MODIFY | S |
| S3-2 | Add `enableLCResonance_` feature flag | `TransformerModel.h` | MODIFY | S |
| S3-3 | Initialize `lcFilter_` in `configureCircuit()` from `config_.lcParams` | `TransformerModel.h` | MODIFY | M |
| S3-4 | Replace LP filter with `lcFilter_.processSample()` in Physical | `TransformerModel.h` | MODIFY | M |
| S3-5 | Replace LP filter with `lcFilter_.processSample()` in Realtime | `TransformerModel.h` | MODIFY | M |
| S3-6 | Test: frequency response sweep all presets | `Tests/test_lc_resonance.cpp` | EXTEND | L |
| S3-7 | Test: step/square wave response (Bessel vs Butterworth vs underdamped) | `Tests/test_lc_resonance.cpp` | EXTEND | M |
| S3-8 | Test: Zobel on/off comparison | `Tests/test_lc_resonance.cpp` | EXTEND | M |

### Integration in processBlock (simplified)

```cpp
// BEFORE (current code):
lpState_ = (1.0f - lpAlpha_) * wet + lpAlpha_ * lpState_;
wet = lpState_;

// AFTER:
if (enableLCResonance_) {
    wet = lcFilter_.processSample(wet);
} else {
    lpState_ = (1.0f - lpAlpha_) * wet + lpAlpha_ * lpState_;
    wet = lpState_;
}
```

### Expected Frequency Responses

```
Jensen JT-115K-E:   Flat to 20kHz, gentle Bessel rolloff above 100kHz
                    (Fres ~320kHz, Q=0.577 → no resonance in audio band)

Neve 1073 Input:    Flat to 20kHz, sub-Butterworth rolloff at ~40kHz
                    (Q=0.65 → slight presence, no peak)

API AP2503:         Flat to 20kHz, Butterworth rolloff at ~56kHz
                    (Q=0.70 → maximally flat magnitude)

Fender Output:      Resonant peak at ~5kHz (+6-10dB), rapid rolloff above
                    (Q=1.5-3.0 → this IS the "Fender presence")

Lundahl LL1538:     Flat to 20kHz, Bessel rolloff at ~71kHz
                    (Q=0.577 → transparent, like Jensen)

Hammond Vox AC30:   Presence bump at ~11kHz, rolloff above
                    (Q=1.2-2.0 → "chimey" character)
```

### Acceptance Criteria
- [ ] Jensen/Lundahl: no peak in 20Hz-20kHz, flat within ±0.5dB
- [ ] Fender: visible peak at 4-6kHz, +5dB minimum
- [ ] Hammond: visible bump at 9-13kHz
- [ ] API: Butterworth shape (maximally flat, no peak, -3dB at ~56kHz)
- [ ] Jensen step response: zero overshoot
- [ ] Fender step response: >20% overshoot + ringing
- [ ] Zobel ON vs OFF: measurable Q reduction
- [ ] Feature flag OFF: behavior identical to current code
- [ ] All existing tests still pass (zero regression)

---

## Sprint 4 — Integration, Polish & Activation

**Goal**: Enable both features, update all downstream systems, final validation.

### Tickets

| ID | Task | File | Type | Est |
|----|------|------|------|-----|
| S4-1 | Enable `enableDynamicLm_` by default | `TransformerModel.h` | MODIFY | S |
| S4-2 | Enable `enableLCResonance_` by default | `TransformerModel.h` | MODIFY | S |
| S4-3 | Remove feature flags (dead code cleanup) | `TransformerModel.h` | MODIFY | S |
| S4-4 | Update `ToleranceModel.h` with LC tolerance offsets | `ToleranceModel.h` | MODIFY | M |
| S4-5 | Update plugin parameters if needed (Zobel toggle?) | `PluginProcessor.cpp` | MODIFY | M |
| S4-6 | Update existing neve_validation test thresholds if needed | `test_neve_validation.cpp` | MODIFY | M |
| S4-7 | Full regression: run all tests (old + new) | All tests | TEST | M |
| S4-8 | A/B listening test: process music through all 10 presets | Manual | TEST | L |
| S4-9 | CPU profiling: verify < 20% overhead vs baseline | Manual | TEST | M |
| S4-10 | Update root CMakeLists.txt (new headers for IDE) | `CMakeLists.txt` | MODIFY | S |

### Verification Matrix

| Test | Sprint | Validates |
|------|--------|-----------|
| test_dynamic_parallel_adaptor | S1 | WDF adaptor correctness |
| test_lc_resonance | S1, S3 | LC filter freq response, Q, step response |
| test_nonlinear_lm | S2 | Level-dependent bass, 3 regimes |
| test_cpwl_adaa | S4 (regression) | CPWL+ADAA unchanged |
| test_cpwl_passivity | S4 (regression) | Passivity bounds unchanged |
| test_hsim_diagnostics | S4 (regression) | WDF solver components unchanged |
| test_plugin_integration | S4 (regression) | Full pipeline, preset switching |
| test_neve_validation | S4 (regression) | THD within adjusted thresholds |
| test_dynamic_losses | S4 (regression) | Bertotti dynamic losses unchanged |

### CPU Budget (from spec)

```
Component                    Ops/sample   % of total
WDF tree propagation         ~30          15%
J-A NR solver (2-3 iter)     ~50          25%
Bertotti dynamic terms       ~5           3%
mu_inc → Lm → hpAlpha       ~5           3%     ← NEW (Part A)
LC resonance filter          ~30          15%    ← NEW (Part B)
Adaptor recalculation        ~12          6%     ← NEW (Part A, periodic)
Oversampling filter          ~40          20%
Total per OS sample          ~172
```

At OS 4x / 44.1kHz = 176.4kHz → ~30M ops/s/channel. Well within single core budget.

---

## Summary

| Sprint | Focus | New Files | Modified Files | New Tests |
|--------|-------|-----------|----------------|-----------|
| **S0** | Data structs + presets | 2 | 7 | 0 |
| **S1** | WDF components | 3 | 1 | 2 |
| **S2** | Part A: dynamic Lm | 0 | 3 | 1 |
| **S3** | Part B: LC resonance | 0 | 2 | (extend S1) |
| **S4** | Integration + polish | 0 | 5 | 0 (regression) |

**Total**: 5 new files, ~18 modified files, 3 new test files

### Critical Path

```
S0 (config) ──→ S1 (WDF components) ──→ S3 (Part B integration)
                      │
S0 (config) ──→ S2 (Part A integration)
                      │
                      └──→ S4 (activation + polish)
```

S2 and S3 can run **in parallel** after S0+S1 are done.

### Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| WDFResonanceFilter instability at high Q | Audio artifacts | Clamp Q ≤ 5.0, add smoothing |
| Dynamic Lm clicks on transients | Audio artifacts | Exponential smoothing (τ=5ms) |
| Fender preset f_res in audio band | Unexpected HF color | This is correct behavior (spec) |
| Existing test regressions | Broken CI | Feature flags OFF by default until S4 |
| CPU overhead > budget | Performance | Profile early in S1, optimize inner loops |
