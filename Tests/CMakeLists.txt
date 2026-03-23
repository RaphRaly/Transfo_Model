cmake_minimum_required(VERSION 3.22)
project(HysteresisTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)

# ── Legacy test (Phase 1 code) ──────────────────────────────────────────────
add_executable(test_hysteresis
    test_hysteresis.cpp
)
target_include_directories(test_hysteresis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source
)

# ── v3 tests — no JUCE dependency ───────────────────────────────────────────
add_executable(test_cpwl_adaa
    test_cpwl_adaa.cpp
)
target_include_directories(test_cpwl_adaa PRIVATE ${CORE_INCLUDE})

add_executable(test_cpwl_passivity
    test_cpwl_passivity.cpp
)
target_include_directories(test_cpwl_passivity PRIVATE ${CORE_INCLUDE})

add_executable(test_hsim_diagnostics
    test_hsim_diagnostics.cpp
)
target_include_directories(test_hsim_diagnostics PRIVATE ${CORE_INCLUDE})

add_executable(test_plugin_integration
    test_plugin_integration.cpp
)
target_include_directories(test_plugin_integration PRIVATE ${CORE_INCLUDE})

add_executable(test_dynamic_losses
    test_dynamic_losses.cpp
)
target_include_directories(test_dynamic_losses PRIVATE ${CORE_INCLUDE})

add_executable(test_dynamic_parallel_adaptor
    test_dynamic_parallel_adaptor.cpp
)
target_include_directories(test_dynamic_parallel_adaptor PRIVATE ${CORE_INCLUDE})

add_executable(test_lc_resonance
    test_lc_resonance.cpp
)
target_include_directories(test_lc_resonance PRIVATE ${CORE_INCLUDE})

add_executable(test_nonlinear_lm
    test_nonlinear_lm.cpp
)
target_include_directories(test_nonlinear_lm PRIVATE ${CORE_INCLUDE})

add_executable(test_circuit_model
    test_circuit_model.cpp
)
target_include_directories(test_circuit_model PRIVATE ${CORE_INCLUDE})

# ── Preamp Sprint 0+1 tests ──────────────────────────────────────────────────
add_executable(test_diode_leaf
    test_diode_leaf.cpp
)
target_include_directories(test_diode_leaf PRIVATE ${CORE_INCLUDE})

add_executable(test_bjt_leaf
    test_bjt_leaf.cpp
)
target_include_directories(test_bjt_leaf PRIVATE ${CORE_INCLUDE})

# ── CTest registration ───────────────────────────────────────────────────────
enable_testing()
add_test(NAME cpwl_adaa              COMMAND test_cpwl_adaa)
add_test(NAME cpwl_passivity         COMMAND test_cpwl_passivity)
add_test(NAME hsim_diagnostics       COMMAND test_hsim_diagnostics)
add_test(NAME plugin_integration     COMMAND test_plugin_integration)
add_test(NAME dynamic_losses         COMMAND test_dynamic_losses)
add_test(NAME dynamic_parallel_adaptor COMMAND test_dynamic_parallel_adaptor)
add_test(NAME lc_resonance            COMMAND test_lc_resonance)
add_test(NAME nonlinear_lm             COMMAND test_nonlinear_lm)
add_test(NAME circuit_model            COMMAND test_circuit_model)
add_test(NAME diode_leaf               COMMAND test_diode_leaf)
add_test(NAME bjt_leaf                 COMMAND test_bjt_leaf)

# ── Preamp Sprint 2 tests ──────────────────────────────────────────────────
add_executable(test_input_stage
    test_input_stage.cpp
)
target_include_directories(test_input_stage PRIVATE ${CORE_INCLUDE})

add_test(NAME input_stage               COMMAND test_input_stage)

# ── Preamp Sprint 3 tests ──────────────────────────────────────────────────
add_executable(test_neve_path
    test_neve_path.cpp
)
target_include_directories(test_neve_path PRIVATE ${CORE_INCLUDE})

add_test(NAME neve_path                  COMMAND test_neve_path)

# ── Preamp Sprint 4 tests ──────────────────────────────────────────────────
add_executable(test_je990_path
    test_je990_path.cpp
)
target_include_directories(test_je990_path PRIVATE ${CORE_INCLUDE})

add_test(NAME je990_path                 COMMAND test_je990_path)

# ── Preamp Sprint 5 tests ──────────────────────────────────────────────────
add_executable(test_output_stage
    test_output_stage.cpp
)
target_include_directories(test_output_stage PRIVATE ${CORE_INCLUDE})

add_test(NAME output_stage                  COMMAND test_output_stage)

# ── Preamp Sprint 6 tests ──────────────────────────────────────────────────
add_executable(test_preamp_full
    test_preamp_full.cpp
)
target_include_directories(test_preamp_full PRIVATE ${CORE_INCLUDE})

add_test(NAME preamp_full                     COMMAND test_preamp_full)

# ── Preamp Sprint 7 tests ──────────────────────────────────────────────────
add_executable(test_preamp_plugin
    test_preamp_plugin.cpp
)
target_include_directories(test_preamp_plugin PRIVATE ${CORE_INCLUDE})

add_test(NAME preamp_plugin                    COMMAND test_preamp_plugin)

# ── Preamp audio sweep (post-fix validation with WAV output) ─────────────────
add_executable(test_preamp_audio_sweep
    test_preamp_audio_sweep.cpp
)
target_include_directories(test_preamp_audio_sweep PRIVATE ${CORE_INCLUDE})

add_test(NAME preamp_audio_sweep              COMMAND test_preamp_audio_sweep)

# ── Preamp diagnostic (detailed per-stage tracing) ────────────────────────────
add_executable(test_preamp_diagnostic
    test_preamp_diagnostic.cpp
)
target_include_directories(test_preamp_diagnostic PRIVATE ${CORE_INCLUDE})

add_test(NAME preamp_diagnostic               COMMAND test_preamp_diagnostic)

# Neve gain diagnostic
add_executable(test_neve_gain_diag test_neve_gain_diag.cpp)
target_include_directories(test_neve_gain_diag PRIVATE ${CORE_INCLUDE})

# Per-stage gain diagnostic
add_executable(test_neve_stage_gains test_neve_stage_gains.cpp)
target_include_directories(test_neve_stage_gains PRIVATE ${CORE_INCLUDE})
