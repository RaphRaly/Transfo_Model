# Contributing to Transfo_Model

Thank you for your interest in contributing to the Transfo_Model project. This document covers the development workflow, build instructions, code style, and pull request process.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Building the Project](#building-the-project)
3. [Project Structure](#project-structure)
4. [How to Add a New Transformer Preset](#how-to-add-a-new-transformer-preset)
5. [Code Style Guidelines](#code-style-guidelines)
6. [Testing](#testing)
7. [Pull Request Process](#pull-request-process)
8. [Branching Strategy](#branching-strategy)

---

## Prerequisites

| Requirement | Minimum Version | Notes |
|---|---|---|
| **CMake** | 3.22+ | Build system generator |
| **C++ compiler** | C++17 support required | MSVC 2022, Clang 14+, or GCC 11+ |
| **Git** | 2.x | Source control |
| **JUCE** | 8.0.4 | Auto-fetched via CMake `FetchContent` -- no manual install needed |

### Platform-specific toolchains

- **Windows**: Visual Studio 2022 with "Desktop development with C++" workload.
- **macOS**: Xcode + Command Line Tools (`xcode-select --install`), CMake via Homebrew (`brew install cmake`).
- **Linux (Ubuntu 22.04+)**: `sudo apt install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype6-dev libasound2-dev`.

### Optional

- **AAX SDK** (from Avid) -- only needed if building the AAX plugin format. Pass `-DAAX_SDK_PATH=/path/to/aax` to CMake.
- **Python 3** -- for data analysis scripts in `data/`.

---

## Building the Project

### Plugin (VST3 + Standalone)

#### Windows (MSVC)

```bash
# From the repository root
cmake -B build
cmake --build build --config Release
```

The VST3 plugin is output to `build/TransformerModel_artefacts/Release/VST3/`.

#### macOS (Clang / Xcode)

```bash
# Detect architecture automatically
cmake -B build-mac -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"

cmake --build build-mac --config Release -j$(sysctl -n hw.ncpu)
```

For AU installation and Logic Pro validation, see `BUILD_AU_MAC.md`.

#### Linux (GCC)

```bash
cmake -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j$(nproc)
```

### Tests only (no JUCE dependency)

The test suite builds independently of JUCE, linking only against the header-only `core/` library:

```bash
cd Tests
cmake -B build
cmake --build build --config Release
```

### CLI tool

The `simulate` executable is built alongside the plugin automatically. It links only against `TransformerCore` (no JUCE).

---

## Project Structure

```
Transfo_Model/
|
|-- core/include/core/             # Header-only library, ZERO external deps
|   |-- util/                      # SmallMatrix, AlignedBuffer, SPSCQueue, SIMDMath, SmoothedValue, Constants
|   |-- magnetics/                 # AnhystereticFunctions, HysteresisModel, CPWLLeaf, JilesAthertonLeaf, DynamicLosses
|   |-- wdf/                       # WDOnePort, TopologicalJunction, MEJunction, HSIMSolver, ConvergenceGuard
|   |-- dsp/                       # ADAAEngine, OversamplingEngine
|   +-- model/                     # TransformerModel, TransformerConfig, CoreGeometry, WindingConfig, Presets
|
|-- identification/include/identification/   # Offline identification pipeline
|   |-- CMA_ES.h                   # Global optimiser (log-reparametrised)
|   |-- LevenbergMarquardt.h       # Local polish optimiser
|   |-- CPWLFitter.h               # J-A -> CPWL conversion
|   |-- IdentificationPipeline.h   # Phase 0->3 orchestration
|   +-- ActiveLearning.h           # Suggest next measurement
|
|-- plugin/Source/                  # JUCE plugin (processor, editor, parameters)
|   |-- PluginProcessor.h/cpp
|   |-- PluginEditor.h/cpp
|   |-- BHScopeComponent.h/cpp
|   +-- ParameterLayout.h
|
|-- Tests/                          # Standalone test executables (no JUCE)
|   |-- test_cpwl_adaa.cpp          # 22 ADAA + CPWL tests
|   |-- test_cpwl_passivity.cpp     # 65 passivity, J-A, LangevinPade tests
|   |-- test_hsim_diagnostics.cpp   # HSIM solver tests
|   +-- test_hysteresis.cpp         # Legacy Phase 1 tests
|
|-- data/
|   |-- materials/                  # B-H curve JSON (mu-metal, NiFe-50, permalloy)
|   +-- transformers/               # Transformer config JSON (Jensen, Neve, API)
|
|-- tools/
|   +-- simulate.cpp                # CLI headless simulation tool
|
|-- Source/                          # Legacy Phase 1 code (kept for reference)
|
|-- .github/workflows/ci.yml        # CI pipeline
|-- CMakeLists.txt                   # Root build configuration
|-- BUILD_AU_MAC.md                  # macOS AU build & install guide
|-- CHANGELOG.md                     # Version history
+-- CONTRIBUTING.md                  # This file
```

### Dependency layers (strict ordering)

```
Plugin (JUCE)
   +-- Transformer Model Layer    TransformerModel<Leaf>
         +-- WDF Engine Layer     HSIMSolver, MEJunction, TopologicalJunction
               +-- Magnetics      HysteresisModel, CPWLLeaf, JilesAthertonLeaf
                     +-- DSP      ADAAEngine, OversamplingEngine
                           +-- Utilities   SIMDMath, SmallMatrix, SPSCQueue
```

The `core/` library must never depend on JUCE, Eigen, Boost, or any external library. Standard library and SIMD intrinsics only.

---

## How to Add a New Transformer Preset

Follow these steps to add a new transformer model to the plugin.

### Step 1 -- Gather physical data

Collect the following for your transformer:

- Core geometry (cross-section area, mean path length, stack height, gap length)
- Winding configuration (turns ratio, DC resistance primary/secondary, leakage inductance, inter-winding capacitance)
- Core material type (mu-metal, NiFe 50 %, SiFe, etc.) and B-H curve data
- Load impedance

### Step 2 -- Add core geometry factory method

Edit `core/include/core/model/CoreGeometry.h` and add a new static factory method:

```cpp
static CoreGeometry myNewTransformer()
{
    CoreGeometry g;
    g.crossSectionArea = 0.000125f;  // [m^2]
    g.meanPathLength   = 0.12f;      // [m]
    g.stackHeight      = 0.025f;     // [m]
    g.gapLength        = 0.0f;       // [m] (0 = ungapped)
    return g;
}
```

### Step 3 -- Add winding configuration factory method

Edit `core/include/core/model/WindingConfig.h` and add a new static factory method:

```cpp
static WindingConfig myNewTransformer()
{
    WindingConfig w;
    w.turnsRatio       = 10.0f;      // N_sec / N_pri
    w.dcResistancePri  = 20.0f;      // [Ohm]
    w.dcResistanceSec  = 2500.0f;    // [Ohm]
    // ... other parameters
    return w;
}
```

### Step 4 -- Add the TransformerConfig factory method

Edit `core/include/core/model/TransformerConfig.h` and add a new static factory method:

```cpp
static TransformerConfig MyNewTransformer()
{
    TransformerConfig cfg;
    cfg.name          = "My New Transformer";
    cfg.core          = CoreGeometry::myNewTransformer();
    cfg.windings      = WindingConfig::myNewTransformer();
    cfg.material      = JAParameterSet::defaultMuMetal();  // or appropriate material
    cfg.loadImpedance = 10000.0f;
    return cfg;
}
```

### Step 5 -- Register the preset

Edit `core/include/core/model/Presets.h`:

1. Add an inline convenience function:
   ```cpp
   inline TransformerConfig MyNewTransformer() { return TransformerConfig::MyNewTransformer(); }
   ```

2. Increment `count()` by 1.

3. Add a new `case` in `getByIndex()` and `getNameByIndex()`.

### Step 6 -- Add B-H curve data (if available)

Place the measured B-H data as JSON in `data/materials/` or `data/transformers/`:

```json
{
    "name": "My New Material",
    "B": [0.0, 0.1, 0.3, 0.6, 0.9, 1.1, 1.3],
    "H": [0.0, 5.0, 15.0, 40.0, 100.0, 300.0, 1000.0]
}
```

### Step 7 -- Run the identification pipeline (optional)

If you have measured B-H data and want to fit Jiles-Atherton parameters rather than using default material values, use the `IdentificationPipeline`:

1. Load the measurement data via `MeasurementData`.
2. Run CMA-ES global search followed by Levenberg-Marquardt local polish.
3. Extract the fitted `JAParameterSet` and use it in your `TransformerConfig`.
4. Optionally run `CPWLFitter` to generate the CPWL segments for Realtime mode.

### Step 8 -- Update the plugin UI

If the plugin editor has a hardcoded preset menu, update `plugin/Source/ParameterLayout.h` to include the new preset in the `StringArray`.

### Step 9 -- Test

Build the plugin and verify:
- The new preset appears in the preset selector.
- Audio passes through without artefacts in both Realtime and Physical modes.
- CPU usage is within acceptable range.

---

## Code Style Guidelines

### Language standard

- **C++17** is the project standard. Do not use C++20 features.
- Use `#pragma once` for header guards.

### Architecture rules

- The `core/` library is **header-only** with **zero external dependencies**. This is a hard constraint. Do not introduce includes for JUCE, Eigen, Boost, or any third-party library in `core/`.
- The `identification/` module may use Eigen on the cold path (parameter fitting), but it must not be required at runtime.
- Use **CRTP (Curiously Recurring Template Pattern)** over virtual dispatch for performance-critical leaf types. The `TransformerModel<Leaf>` template is the canonical example.

### Naming conventions

| Element | Convention | Example |
|---|---|---|
| Classes / structs | PascalCase | `HysteresisModel`, `CPWLLeaf` |
| Functions / methods | camelCase | `processSample()`, `getByIndex()` |
| Member variables | camelCase (no prefix) | `crossSectionArea`, `turnsRatio` |
| Constants | camelCase or UPPER_CASE | `kDefaultSampleRate`, `MAX_ITERATIONS` |
| Namespaces | lowercase | `transfo`, `transfo::Presets` |
| Files | PascalCase `.h` / `.cpp` | `HSIMSolver.h`, `PluginProcessor.cpp` |

### Formatting

- Indentation: **4 spaces** (no tabs).
- Braces: opening brace on the **same line** for control structures, **next line** for function/class definitions is acceptable.
- Line length: aim for **100 characters**, hard limit at **120**.
- Use `// comment` for inline comments, `// ====` section separators for major blocks.

### Performance considerations

- No heap allocations on the audio thread. Use `AlignedBuffer` or stack allocation.
- No `std::mutex`, `std::atomic` with `memory_order_seq_cst`, or system calls on the audio thread.
- Use `SPSCQueue` for real-time thread to GUI communication.
- Prefer SIMD intrinsics (via `SIMDMath.h`) over scalar loops for hot paths.
- The `LangevinPade` rational approximation exists specifically to avoid transcendental function calls -- do not replace it with `std::tanh` or `std::cosh`.

### Documentation

- Every public header should have a file-level comment block explaining purpose, location in the architecture, and key design decisions.
- Non-obvious algorithms (e.g., Sherman-Morrison update, ADAA integral evaluation) must have inline comments with references to papers or equations.

---

## Testing

### Test executables

The project uses a lightweight custom test framework (no Google Test dependency). There are three test executables:

| Executable | Tests | Coverage |
|---|---|---|
| `test_cpwl_adaa` | 22 | ADAA antiderivative continuity, 1st/2nd-order correctness, CPWL direction switching, alias suppression |
| `test_cpwl_passivity` | 65 | Passivity enforcement, J-A stability condition, log-space round-trip, LangevinPade properties, HysteresisModel commit/rollback, DynamicLosses |
| `test_hsim_diagnostics` | -- | HSIM solver convergence and diagnostics |

### Running tests locally

```bash
# Build
cd Tests
cmake -B build
cmake --build build --config Release

# Run individually
./build/Release/test_cpwl_adaa        # or .\build\Release\test_cpwl_adaa.exe on Windows
./build/Release/test_cpwl_passivity
./build/Release/test_hsim_diagnostics

# Or use CTest
cd build
ctest -C Release --output-on-failure
```

### Adding a new test

1. Create a new `.cpp` file in `Tests/` (e.g., `test_my_feature.cpp`).
2. Include the relevant `core/` headers.
3. Write test functions using assertion macros or simple `assert()` / `return 1` on failure.
4. Register the executable in `Tests/CMakeLists.txt`:

```cmake
add_executable(test_my_feature test_my_feature.cpp)
target_include_directories(test_my_feature PRIVATE ${CORE_INCLUDE})

add_test(NAME my_feature COMMAND test_my_feature)
```

5. Verify it runs in CI by pushing to a feature branch and checking the GitHub Actions output.

### CI pipeline

The GitHub Actions workflow (`.github/workflows/ci.yml`) runs on every push and pull request to `main` / `master`:

1. **Core tests** -- built and run on Ubuntu 22.04, Windows (latest), and macOS 13. No JUCE dependency.
2. **Plugin build** -- full VST3 + Standalone build on Windows (latest). Depends on core tests passing. Uploads the VST3 artifact.

All tests must pass before a pull request can be merged.

---

## Pull Request Process

1. **Create a feature or bugfix branch** from `develop` (see [Branching Strategy](#branching-strategy)).

2. **Make your changes.** Follow the code style guidelines above.

3. **Run the test suite locally** and confirm all tests pass:
   ```bash
   cd Tests && cmake -B build && cmake --build build --config Release
   cd build && ctest -C Release --output-on-failure
   ```

4. **Build the plugin** and verify it loads correctly in a DAW or Standalone mode.

5. **Update documentation** if your change affects:
   - The public API of `core/` or `identification/`
   - Build instructions or prerequisites
   - Preset list or parameter layout

6. **Update `CHANGELOG.md`** under the `[Unreleased]` section with a summary of your changes.

7. **Push your branch** and open a pull request against `develop`.

8. **Fill in the PR description:**
   - What does this PR do?
   - Why is this change needed?
   - How was it tested?
   - Any performance implications?

9. **Address review feedback.** At least one team member must approve before merge.

10. **Squash-merge** into `develop` with a clear commit message.

---

## Branching Strategy

The project follows a Git Flow-inspired branching model:

```
main
 |
 +-- develop
      |
      +-- feature/cpwl-improvements
      +-- feature/new-preset-xyz
      +-- bugfix/convergence-guard-crash
      +-- bugfix/adaa-discontinuity
```

### Branch types

| Branch | Purpose | Branches from | Merges into |
|---|---|---|---|
| `main` | Production-ready releases. Tagged with version numbers. | -- | -- |
| `develop` | Integration branch. All features merge here first. | `main` | `main` (via release) |
| `feature/*` | New features or enhancements. | `develop` | `develop` |
| `bugfix/*` | Bug fixes. | `develop` | `develop` |
| `hotfix/*` | Critical production fixes. | `main` | `main` + `develop` |
| `release/*` | Release preparation (version bump, final testing). | `develop` | `main` + `develop` |

### Naming conventions for branches

- `feature/short-description` -- e.g., `feature/add-lundahl-preset`
- `bugfix/short-description` -- e.g., `bugfix/hsim-nan-on-silence`
- `hotfix/short-description` -- e.g., `hotfix/crash-on-sample-rate-change`
- `release/vX.Y.Z` -- e.g., `release/v3.1.0`

### Release workflow

1. Create `release/vX.Y.Z` from `develop`.
2. Update version in `CMakeLists.txt` (`project(TransformerModel VERSION X.Y.Z ...)`).
3. Move `[Unreleased]` entries in `CHANGELOG.md` to the new version section.
4. Final testing and bug fixes on the release branch.
5. Merge into `main` and tag `vX.Y.Z`.
6. Merge back into `develop`.

---

## Questions?

If you have questions about the architecture, the physics model, or anything else, open an issue on the repository or reach out to the team.
