cmake_minimum_required (VERSION 3.13)

project(lagoon-randomizer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#add_compile_options(-Wall -Werror -g3)
include_directories(extern/Lyra/include)
include_directories(extern/spdlog/include)



#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")


if(EMSCRIPTEN)
    # Browser build: a WASM module exposing randomizeRom() to JavaScript via
    # embind. No CLI / file I/O here -- the page hands us the uploaded ROM bytes
    # and takes back the patched bytes. Shared logic lives in randomizer.hpp.
    add_executable(lagoon-randomizer web.cpp)

    target_link_options(lagoon-randomizer PRIVATE
        "-lembind"                          # embind bindings
        "--no-entry"                        # no main(); this is a library module
        "-sMODULARIZE=1"                    # export a factory instead of polluting globals
        "-sEXPORT_NAME=createLagoonRandomizer" # window.createLagoonRandomizer()
        "-sALLOW_MEMORY_GROWTH=1"           # ROMs are ~1MB, grow as needed
        "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
    )
    set_target_properties(lagoon-randomizer PROPERTIES SUFFIX ".js")
else()
    # Native Linux CLI.
    add_executable(lagoon-randomizer main.cpp)
endif()
