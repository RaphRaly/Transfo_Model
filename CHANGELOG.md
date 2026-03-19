# Changelog

All notable changes to the **Transfo_Model** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **TWISTERION branding** -- renamed from "Transformer Model" with "Powered by HysteriCore" tagline.
- **O.D.T Balanced Preamp** (Original Dual Topology) -- dual-transformer preamp engine with two amplifier paths.
- **Heritage Mode** -- 3-transistor Class-A path (BC184C/BC214C/BD139) with 11-position gain switch (+10 to +50 dB).
- **Modern Mode** -- 8-transistor discrete op-amp path (LM-394 diff pair, cascode, VAS, Class-AB output) with load isolation.
- **Preamp controls**: Gain (11-step), Path (Heritage/Modern), Ratio (1:5/1:10), PAD (-20 dB), Phase invert.
- **InputStageWDF** -- T1 input transformer with phantom power, pad, and impedance ratio selection.
- **OutputStageWDF** -- T2 output transformer with cos²/sin² crossfade between amplifier paths.
- **BJTLeaf / BJTCompanionModel** -- Ebers-Moll BJT WDF nonlinear element with companion-source linearisation.
- **WDF preamp stages**: CEStageWDF, EFStageWDF, DiffPairWDF, CascodeStage, VASStageWDF, ClassABOutputWDF.
- **GainTable** -- 11-position Grayhill switch emulation for feedback resistor selection.
- **ABCrossfade** -- smooth equal-power A/B path switching with no transient clicks.
- **LoadIsolator** -- 39 ohm + 40 uH output impedance network for stability.
- **DiodeLeaf** -- WDF diode element for bias networks.
- **DynamicParallelAdaptor** -- multi-way parallel WDF junction.
- **WDFSeriesAdaptor** -- series junction for feedback topologies.
- **Space Grotesk font** -- embedded via JUCE binary data for consistent typography.
- **LevelMeterComponent** -- 30-segment stereo LED bargraph (green/yellow/red).
- **SSL-inspired dark GUI** -- 4-column channel strip layout (Input | Preamp | Output | Analysis).
- **Dynamic magnetizing inductance (Lm)** -- primary inductance varies with incremental permeability for load-dependent bass response.
- **LC parasitic resonance** -- WDFResonanceFilter for interwinding capacitance + leakage inductance peaks.
- 260+ tests including preamp path validation, gain accuracy, crossfade, and full-chain integration.

### Changed

- Plugin GUI redesigned with 4-column SSL-inspired layout and Space Grotesk typography.
- Default engine switched from legacy transformer-only to O.D.T Balanced Preamp.
- Preamp path labels: "Neve" -> "Heritage Mode", "Jensen" -> "Modern".

## [3.0.0] - 2025-01-15

### Added

- **CPWL (Continuous Piecewise-Linear) leaf** with integrated 1st-order ADAA antialiasing -- no oversampling needed in Realtime mode.
- **Realtime processing mode** -- CPWL directional hysteresis + ADAA per WDF leaf, ~9 % CPU mono @ 44.1 kHz on a single i7 core.
- **Physical processing mode** -- full implicit Newton-Raphson Jiles-Atherton solver + 4x polyphase oversampling for offline bounce quality.
- **TMT (Tolerance Modeling Technology)** -- component tolerance spread between L/R channels for natural stereo width (Brainworx-style).
- **B-H scope real-time visualization** (`BHScopeComponent`) with lock-free SPSC queue for thread-safe GUI updates.
- **5 transformer presets:**
  - Jensen JT-115K-E (1:10, mu-metal, microphone / line input)
  - Jensen Harrison Preamp (JT-115K-E in Harrison circuit context, heavy loading)
  - Neve 1073 Input -- Marinair 10468 / Carnhill VTB9045 (1:2, NiFe 50 %)
  - Neve 1073 Output -- LI1166 gapped (step-down, NiFe 50 %)
  - API AP2503 (line output, grain-oriented SiFe)
- **HSIM (Hybrid Scattering-Impedance Method) solver** -- alternates wave-domain forward/backward scans and port resistance adaptation with adaptive interval (every 16 samples).
- **MEJunction** -- novel magneto-electric WDF coupling element converting between electrical and magnetic wave variables via discretised Faraday's and Ampere's laws.
- **LangevinPade [3/3] approximation** -- rational function `x(15+x^2)/(45+6x^2)` replaces transcendental calls on the hot path (~10x faster than `std::tanh`).
- **CMA-ES + Levenberg-Marquardt identification pipeline** -- global + local optimisation for fitting Jiles-Atherton parameters from measured B-H curves.
- **CPWL fitter** (`CPWLFitter`) -- automatic extraction of piecewise-linear segments from a fitted J-A model for the Realtime leaf.
- **Active learning** (`ActiveLearning`) -- CMA-ES ensemble to suggest the next most informative measurement point.
- **SIMD vectorization** -- SSE2 (x86-64) and NEON (ARM / Apple Silicon) intrinsics in `SIMDMath.h`.
- **Sherman-Morrison O(N^2) port adaptation** -- rank-1 scattering matrix update instead of full O(N^3) recomputation.
- **Lock-free SPSC queue** (`SPSCQueue`) for real-time-safe B-H visualisation data transfer.
- **GitHub Actions CI/CD** -- core tests on Ubuntu 22.04, Windows (latest), macOS 13; plugin build on Windows with VST3 artifact upload.
- **AU (Audio Unit) format support** with macOS build guide (`BUILD_AU_MAC.md`).
- **AAX format support** (requires external AAX SDK path).
- Six-layer architecture with strict dependency ordering: `util/ -> magnetics/ -> wdf/ -> dsp/ -> model/ -> plugin/`.
- Header-only `core/` library with zero external dependencies (no Eigen, no JUCE, no Boost).
- CLI tool `simulate` for headless transformer simulation.
- `ConvergenceGuard` and `HSIMDiagnostics` for solver monitoring and debugging.
- `SmallMatrix`, `AlignedBuffer`, `SmoothedValue` utility classes.
- Constraint set with physical penalties for the identification pipeline (`ConstraintSet`).
- Multi-component objective function (THD, coercivity, loop closure) for parameter fitting (`ObjectiveFunction`).

### Changed

- Complete architectural rewrite from Phase 1 monolithic code to layered v3 architecture.
- Plugin name changed to "Transformer Model v3".
- Replaced single legacy HysteresisProcessor with templated `TransformerModel<Leaf>` supporting both `JilesAthertonLeaf` and `CPWLLeaf`.

### Deprecated

- Legacy Phase 1 source files under `Source/` are kept for reference but are no longer part of the active architecture.

## [2.0.0] - 2024-06-01

### Added

- **Jiles-Atherton hysteresis model** with implicit Newton-Raphson solver for accurate magnetic saturation and hysteresis.
- **Wave Digital Filter (WDF) circuit topology** for physically-motivated signal flow.
- **Extrapolative warm-start prediction** to accelerate Newton-Raphson convergence between successive samples.
- **4x oversampling engine** (`OversamplingEngine`) using polyphase filters to suppress aliasing from the nonlinear hysteresis model.
- **Dynamic losses model** (`DynamicLosses`) -- eddy current and excess losses for frequency-dependent core behaviour.
- `JAParameterSet` structure for organising Jiles-Atherton material parameters.

## [1.0.0] - 2024-01-15

### Added

- Initial transformer simulation prototype.
- Basic hysteresis modelling (`HysteresisProcessor`, `HysteresisUtils`).
- JUCE plugin framework integration (VST3 + Standalone).
- Standalone audio processor with simple parameter control.
- Implicit solver foundation (`ImplicitSolver`).
- DC blocker filter (`DCBlocker`).
- Basic oversampling infrastructure (`Oversampling`).

[Unreleased]: https://github.com/TransformerModelProject/Transfo_Model/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/TransformerModelProject/Transfo_Model/compare/v2.0.0...v3.0.0
[2.0.0]: https://github.com/TransformerModelProject/Transfo_Model/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/TransformerModelProject/Transfo_Model/releases/tag/v1.0.0
