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
    ${CMAKE_CURRENT_SOURCE_DIR}/../legacy/Source
    ${CORE_INCLUDE}
)

# ── Numerical stability non-regression harness ─────────────────────────────
# Sweeps sample rates x input signals (x dynamic loss mix) and asserts: no
# NaN/Inf, bounded output, NR convergence >= 95%, no discontinuous jumps.
# Must stay green.
add_executable(test_hysteresis_stability
    test_hysteresis_stability.cpp
)
target_include_directories(test_hysteresis_stability PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../legacy/Source
    ${CORE_INCLUDE}
)
add_test(NAME hysteresis_stability COMMAND test_hysteresis_stability)

# ── Bertotti coupling non-regression harness ────────────────────────────────
# Validates the J-A ↔ DynamicLosses coupling (field-separation NR residual).
# Asserts Bertotti signature (log-log slope of dynamic loop area vs frequency).
add_executable(test_bertotti_coupling
    test_bertotti_coupling.cpp
)
target_include_directories(test_bertotti_coupling PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../legacy/Source
    ${CORE_INCLUDE}
)
add_test(NAME bertotti_coupling COMMAND test_bertotti_coupling)

# ── v3 tests — no JUCE dependency ───────────────────────────────────────────
add_executable(test_cpwl_adaa
    test_cpwl_adaa.cpp
)
target_include_directories(test_cpwl_adaa PRIVATE ${CORE_INCLUDE})

add_executable(test_cpwl_passivity
    test_cpwl_passivity.cpp
)
target_include_directories(test_cpwl_passivity PRIVATE ${CORE_INCLUDE})

add_executable(test_plugin_integration
    test_plugin_integration.cpp
)
target_include_directories(test_plugin_integration PRIVATE ${CORE_INCLUDE})

add_executable(test_dynamic_losses
    test_dynamic_losses.cpp
)
target_include_directories(test_dynamic_losses PRIVATE ${CORE_INCLUDE})

add_executable(test_lc_resonance
    test_lc_resonance.cpp
)
target_include_directories(test_lc_resonance PRIVATE ${CORE_INCLUDE})

add_executable(test_nonlinear_lm
    test_nonlinear_lm.cpp
)
target_include_directories(test_nonlinear_lm PRIVATE ${CORE_INCLUDE})

# ── CTest registration ───────────────────────────────────────────────────────
enable_testing()
add_test(NAME cpwl_adaa              COMMAND test_cpwl_adaa)
add_test(NAME cpwl_passivity         COMMAND test_cpwl_passivity)
add_test(NAME plugin_integration     COMMAND test_plugin_integration)
add_test(NAME dynamic_losses         COMMAND test_dynamic_losses)
add_test(NAME lc_resonance            COMMAND test_lc_resonance)
add_test(NAME nonlinear_lm             COMMAND test_nonlinear_lm)

# ── THD Validation Tests (V1.1-V1.3) ──────────────────────────────────────
add_executable(test_thd_validation
    test_thd_validation.cpp
)
target_include_directories(test_thd_validation PRIVATE ${CORE_INCLUDE})

add_test(NAME thd_validation                  COMMAND test_thd_validation)

# ── Frequency Response Validation (V1.4) ──────────────────────────────────
add_executable(test_freq_response_validation
    test_freq_response_validation.cpp
)
target_include_directories(test_freq_response_validation PRIVATE ${CORE_INCLUDE})

add_test(NAME freq_response_validation        COMMAND test_freq_response_validation)

# ── Plugin State Tests (A2.4) ──────────────────────────────────────────────
add_executable(test_plugin_state
    test_plugin_state.cpp
)
target_include_directories(test_plugin_state PRIVATE ${CORE_INCLUDE})

add_test(NAME plugin_state                    COMMAND test_plugin_state)

# ── Jensen Transformer Coverage Tests ─────────────────────────────────────────
add_executable(test_jensen_passivity
    test_jensen_passivity.cpp
)
target_include_directories(test_jensen_passivity PRIVATE ${CORE_INCLUDE})

add_executable(test_jensen_convergence
    test_jensen_convergence.cpp
)
target_include_directories(test_jensen_convergence PRIVATE ${CORE_INCLUDE})

add_executable(test_jensen_harmonics
    test_jensen_harmonics.cpp
)
target_include_directories(test_jensen_harmonics PRIVATE ${CORE_INCLUDE})

add_executable(test_jensen_bertotti_freq
    test_jensen_bertotti_freq.cpp
)
target_include_directories(test_jensen_bertotti_freq PRIVATE ${CORE_INCLUDE})

add_executable(test_jensen_cpwl_parity
    test_jensen_cpwl_parity.cpp
)
target_include_directories(test_jensen_cpwl_parity PRIVATE ${CORE_INCLUDE})

add_executable(test_nr_low_h_stress
    test_nr_low_h_stress.cpp
)
target_include_directories(test_nr_low_h_stress PRIVATE ${CORE_INCLUDE})

add_test(NAME nr_low_h_stress               COMMAND test_nr_low_h_stress)

add_executable(test_bertotti_field_separation
    test_bertotti_field_separation.cpp
)
target_include_directories(test_bertotti_field_separation PRIVATE ${CORE_INCLUDE})

add_test(NAME bertotti_field_separation     COMMAND test_bertotti_field_separation)
add_test(NAME jensen_passivity              COMMAND test_jensen_passivity)
add_test(NAME jensen_convergence            COMMAND test_jensen_convergence)
add_test(NAME jensen_harmonics              COMMAND test_jensen_harmonics)
add_test(NAME jensen_bertotti_freq          COMMAND test_jensen_bertotti_freq)
add_test(NAME jensen_cpwl_parity            COMMAND test_jensen_cpwl_parity)

# ── Channel Strip Integration Test ────────────────────────────────────────────
add_executable(test_channel_strip
    test_channel_strip.cpp
)
target_include_directories(test_channel_strip PRIVATE ${CORE_INCLUDE})

add_test(NAME channel_strip                   COMMAND test_channel_strip)

# ── Phase 3 UAD Critique Tests ─────────────────────────────────────────────────

add_executable(test_double_legacy_plugin_path
    test_double_legacy_plugin_path.cpp
)
target_include_directories(test_double_legacy_plugin_path PRIVATE ${CORE_INCLUDE})
add_test(NAME double_legacy_plugin_path       COMMAND test_double_legacy_plugin_path)

# ── Flux Integrator Tests (Problem #4: freq-dependent saturation) ────────────
add_executable(test_flux_integrator test_flux_integrator.cpp)
target_include_directories(test_flux_integrator PRIVATE ${CORE_INCLUDE})
add_test(NAME flux_integrator                    COMMAND test_flux_integrator)

# ── FluxIntegrator Physical Mode Regression ───────────────────────────────────
add_executable(test_fluxint_physical test_fluxint_physical.cpp)
target_include_directories(test_fluxint_physical PRIVATE ${CORE_INCLUDE})
add_test(NAME fluxint_physical                    COMMAND test_fluxint_physical)

# ── Harrison Console Mic Pre Tests ──────────────────────────────────────────
add_executable(test_harrison_micpre test_harrison_micpre.cpp)
target_include_directories(test_harrison_micpre PRIVATE ${CORE_INCLUDE})

add_test(NAME harrison_micpre                     COMMAND test_harrison_micpre)

add_executable(test_harrison_integration test_harrison_integration.cpp)
target_include_directories(test_harrison_integration PRIVATE ${CORE_INCLUDE})

add_test(NAME harrison_integration                COMMAND test_harrison_integration)

# ── FluxIntegrator Exploration Diagnostic (not a CTest — manual only) ─────────
add_executable(test_fluxint_diag test_fluxint_diag.cpp)
target_include_directories(test_fluxint_diag PRIVATE ${CORE_INCLUDE})

# ── Demo Audio Generator (P2.3) ──────────────────────────────────────────────
add_executable(generate_demo_audio
    ../tools/generate_demo_audio.cpp
)
target_include_directories(generate_demo_audio PRIVATE ${CORE_INCLUDE})
