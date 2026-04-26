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

# ── Numerical stability non-regression harness ─────────────────────────────
# Sweeps sample rates x input signals (x dynamic loss mix) and asserts: no
# NaN/Inf, bounded output, NR convergence >= 95%, no discontinuous jumps.
# Must stay green.
add_executable(test_hysteresis_stability
    test_hysteresis_stability.cpp
)
target_include_directories(test_hysteresis_stability PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source
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

# Neve gain diagnostic
add_executable(test_neve_gain_diag test_neve_gain_diag.cpp)
target_include_directories(test_neve_gain_diag PRIVATE ${CORE_INCLUDE})

# Per-stage gain diagnostic
add_executable(test_neve_stage_gains test_neve_stage_gains.cpp)
target_include_directories(test_neve_stage_gains PRIVATE ${CORE_INCLUDE})

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

# ── ODT Differentiation Tests ──────────────────────────────────────────────
add_executable(test_odt_differentiation
    test_odt_differentiation.cpp
)
target_include_directories(test_odt_differentiation PRIVATE ${CORE_INCLUDE})

add_test(NAME odt_differentiation              COMMAND test_odt_differentiation)

# ── Sample Rate Invariance Tests (Sprint 3 polish) ───────────────────────────
add_executable(test_sample_rate_invariance
    test_sample_rate_invariance.cpp
)
target_include_directories(test_sample_rate_invariance PRIVATE ${CORE_INCLUDE})

add_test(NAME sample_rate_invariance           COMMAND test_sample_rate_invariance)

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

# Sprint A.1: VAS diagnostic (double-degen check)
add_executable(test_vas_diagnostic test_vas_diagnostic.cpp)
target_include_directories(test_vas_diagnostic PRIVATE ${CORE_INCLUDE})
add_test(NAME vas_diagnostic                    COMMAND test_vas_diagnostic)

# Sprint B.5: ClassAB isolated crossover validation
add_executable(test_classab_isolated test_classab_isolated.cpp)
target_include_directories(test_classab_isolated PRIVATE ${CORE_INCLUDE})
add_test(NAME classab_isolated                  COMMAND test_classab_isolated)

# Sprint C.1: Heritage THD at low level
add_executable(test_heritage_thd_lowlevel test_heritage_thd_lowlevel.cpp)
target_include_directories(test_heritage_thd_lowlevel PRIVATE ${CORE_INCLUDE})
add_test(NAME heritage_thd_lowlevel             COMMAND test_heritage_thd_lowlevel)

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

# ── Preamp FR Full-Chain Sweep (Chantier A2) ──────────────────────────────────
add_executable(test_preamp_fr_sweep test_preamp_fr_sweep.cpp)
target_include_directories(test_preamp_fr_sweep PRIVATE ${CORE_INCLUDE})
add_test(NAME preamp_fr_sweep                    COMMAND test_preamp_fr_sweep)

# ── FluxIntegrator Exploration Diagnostic (not a CTest — manual only) ─────────
add_executable(test_fluxint_diag test_fluxint_diag.cpp)
target_include_directories(test_fluxint_diag PRIVATE ${CORE_INCLUDE})

# TR3 trace diagnostic (manual only)
add_executable(test_neve_tr3_trace test_neve_tr3_trace.cpp)
target_include_directories(test_neve_tr3_trace PRIVATE ${CORE_INCLUDE})

# Newton trace diagnostic (manual only)
add_executable(test_neve_newton_trace test_neve_newton_trace.cpp)
target_include_directories(test_neve_newton_trace PRIVATE ${CORE_INCLUDE})

# ── Demo Audio Generator (P2.3) ──────────────────────────────────────────────
add_executable(generate_demo_audio
    ../tools/generate_demo_audio.cpp
)
target_include_directories(generate_demo_audio PRIVATE ${CORE_INCLUDE})
