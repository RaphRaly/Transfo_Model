# Transfo_Model v3

[![CI](https://github.com/RaphRaly/Transfo_Model/actions/workflows/ci.yml/badge.svg)](https://github.com/RaphRaly/Transfo_Model/actions/workflows/ci.yml)

> **Physical audio transformer modelling -- JUCE VST3/AU plugin + identification pipeline**

A research-grade C++ implementation of audio transformer simulation based on the Jiles-Atherton hysteresis model, Wave Digital Filters (WDF), and Antiderivative Antialiasing (ADAA).

---

## Features

| Feature | Details |
|---|---|
| **Realtime mode** | CPWL + ADAA -- no oversampling, ~9% CPU mono @ 44.1 kHz |
| **Physical mode** | Full J-A implicit solver + 4x oversampling -- offline bounce quality |
| **5 Presets** | Jensen JT-115K-E, Jensen Harrison, Neve 1073 Input/Output, API AP2503 |
| **Stereo TMT** | Component tolerance spread L/R for natural stereo width |
| **B-H Scope** | Real-time hysteresis loop visualization |
| **Custom presets** | Import/export transformer configs via JSON |
| **Identification** | CMA-ES + Levenberg-Marquardt -- fit J-A params from measured B-H curves |
| **Circuit filters** | HP (source Z / Lp bass rolloff) + LP (load damping HF rolloff) |
| **Plugin formats** | VST3, AU, AAX, Standalone (via JUCE 8) |

---

## Architecture

Six strict dependency layers -- `core/` has **zero external dependencies**.

```
Plugin (JUCE)
   +-- Transformer Model Layer   TransformerModel<Leaf>
         +-- WDF Engine Layer    HSIMSolver, MEJunction, TopologicalJunction
               +-- Magnetics     HysteresisModel, CPWLLeaf, JilesAthertonLeaf
                     +-- DSP     ADAAEngine, OversamplingEngine
                           +-- Utilities   SIMDMath, SmallMatrix, SPSCQueue

Identification (offline)
   +-- CMA_ES -> LevenbergMarquardt -> CPWLFitter -> IdentificationPipeline
```

### Processing modes

**Realtime** (monitoring) -- CPWL directional hysteresis + 1st-order ADAA per WDF leaf. No oversampling. ~18% stereo CPU on a single i7 core.

**Physical** (bounce/render) -- Full implicit Newton-Raphson J-A solver inside each magnetic leaf, 4x polyphase oversampling. ~60% stereo CPU single-core.

---

## Transformer Presets

| Preset | Ratio | Core | Material | Application |
|---|---|---|---|---|
| Jensen JT-115K-E | 1:10 | EI | 80% NiFe (mu-metal) | Microphone / line input |
| Jensen Harrison | 1:10 | EI | 80% NiFe (mu-metal) | Preamp coloration |
| Neve 1073 Input | 1:2 | EI | 50% NiFe (Marinair) | Studio console input |
| Neve 1073 Output | 5:3 | EI | 50% NiFe (gapped) | Studio console output |
| API AP2503 | 1:5 | EI | Grain-oriented SiFe | Line output, high drive |

Custom presets can be imported from JSON files using the `PresetLoader` -- see [CONTRIBUTING.md](CONTRIBUTING.md) for the format.

---

## Plugin Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| Input Gain | -40 to +20 dB | 0 dB | Drive level into the transformer |
| Output Gain | -40 to +20 dB | 0 dB | Output level compensation (+15 dB internal cal.) |
| Mix | 0 -- 100% | 100% | Dry/wet parallel blend |
| SVU | 0 -- 5% | 2% | Stereo Variation Units (TMT tolerance spread) |
| Preset | 0 -- 4 | 0 | Transformer model selection |
| Mode | Realtime / Physical | Realtime | Processing quality |

---

## Repository Layout

```
Transfo_Model/
|-- core/include/core/
|   |-- util/          SmallMatrix, AlignedBuffer, SPSCQueue, SIMDMath, SmoothedValue, Constants
|   |-- magnetics/     AnhystereticFunctions, HysteresisModel, CPWLLeaf, JilesAthertonLeaf, DynamicLosses
|   |-- wdf/           WDOnePort, TopologicalJunction, MEJunction, HSIMSolver, ConvergenceGuard
|   |-- dsp/           ADAAEngine, OversamplingEngine
|   +-- model/         TransformerModel, TransformerConfig, CoreGeometry, WindingConfig,
|                      ToleranceModel, Presets, PresetLoader, PresetSerializer
|
|-- identification/include/identification/
|   |-- CMA_ES.h                   Global optimizer (log-reparametrised)
|   |-- LevenbergMarquardt.h       Local polish optimizer
|   |-- CPWLFitter.h               Convert J-A model -> realtime CPWL leaf
|   |-- ObjectiveFunction.h        Multi-component cost (THD, coercivity, closure)
|   |-- IdentificationPipeline.h   Phase 0->3 orchestration
|   +-- ActiveLearning.h           CMA-ES ensemble -- suggest next measurement
|
|-- plugin/Source/
|   |-- PluginProcessor.h/cpp      Audio engine (TransformerModel x 2 stereo)
|   |-- PluginEditor.h/cpp         JUCE GUI (knobs, combo boxes, B-H scope)
|   |-- BHScopeComponent.h/cpp     Real-time B-H loop visualization
|   +-- ParameterLayout.h          APVTS parameter definitions
|
|-- Tests/
|   |-- test_cpwl_adaa.cpp         ADAA + CPWL segment tests (22 tests)
|   |-- test_cpwl_passivity.cpp    Passivity, J-A validity, LangevinPade tests (65 tests)
|   |-- test_hsim_diagnostics.cpp  HSIM solver diagnostics tests
|   +-- test_plugin_integration.cpp Integration tests (60+ tests)
|
|-- data/
|   |-- materials/                 B-H curve JSON (mu-metal, NiFe-50, permalloy)
|   +-- transformers/              Transformer config JSON + schematics
|
|-- tools/
|   |-- simulate.cpp               CLI offline transformer simulation
|   +-- ab_compare.py              A/B comparison script
|
|-- docs/
|   |-- REQUIREMENTS.md            Software Requirements Specification (SRS)
|   |-- CONTEXT_DIAGRAM.md         System boundaries and external entities
|   +-- USER_STORIES.md            Product backlog with user stories
|
|-- CHANGELOG.md                   Version history (Keep a Changelog)
|-- CONTRIBUTING.md                Build instructions, code style, PR process
+-- CMakeLists.txt                 Main build configuration
```

---

## Key Technical Innovations

### MEJunction -- Magneto-Electric WDF coupling

The core of the transformer model. Converts between electrical wave variables `(ae, be)` and magnetic wave variables `(am, bm)` via Faraday's and Ampere's laws, discretised with the trapezoidal rule. No equivalent exists in `chowdsp_wdf`.

### CPWL + ADAA

The Realtime mode uses a **Continuous Piecewise-Linear** approximation of the hysteresis loop. Each WDF leaf evaluates the integral of the piecewise-linear function analytically (no numerical integration), giving 1st-order ADAA alias suppression with zero oversampling overhead.

### LangevinPade [3/3]

The anhysteretic function `L(x) = coth(x) - 1/x` is approximated by the rational function `x(15+x^2)/(45+6x^2)`. No transcendental calls on the hot path -- ~10x faster than `std::tanh`.

### HSIMSolver

Hybrid Scattering-Impedance Method. Alternates between wave-domain forward/backward scans and port resistance adaptation. Adaptive interval (every 16 samples) + Sherman-Morrison O(N^2) rank-1 scattering matrix update.

### Circuit Impedance Filters

Source impedance / primary inductance interaction modelled as a 1st-order HP filter (bass rolloff). Secondary load / leakage inductance modelled as a 1st-order LP filter (HF damping). Coefficients derived from winding parameters -- zero manual tuning.

---

## Build

**Requirements:** CMake 3.22+, C++17 compiler (MSVC 2022 / Clang 14+ / GCC 11+). JUCE 8.0.4 is auto-fetched via FetchContent.

```sh
# Build plugin (VST3 + Standalone)
cmake -B build
cmake --build build --config Release

# Build and run core tests (no JUCE dependency)
cd Tests
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The `core/` library is **header-only** with zero external dependencies.

For macOS AU builds, see [BUILD_AU_MAC.md](BUILD_AU_MAC.md). For full build instructions on all platforms, see [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Tests

```
test_cpwl_adaa           22 passed
test_cpwl_passivity      65 passed
test_hsim_diagnostics     -  passed
test_plugin_integration  60+ passed
```

Coverage includes: ADAA antiderivative continuity, CPWL direction switching, alias suppression, passivity enforcement, J-A stability condition, LangevinPade properties, HysteresisModel commit/rollback, DynamicLosses, HSIM diagnostics, TransformerModel lifecycle, preset switching, mode switching, parameter ranges, stress tests (DC, hot signals, tiny buffers), and B-H queue correctness.

---

## Performance

| Mode | Config | CPU (i7, 44.1 kHz) |
|---|---|---|
| Realtime | CPWL + ADAA, no OS | ~9% mono, ~18% stereo |
| Physical | J-A + OS 4x | ~30% mono, ~60% stereo |
| Latency | Realtime mode | < 1 ms |

---

## Documentation

| Document | Description |
|---|---|
| [REQUIREMENTS.md](docs/REQUIREMENTS.md) | Formal SRS -- 24 functional + 19 non-functional requirements |
| [CONTEXT_DIAGRAM.md](docs/CONTEXT_DIAGRAM.md) | System boundaries, external entities, data flows |
| [USER_STORIES.md](docs/USER_STORIES.md) | Product backlog -- 20 user stories, 5 epics, MoSCoW prioritization |
| [CHANGELOG.md](CHANGELOG.md) | Version history following Keep a Changelog |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Build guide, code style, adding presets, PR process |
| [BUILD_AU_MAC.md](BUILD_AU_MAC.md) | macOS Audio Units build and validation guide |

---

## References

- Jiles & Atherton, *J. Magn. Magn. Mater.* 61 (1986) -- J-A hysteresis model
- Parker & Valimaki, *IEEE SPL* (2017) -- Antiderivative Antialiasing
- Chowdhury et al., *arXiv:2210.12554* (2022) -- chowdsp_wdf patterns
- Werner, *Stanford thesis* -- WDF convergence, spectral radius
- Polimi thesis -- Multiphysics WD modelling, CPWL passivity
- Magnetic Shields Ltd -- B-H curves for mu-metal and NiFe-50
- Jensen datasheets -- JT-115K-E THD validation data
- Brainworx Patent US 10,725,727 -- TMT stereo tolerance

---

## License

Research / personal use. Contact author for commercial licensing.
