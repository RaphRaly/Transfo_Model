# TWISTERION

> **Powered by HysteriCore** -- Physical magnetic transformer & preamp modelling

![TWISTERION GUI](TWISTERION_GUI.png)

TWISTERION is a physics-based audio plugin that models magnetic transformer saturation and a dual-topology microphone preamp from first principles. No impulse responses, no static waveshapers -- real Jiles-Atherton hysteresis, Wave Digital Filters, and Antiderivative Antialiasing running in real time.

---

## Features

| Feature | Details |
|---|---|
| **HysteriCore engine** | Proprietary magnetic hysteresis core -- Jiles-Atherton physics + CPWL/ADAA realtime path |
| **O.D.T Balanced Preamp** | Original Dual Topology -- two transformer stages (T1 input + T2 output) with full preamp circuit |
| **Heritage Mode** | 3-transistor Class-A path -- warm, colored, soft-clipping character |
| **Modern Mode** | 8-transistor discrete op-amp path (JE-990) -- linear, musical, high headroom |
| **Realtime mode** | CPWL + ADAA -- no oversampling, ~9% CPU mono @ 44.1 kHz |
| **Physical mode** | Full J-A implicit solver + 4x oversampling -- offline bounce quality |
| **B-H Scope** | Real-time hysteresis loop visualization (lock-free SPSC queue) |
| **Stereo TMT** | Component tolerance spread L/R for natural stereo width |
| **Identification pipeline** | CMA-ES + Levenberg-Marquardt -- fit J-A params from measured B-H curves |
| **Plugin formats** | VST3, AU, AAX, Standalone (JUCE 8) |

---

## Signal Flow

```
MIC/LINE IN
  |
  v
[Input Gain] --> [T1 Input Transformer (HysteriCore)]
                         |
            +------------+------------+
            |                         |
     Heritage Mode              Modern Mode
    (3-BJT Class-A)         (8-BJT Discrete Op-Amp)
     Q1 CE -> Q2 CE           DiffPair -> Cascode
       -> Q3 EF                -> VAS -> Class-AB
            |                         |
            +------[Crossfade]--------+
                       |
              [T2 Output Transformer (HysteriCore)]
                       |
              [Output Gain] --> [Mix] --> OUT
```

Both paths process in parallel to maintain continuous magnetic flux states. Switching is seamless -- no transients.

---

## Architecture

Seven strict dependency layers -- `core/` has **zero external dependencies**.

```
Plugin (JUCE)
   +-- Preamp Model          PreampModel<Leaf>, InputStage, OutputStage
         +-- Amplifier Paths  NeveClassAPath (Heritage), JE990Path (Modern)
               +-- Transformer Model   TransformerModel<Leaf>, TransformerCircuitWDF
                     +-- WDF Engine     HSIMSolver, MEJunction, TopologicalJunction
                           +-- Magnetics   HysteresisModel, CPWLLeaf, JilesAthertonLeaf
                                 +-- DSP   ADAAEngine, OversamplingEngine
                                       +-- Utilities   SIMDMath, SmallMatrix, SPSCQueue

Identification (offline, cold path)
   +-- CMA_ES -> LevenbergMarquardt -> CPWLFitter -> IdentificationPipeline
```

### Processing Modes

**Realtime** (monitoring) -- CPWL directional hysteresis + 1st-order ADAA per WDF leaf. No oversampling. ~18% stereo CPU on a single i7 core.

**Physical** (bounce/render) -- Full implicit Newton-Raphson J-A solver inside each magnetic leaf, 4x polyphase oversampling. ~60% stereo CPU single-core.

---

## Plugin Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Input Gain** | -40 to +20 dB | 0 dB | Drive level into the transformer |
| **Output Gain** | -40 to +20 dB | 0 dB | Output level compensation |
| **Mix** | 0 -- 100% | 100% | Dry/wet parallel blend |
| **SVU** | 0 -- 5% | 2% | Stereo Variation Units (TMT tolerance spread) |
| **Engine** | O.D.T Balanced Preamp / Legacy | O.D.T | Processing topology |
| **Preamp Gain** | 0 -- 10 (11-position) | 5 | Stepped gain (+10 to +50 dB) |
| **Path** | Heritage Mode / Modern | Heritage | Amplifier topology selection |
| **Ratio** | 1:5 / 1:10 | 1:10 | Input transformer impedance ratio |
| **PAD** | On / Off | Off | -20 dB input attenuation |
| **Phase** | Normal / Invert | Normal | Polarity inversion |

---

## Key Technical Innovations

### HysteriCore -- Magnetic Hysteresis Engine

The core technology. Physically models ferromagnetic hysteresis via the Jiles-Atherton equations (5+2 parameters: Ms, a, k, alpha, c + Bertotti dynamic losses K1/K2). Two execution paths:
- **Physical**: Full implicit Newton-Raphson ODE solver + 4x polyphase oversampling
- **Realtime**: CPWL piecewise-linear approximation with analytical 1st-order ADAA -- zero aliasing, zero oversampling overhead

### MEJunction -- Magneto-Electric WDF Coupling

Novel WDF element (no equivalent in chowdsp_wdf or academic literature). Converts between electrical wave variables and magnetic wave variables via discretised Faraday's and Ampere's laws with trapezoidal integration and unit-delay memory.

### O.D.T -- Original Dual Topology Preamp

Two complete amplifier circuits sharing input/output transformer stages:
- **Heritage**: 3-transistor Class-A cascade (BC184C CE -> BC214C CE -> BD139 EF) with AC-coupled negative feedback. 11-position Grayhill gain switch (+10 to +50 dB).
- **Modern**: 8-transistor discrete op-amp (LM-394 differential pair -> 2N4250A cascode -> VAS -> MJE-181/171 Class-AB output) with load isolation (39 ohm + 40uH).

Both paths use analytical closed-loop gain computation (avoids WDF feedback oscillation) and process simultaneously for seamless switching.

### HSIMSolver

Hybrid Scattering-Impedance Method. Alternates between wave-domain forward/backward scans and port resistance adaptation. Adaptive interval (every 16 samples) + Sherman-Morrison O(N^2) rank-1 scattering matrix update.

### LangevinPade [3/3]

The anhysteretic function `L(x) = coth(x) - 1/x` approximated by `x(15+x^2)/(45+6x^2)`. No transcendental calls on the hot path -- ~10x faster than `std::tanh`.

---

## Repository Layout

```
Transfo_Model/
|-- core/include/core/
|   |-- util/          SmallMatrix, AlignedBuffer, SPSCQueue, SIMDMath, SmoothedValue
|   |-- magnetics/     HysteresisModel, CPWLLeaf, JilesAthertonLeaf, DynamicLosses
|   |-- wdf/           WDOnePort, HSIMSolver, MEJunction, TransformerCircuitWDF, BJTLeaf
|   |-- dsp/           ADAAEngine, OversamplingEngine
|   |-- model/         TransformerModel, TransformerConfig, Presets, ToleranceModel
|   +-- preamp/        PreampModel, NeveClassAPath, JE990Path, InputStage,
|                      OutputStage, CEStageWDF, DiffPairWDF, VASStageWDF,
|                      ClassABOutputWDF, GainTable, ABCrossfade, LoadIsolator
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
|   |-- PluginProcessor.h/cpp      Audio engine (dual-mode stereo processing)
|   |-- PluginEditor.h/cpp         TWISTERION GUI (Space Grotesk, SSL-inspired)
|   |-- BHScopeComponent.h/cpp     Real-time B-H loop visualization
|   |-- LevelMeterComponent.h      30-segment stereo LED bargraph
|   +-- ParameterLayout.h          APVTS parameter definitions
|
|-- plugin/Resources/              Embedded fonts (Space Grotesk)
|-- Tests/                         260+ tests (CPWL, passivity, HSIM, preamp, plugin)
|-- data/                          B-H curves JSON, transformer configs
|-- tools/                         CLI simulator, A/B comparison script
+-- docs/                          SRS, sprint plans, architecture decisions
```

---

## Build

**Requirements:** CMake 3.22+, C++17 compiler (MSVC 2022 / Clang 14+ / GCC 11+). JUCE 8.0.4 is auto-fetched via FetchContent.

```sh
# Build plugin (VST3 + AU + Standalone)
cmake -B build
cmake --build build --config Release

# Build and run core tests (no JUCE dependency)
cd Tests
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The `core/` library is **header-only** with zero external dependencies.

---

## Performance

| Mode | Config | CPU (i7, 44.1 kHz) |
|---|---|---|
| Realtime | CPWL + ADAA, no OS | ~9% mono, ~18% stereo |
| Physical | J-A + OS 4x | ~30% mono, ~60% stereo |
| Latency | Realtime mode | < 1 ms |

---

## Tests

260+ tests covering:
- ADAA antiderivative continuity and alias suppression
- CPWL direction switching and passivity enforcement
- J-A stability, LangevinPade properties, dynamic losses
- HSIM convergence and spectral radius diagnostics
- Preamp paths (Heritage/Modern): gain accuracy, frequency response, THD, crossfade
- Full plugin lifecycle: instantiation, APVTS, mode switching, state save/load, stress tests

---

## References

- Jiles & Atherton, *J. Magn. Magn. Mater.* 61 (1986) -- J-A hysteresis model
- Bertotti, *IEEE Trans. Magn.* (1988) -- Dynamic loss separation
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
