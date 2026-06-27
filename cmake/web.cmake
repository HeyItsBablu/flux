# cmake/web.cmake
# Emscripten toolchain file for FluxUI web builds.


set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_SYSTEM_VERSION 1)

# Locate emsdk — prefer the environment variable set by `source emsdk_env.sh`
if(DEFINED ENV{EMSDK})
    set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
else()
    # Fallback: try to find emcc on PATH
    find_program(EMCC_PROGRAM emcc)
    if(EMCC_PROGRAM)
        get_filename_component(EMSCRIPTEN_ROOT "${EMCC_PROGRAM}" DIRECTORY)
    else()
        message(FATAL_ERROR
            "Emscripten not found.\n"
            "Run: source /path/to/emsdk/emsdk_env.sh\n"
            "Or set EMSDK environment variable to your emsdk root.")
    endif()
endif()

set(CMAKE_C_COMPILER   "${EMSCRIPTEN_ROOT}/emcc")
set(CMAKE_CXX_COMPILER "${EMSCRIPTEN_ROOT}/em++")
set(CMAKE_AR           "${EMSCRIPTEN_ROOT}/emar")
set(CMAKE_RANLIB       "${EMSCRIPTEN_ROOT}/emranlib")

# Tell CMake not to try to run test executables on the host
set(CMAKE_CROSSCOMPILING_EMULATOR "node")

# Emscripten find_package paths
set(CMAKE_FIND_ROOT_PATH "${EMSCRIPTEN_ROOT}/system")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)