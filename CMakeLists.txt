cmake_minimum_required(VERSION 3.20)
project(oscairn_pcm VERSION 0.5.0 LANGUAGES C CXX)

# The binary's name (the release stem is NAME in tools/release/release.conf).
set(PCM_NAME oscairn-pcm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The DSP core: a plain C++ library with no plugin API, OS, threads, files or heap (core/pcore.h).
add_library(pcmcore STATIC core/core.cpp core/laws.cpp core/measured.cpp core/sound.cpp core/tables.cpp)
target_include_directories(pcmcore PUBLIC core)
if(MSVC)
    target_compile_options(pcmcore PRIVATE /W4)
else()
    target_compile_options(pcmcore PRIVATE -Wall -Wextra)
endif()

add_library(clap_headers INTERFACE)
target_include_directories(clap_headers INTERFACE third_party/clap/include third_party/oscairn-clap/include)

# Oscairn Sample Packs (third_party/samplepack, a Rust library with a C interface, a submodule at a pinned
# commit; its CMake module runs cargo with the toolchain its rust-toolchain.toml pins): the pack reader and
# decoder for the shell's loader. The DSP core never sees it.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/samplepack/cmake")
include(SamplePack)

add_library(oscairn_pcm MODULE src/plugin.cpp src/core_engine.cpp src/samples.cpp)
target_link_libraries(oscairn_pcm PRIVATE pcmcore samplepack::samplepack)
target_link_libraries(oscairn_pcm PRIVATE clap_headers)
target_include_directories(oscairn_pcm PRIVATE third_party/dr_libs)
if(NOT MSVC)
    target_compile_options(oscairn_pcm PRIVATE -Wall -Wextra)
endif()
set_target_properties(oscairn_pcm PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME ${PCM_NAME}
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT APPLE AND NOT MSVC)
    target_compile_options(oscairn_pcm PRIVATE -march=x86-64-v2)  # the baseline of the device images, at most
endif()
install(TARGETS oscairn_pcm LIBRARY DESTINATION lib/clap)

# The core's own tests link the core (no plugin): laws, the sound format, voices, cross-build statistics.
enable_testing()
add_executable(pcmcore_tests tests/core/test_core.cpp)
target_link_libraries(pcmcore_tests PRIVATE pcmcore)
target_compile_definitions(pcmcore_tests PRIVATE "PCM_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")  # reads docs/coverage.md
add_test(NAME core COMMAND pcmcore_tests)
if(WIN32)
    return()  # shortcut: the ABI tests (dlopen) and the bench (POSIX clocks) are not ported; on Windows only the core's tests build
endif()
# The bench, cost and render driver (tools/pcbench.cpp; tools/cost/cg.sh runs it under callgrind).
add_executable(pcbench tools/pcbench.cpp src/samples.cpp)
target_link_libraries(pcbench PRIVATE pcmcore)
target_include_directories(pcbench PRIVATE third_party/dr_libs)
target_compile_definitions(pcbench PRIVATE PCM_DECODE)

# The tests see only the built .clap, through the CLAP ABI (dlopen); they link no plugin code.
add_executable(pcm_tests tests/test_main.cpp tests/alloc_guard.cpp tests/test_notes.cpp tests/test_budget.cpp
    tests/test_state.cpp tests/test_contexts.cpp tests/test_alloc.cpp tests/test_threads.cpp tests/test_fuzz.cpp tests/test_controls.cpp
    tests/test_packs.cpp)
target_link_libraries(pcm_tests PRIVATE clap_headers samplepack::samplepack ${CMAKE_DL_LIBS})
if(APPLE)
    # Counts allocations in every image of the process, the plugin's included (alloc_guard.cpp).
    add_library(pcm_alloc_interpose SHARED tests/alloc_interpose.c)
    target_link_libraries(pcm_tests PRIVATE pcm_alloc_interpose)
endif()
add_dependencies(pcm_tests oscairn_pcm)
add_test(NAME abi COMMAND pcm_tests $<TARGET_FILE:oscairn_pcm>)
