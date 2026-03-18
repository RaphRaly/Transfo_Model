cmake_minimum_required(VERSION 3.22)

project(TransformerModel VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Architecture v3 — Magnetic Transformer Physical Model
#
# Layers (strict dependency order):
#   1. core/         — Header-only, ZERO external dependencies
#   2. identification/ — Links against core/, may use Eigen (cold path)
#   3. plugin/       — JUCE + core/ + identification/
#   4. tools/        — CLI tools (optional)
# =============================================================================

# ─── Fetch JUCE via FetchContent ─────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

# ─── AAX SDK Path (required for Pro Tools AAX builds) ────────────────────────
# Set this to your local AAX SDK path, or pass via cmake -DAAX_SDK_PATH=...
set(AAX_SDK_PATH "" CACHE PATH "Path to the AAX SDK from Avid")
if(AAX_SDK_PATH)
    juce_set_aax_sdk_path(${AAX_SDK_PATH})
endif()

# ─── Core Library (header-only) ──────────────────────────────────────────────
# Zero external dependencies. Standard library + SIMD intrinsics only.
add_library(TransformerCore INTERFACE)

target_include_directories(TransformerCore
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

# ─── Plugin Target ───────────────────────────────────────────────────────────
juce_add_plugin(TransformerModel
    COMPANY_NAME            "TransformerModelProject"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    PLUGIN_MANUFACTURER_CODE Tmfr
    PLUGIN_CODE             Tm01
    FORMATS                 VST3 AU AAX Standalone
    PRODUCT_NAME            "Transformer Model v3"
    # AU-specific metadata (required by Logic Pro / auval)
    AU_MAIN_TYPE            kAudioUnitType_Effect
    DESCRIPTION             "Physical transformer saturation model"
    # AAX-specific metadata (required by Pro Tools)
    AAX_CATEGORY            AAX_ePlugInCategory_Harmonic
)

# ─── Source Files ────────────────────────────────────────────────────────────
target_sources(TransformerModel
    PRIVATE
        # ── Core headers (header-only, listed for IDE visibility) ──
        # util/
        core/include/core/util/Constants.h
        core/include/core/util/SmallMatrix.h
        core/include/core/util/AlignedBuffer.h
        core/include/core/util/SPSCQueue.h
        core/include/core/util/SmoothedValue.h
        core/include/core/util/SIMDMath.h

        # magnetics/
        core/include/core/magnetics/AnhystereticFunctions.h
        core/include/core/magnetics/JAParameterSet.h
        core/include/core/magnetics/HysteresisModel.h
        core/include/core/magnetics/DynamicLosses.h
        core/include/core/magnetics/JilesAthertonLeaf.h
        core/include/core/magnetics/CPWLLeaf.h

        # wdf/
        core/include/core/wdf/WDOnePort.h
        core/include/core/wdf/AdaptedElements.h
        core/include/core/wdf/TopologicalJunction.h
        core/include/core/wdf/MEJunction.h
        core/include/core/wdf/ConvergenceGuard.h
        core/include/core/wdf/HSIMDiagnostics.h
        core/include/core/wdf/HSIMSolver.h
        core/include/core/wdf/WDFSeriesAdaptor.h
        core/include/core/wdf/DynamicParallelAdaptor.h
        core/include/core/wdf/WDFResonanceFilter.h
        core/include/core/wdf/TransformerCircuitWDF.h

        # dsp/ (SIMDMath.h and SmoothedValue.h live in util/ only)
        core/include/core/dsp/ADAAEngine.h
        core/include/core/dsp/OversamplingEngine.h

        # model/
        core/include/core/model/CoreGeometry.h
        core/include/core/model/WindingConfig.h
        core/include/core/model/TransformerConfig.h
        core/include/core/model/TransformerModel.h
        core/include/core/model/TransformerGeometry.h
        core/include/core/model/LCResonanceParams.h
        core/include/core/model/ToleranceModel.h
        core/include/core/model/Presets.h
        core/include/core/model/PresetLoader.h
        core/include/core/model/PresetSerializer.h

        # ── Identification headers ──
        identification/include/identification/MeasurementData.h
        identification/include/identification/IOptimizer.h
        identification/include/identification/ObjectiveFunction.h
        identification/include/identification/CMA_ES.h
        identification/include/identification/LevenbergMarquardt.h
        identification/include/identification/ConstraintSet.h
        identification/include/identification/CPWLFitter.h
        identification/include/identification/ActiveLearning.h
        identification/include/identification/IdentificationPipeline.h

        # ── Plugin (JUCE) ──
        plugin/Source/ParameterLayout.h
        plugin/Source/PluginProcessor.h
        plugin/Source/PluginProcessor.cpp
        plugin/Source/PluginEditor.h
        plugin/Source/PluginEditor.cpp
        plugin/Source/BHScopeComponent.h
        plugin/Source/BHScopeComponent.cpp

        # ── Legacy source (Phase 1 — kept for reference) ──
        Source/Core/HysteresisProcessor.h
        Source/Core/HysteresisProcessor.cpp
        Source/Core/HysteresisUtils.h
        Source/Solver/ImplicitSolver.h
        Source/Solver/ImplicitSolver.cpp
        Source/DSP/Oversampling.h
        Source/DSP/Oversampling.cpp
        Source/DSP/DCBlocker.h
        Source/DSP/DCBlocker.cpp
)

target_include_directories(TransformerModel
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/identification/include
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# ─── JUCE Configuration ─────────────────────────────────────────────────────
target_compile_definitions(TransformerModel
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(TransformerModel
    PRIVATE
        TransformerCore
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ─── CLI Tool: simulate ─────────────────────────────────────────────────────
add_executable(simulate tools/simulate.cpp)
target_link_libraries(simulate PRIVATE TransformerCore)
target_include_directories(simulate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
