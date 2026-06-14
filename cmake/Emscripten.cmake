# Emscripten toolchain wrapper for lagoon-randomizer.
#
# This is a thin shim around the toolchain file that ships with emsdk. It lets
# you configure a WASM build with a plain `cmake -DCMAKE_TOOLCHAIN_FILE=...`
# instead of having to go through `emcmake`.
#
# It uses the EMSDK environment variable when emsdk_env.sh has been sourced,
# and otherwise falls back to the known local emsdk checkout.
#
# Usage:
#   cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
#   cmake --build build-wasm

if(DEFINED ENV{EMSDK})
    set(_emsdk_root "$ENV{EMSDK}")
else()
    set(_emsdk_root "/home/oipo/Programming/emsdk")
endif()

set(_emscripten_toolchain
    "${_emsdk_root}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

if(NOT EXISTS "${_emscripten_toolchain}")
    message(FATAL_ERROR
        "Could not find the Emscripten toolchain at:\n  ${_emscripten_toolchain}\n"
        "Source emsdk_env.sh first "
        "(e.g. `source /home/oipo/Programming/emsdk/emsdk_env.sh`) "
        "or correct the emsdk path in cmake/Emscripten.cmake.")
endif()

include("${_emscripten_toolchain}")
