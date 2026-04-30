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

add_executable(test_ja_derivation_canonical
    test_ja_derivation_canonical.cpp
)
target_include_directories(test_ja_derivation_canonical PRIVATE ${CORE_INCLUDE})

add_executable(test_ja_loop_sanity
    test_ja_loop_sanity.cpp
)
target_include_directories(test_ja_loop_sanity PRIVATE ${CORE_INCLUDE})

# ── CTest registration ───────────────────────────────────────────────────────
enable_testing()
add_test(NAME cpwl_adaa              COMMAND test_cpwl_adaa)
add_test(NAME cpwl_passivity         COMMAND test_cpwl_passivity)
add_test(NAME plugin_integration     COMMAND test_plugin_integration)
add_test(NAME dynamic_losses         COMMAND test_dynamic_losses)
add_test(NAME lc_resonance            COMMAND test_lc_resonance)
add_test(NAME nonlinear_lm             COMMAND test_nonlinear_lm)
add_test(NAME ja_derivation_canonical  COMMAND test_ja_derivation_canonical)
add_test(NAME ja_loop_sanity            COMMAND test_ja_loop_sanity)

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

# ── A2 Voie C : Bertotti champ dynamique opposé pré-J-A ──────────────────
add_executable(test_bertotti_zero_regression
    test_bertotti_zero_regression.cpp
)
target_include_directories(test_bertotti_zero_regression PRIVATE ${CORE_INCLUDE})

add_executable(test_bertotti_excess_loss_scaling
    test_bertotti_excess_loss_scaling.cpp
)
target_include_directories(test_bertotti_excess_loss_scaling PRIVATE ${CORE_INCLUDE})

add_test(NAME bertotti_field_separation     COMMAND test_bertotti_field_separation)
add_test(NAME bertotti_zero_regression      COMMAND test_bertotti_zero_regression)
add_test(NAME bertotti_excess_loss_scaling  COMMAND test_bertotti_excess_loss_scaling)
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

# ── FluxIntegrator Artistic Mode Regression ───────────────────────────────────
add_executable(test_fluxint_artistic test_fluxint_artistic.cpp)
target_include_directories(test_fluxint_artistic PRIVATE ${CORE_INCLUDE})
add_test(NAME fluxint_artistic                    COMMAND test_fluxint_artistic)

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

# ── Sprint 1 Validation Pipeline ─────────────────────────────────────────────
# Generates baseline THD + FR measurements for Jensen JT-115K-E / JT-11ELCF in
# both Realtime (Artistic) and Artistic calibration modes. Outputs CSVs to
# data/measurements/. The --smoke variant runs a 3-point subset for CTest.
add_executable(validate_jensen
    ../tools/validate_jensen.cpp
)
target_include_directories(validate_jensen PRIVATE ${CORE_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})

# Smoke test: confirms the pipeline runs end-to-end and produces CSV files in
# a temporary subdirectory (does not validate numerical content — Sprint 2's
# job once the calibration is fixed).
add_test(NAME validation_smoke
         COMMAND validate_jensen --smoke
                 --out-dir "${CMAKE_BINARY_DIR}/validation_smoke"
                 --date smoke
         WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# ── Sprint 2 diagnostic ────────────────────────────────────────────────────
# Measures J-A χ_diff at minor-loop M=0 in steady state; compares to the
# three analytical-origin candidates (chiEff_code, chiAnEff, chiMinJiles).
add_executable(diag_chi_diff
    ../tools/diag_chi_diff.cpp
)
target_include_directories(diag_chi_diff PRIVATE ${CORE_INCLUDE})

# Decomposes cascade THD vs Bertotti contribution vs isolated J-A — used to
# locate which DSP stage produces the bulk of the harmonics on each Jensen
# datasheet test point.
add_executable(diag_thd_decompose
    ../tools/diag_thd_decompose.cpp
)
target_include_directories(diag_thd_decompose PRIVATE ${CORE_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
