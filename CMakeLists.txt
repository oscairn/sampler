# The Oscairn Sampler, a CLAP instrument. Builds on its own, or inside Oscairn, which fetches
# this repository at a pinned commit (FetchContent) and provides its own CLAP and SDK headers.
cmake_minimum_required(VERSION 3.21)
project(oscairn_sampler VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# The CLAP headers and the Oscairn plugin SDK (both MIT), vendored with their provenance in
# third_party/; inside Oscairn, Oscairn's own copy.
if(NOT TARGET oscairn_clap_headers)
    add_library(oscairn_clap_headers INTERFACE)
    target_include_directories(oscairn_clap_headers INTERFACE third_party/clap/include third_party/oscairn-clap/include)
endif()

# WAV decoding: libsndfile (LGPL-2.1-or-later).
find_package(PkgConfig REQUIRED)
if(NOT TARGET PkgConfig::SNDFILE)
    pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
endif()

add_library(oscairn_sampler MODULE src/sampler.cpp)
target_link_libraries(oscairn_sampler PRIVATE oscairn_clap_headers PkgConfig::SNDFILE)
target_compile_options(oscairn_sampler PRIVATE -Wall -Wextra)
set_target_properties(oscairn_sampler PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
    OUTPUT_NAME oscairn-sampler
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# lib/clap is where CLAP hosts look on Linux (/usr/lib/clap).
install(TARGETS oscairn_sampler LIBRARY DESTINATION lib/clap)
