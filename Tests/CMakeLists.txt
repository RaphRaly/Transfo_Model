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

# ── CTest registration ───────────────────────────────────────────────────────
enable_testing()
add_test(NAME cpwl_adaa              COMMAND test_cpwl_adaa)
add_test(NAME cpwl_passivity         COMMAND test_cpwl_passivity)
add_test(NAME hsim_diagnostics       COMMAND test_hsim_diagnostics)
add_test(NAME plugin_integration     COMMAND test_plugin_integration)
