cmake_minimum_required(VERSION 3.20)
project(oscairn_sampler VERSION 0.9.3 LANGUAGES C CXX)

# The binary's name (the release stem is NAME in tools/release/release.conf).
set(PCM_NAME oscairn-sampler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The DSP core: a plain C++ library with no plugin API, OS, threads, files or heap (core/pcore.h).
add_library(pcmcore STATIC core/core.cpp core/kernels.cpp core/laws.cpp core/measured.cpp core/sound.cpp core/tables.cpp)
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

add_library(oscairn_sampler MODULE src/plugin.cpp src/core_engine.cpp src/loader.cpp src/packs.cpp src/streamer.cpp)
find_package(Threads REQUIRED)  # the I/O thread
target_link_libraries(oscairn_sampler PRIVATE pcmcore samplepack::samplepack Threads::Threads)
target_link_libraries(oscairn_sampler PRIVATE clap_headers)
if(NOT MSVC)
    target_compile_options(oscairn_sampler PRIVATE -Wall -Wextra)
endif()
set_target_properties(oscairn_sampler PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME ${PCM_NAME}
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT APPLE AND NOT MSVC)
    target_compile_options(oscairn_sampler PRIVATE -march=x86-64-v2)  # the baseline of the device images, at most
endif()
install(TARGETS oscairn_sampler LIBRARY DESTINATION lib/clap)

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
add_executable(pcbench tools/pcbench.cpp src/loader.cpp src/packs.cpp src/streamer.cpp)
target_link_libraries(pcbench PRIVATE pcmcore samplepack::samplepack Threads::Threads)
target_compile_definitions(pcbench PRIVATE PCM_PACKS)

# The tests see only the built .clap, through the CLAP ABI (dlopen); they link no plugin code.
add_executable(pcm_tests tests/test_main.cpp tests/alloc_guard.cpp tests/test_notes.cpp tests/test_budget.cpp
    tests/test_state.cpp tests/test_contexts.cpp tests/test_alloc.cpp tests/test_threads.cpp tests/test_fuzz.cpp tests/test_controls.cpp
    tests/test_packs.cpp tests/test_streaming.cpp tests/test_waveforms.cpp tests/test_sharing.cpp tests/test_shared_samples.cpp tests/test_sleep.cpp)
target_link_libraries(pcm_tests PRIVATE clap_headers samplepack::samplepack ${CMAKE_DL_LIBS})
if(APPLE)
    # Counts allocations in every image of the process, the plugin's included (alloc_guard.cpp).
    add_library(pcm_alloc_interpose SHARED tests/alloc_interpose.c)
    target_link_libraries(pcm_tests PRIVATE pcm_alloc_interpose)
endif()
add_dependencies(pcm_tests oscairn_sampler)
# The samplepack tool makes the tests' packs from generated tones (the host's toolchain, not the plugin's target).
set(SAMPLEPACK_CLI "${CMAKE_BINARY_DIR}/samplepack-cli/release/samplepack")
add_custom_target(samplepack_cli ${SAMPLEPACK_CARGO} build --release --locked --offline -p samplepack-cli --target-dir "${CMAKE_BINARY_DIR}/samplepack-cli"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/samplepack" BYPRODUCTS "${SAMPLEPACK_CLI}" COMMENT "cargo build samplepack (the tool)" VERBATIM)
add_dependencies(samplepack_cli samplepack_build)  # one cargo at a time: a first run installs the pinned toolchain
add_dependencies(pcm_tests samplepack_cli)
target_compile_definitions(pcm_tests PRIVATE "SAMPLEPACK_CLI=\"${SAMPLEPACK_CLI}\"")
add_dependencies(pcm_tests pcbench)
target_compile_definitions(pcm_tests PRIVATE "PCBENCH=\"$<TARGET_FILE:pcbench>\"")  # the tests run its render and coverage
add_test(NAME abi COMMAND pcm_tests $<TARGET_FILE:oscairn_sampler>)
