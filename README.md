# Transfo_Model v3

> **Physical audio transformer modelling — JUCE VST3 plugin + identification pipeline**

A research-grade C++ implementation of audio transformer simulation based on the Jiles-Atherton hysteresis model, Wave Digital Filters (WDF), and Antiderivative Antialiasing (ADAA).

---

## ✨ Features

| Feature | Details |
|---|---|
| **Realtime mode** | CPWL + ADAA — no oversampling, ~9% CPU mono @ 44.1 kHz |
| **Physical mode** | Full J-A implicit solver + 4× oversampling — offline bounce quality |
| **Presets** | Jensen JT-115K-E · Neve Marinair LO1166 · API AP2503 |
| **Stereo TMT** | Component tolerance spread L/R for natural stereo width (Brainworx-style) |
| **Parameter identification** | CMA-ES + Levenberg-Marquardt — fit J-A params from measured B-H curves |
| **Plugin formats** | VST3 + Standalone (via JUCE) |

---

## 🏗 Architecture

Six strict dependency layers — `core/` has **zero external dependencies**.

```
Plugin (JUCE)
   └── Transformer Model Layer   TransformerModel<Leaf>
         └── WDF Engine Layer    HSIMSolver, MEJunction, TopologicalJunction
               └── Magnetics     HysteresisModel, CPWLLeaf, JilesAthertonLeaf
                     └── DSP     ADAAEngine, OversamplingEngine
                           └── Utilities   SIMDMath, SmallMatrix, SPSCQueue

Identification (offline)
   └── CMA_ES → LevenbergMarquardt → CPWLFitter → IdentificationPipeline
```

### Processing modes

**Realtime** (monitoring) — CPWL directional hysteresis + 1st-order ADAA per WDF leaf. No oversampling. ~18% stereo CPU on a single i7 core.

**Physical** (bounce/render) — Full implicit Newton-Raphson J-A solver inside each magnetic leaf, 4× polyphase oversampling. ~60% stereo CPU single-core.

---

## 📁 Repository Layout

```
Transfo_Model/
├── core/include/core/
│   ├── util/          SmallMatrix, AlignedBuffer, SPSCQueue, SIMDMath, SmoothedValue, Constants
│   ├── magnetics/     AnhystereticFunctions, HysteresisModel, CPWLLeaf, JilesAthertonLeaf, DynamicLosses
│   ├── wdf/           WDOnePort, TopologicalJunction, MEJunction, HSIMSolver, ConvergenceGuard, HSIMDiagnostics
│   ├── dsp/           ADAAEngine, OversamplingEngine
│   └── model/         TransformerModel, TransformerConfig, CoreGeometry, WindingConfig, ToleranceModel, Presets
│
├── identification/include/identification/
│   ├── MeasurementData.h          Load B-H JSON data
│   ├── ObjectiveFunction.h        Multi-component cost (THD, coercivity, closure…)
│   ├── CMA_ES.h                   Global optimizer (log-reparametrised)
│   ├── LevenbergMarquardt.h       Local polish optimizer
│   ├── CPWLFitter.h               Convert J-A model → realtime CPWL leaf
│   ├── ConstraintSet.h            Physical constraints & penalties
│   ├── IdentificationPipeline.h   Phase 0→3 orchestration
│   └── ActiveLearning.h           CMA-ES ensemble — suggest next measurement
│
├── plugin/Source/
│   ├── PluginProcessor.h/cpp      Audio engine (TransformerModel × 2 stereo)
│   ├── PluginEditor.h/cpp         JUCE GUI
│   └── ParameterLayout.h          APVTS parameter definitions
│
├── Tests/
│   ├── test_hysteresis.cpp        Legacy Phase 1 J-A tests
│   ├── test_cpwl_adaa.cpp         ADAA + CPWL segment tests (22 tests)
│   └── test_cpwl_passivity.cpp    Passivity, J-A validity, LangevinPadé tests (65 tests)
│
├── data/
│   ├── materials/                 B-H curve JSON (mu-metal, NiFe-50, permalloy)
│   └── transformers/              Transformer config JSON (Jensen JT-115K-E…)
│
└── CMakeLists.txt
```

---

## 🔬 Key Technical Innovations

### MEJunction — Magneto-Electric WDF coupling

The core of the transformer model. Converts between electrical wave variables `(ae, be)` and magnetic wave variables `(am, bm)` via Faraday's and Ampere's laws, discretised with the trapezoidal rule. No equivalent exists in `chowdsp_wdf`.

### CPWL + ADAA

The Realtime mode uses a **Continuous Piecewise-Linear** approximation of the hysteresis loop. Each WDF leaf evaluates the integral of the piecewise-linear function analytically (no numerical integration), giving 1st-order ADAA alias suppression with zero oversampling overhead.

### LangevinPadé [3/3]

The anhysteretic function `L(x) = coth(x) − 1/x` is approximated by the rational function `x(15+x²)/(45+6x²)`. No transcendental calls on the hot path — ~10× faster than `std::tanh`.

### HSIMSolver

Hybrid Scattering-Impedance Method. Alternates between wave-domain forward/backward scans and port resistance adaptation. Adaptive interval (every 16 samples) + Sherman-Morrison O(N²) rank-1 scattering matrix update.

---

## ⚙️ Build

**Requirements:** CMake 3.22+, MSVC 2022 (or Clang 14+), JUCE auto-fetched via FetchContent.

```sh
# Configure
cmake -B build

# Build plugin
cmake --build build --config Release

# Build & run tests
cd Tests
cmake -B build
cmake --build build --config Release
.\build\Release\test_cpwl_adaa.exe
.\build\Release\test_cpwl_passivity.exe
```

The `core/` library is **header-only** with zero external dependencies (no Eigen, no JUCE, no Boost).

---

## 🧪 Test Results

```
test_cpwl_adaa      22 passed, 0 failed
test_cpwl_passivity 65 passed, 0 failed
```

Tests cover: ADAA antiderivative continuity, 1st/2nd-order ADAA correctness, CPWL direction switching, alias suppression, passivity enforcement, J-A stability condition, log-space round-trip, LangevinPadé properties, HysteresisModel commit/rollback, and DynamicLosses.

---

## 🎛 Transformer Presets

| Preset | Ratio | Core | Material | Application |
|---|---|---|---|---|
| Jensen JT-115K-E | 1:10 | EI mu-metal | 80% NiFe | Microphone / line input |
| Neve Marinair LO1166 | — | EI NiFe | 50% NiFe | Studio console output |
| API AP2503 | — | EI SiFe | Grain-oriented SiFe | Line output, high drive |

---

## 📚 References

- Jiles & Atherton, *J. Magn. Magn. Mater.* 61 (1986) — J-A hysteresis model
- Parker & Välimäki, *IEEE SPL* (2017) — Antiderivative Antialiasing
- Chowdhury et al., *arXiv:2210.12554* (2022) — chowdsp_wdf patterns
- Werner, *Stanford thesis* — WDF convergence, spectral radius
- Polimi thesis — Multiphysics WD modelling, CPWL passivity
- Magnetic Shields Ltd — B-H curves for mu-metal and NiFe-50
- Jensen datasheets — JT-115K-E THD validation data
- Brainworx Patent US 10,725,727 — TMT stereo tolerance

---

## 📄 License

Research / personal use. Contact author for commercial licensing.
