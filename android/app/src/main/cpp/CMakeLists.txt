cmake_minimum_required(VERSION 3.22.1)
project(flux_android LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FLUX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
add_subdirectory(${FLUX_ROOT} flux_build)

add_library(native-lib SHARED
        flux_android_main.cpp
        ${FLUX_ROOT}/lib/main.cpp
)

target_link_libraries(native-lib PRIVATE
        flux::flux
        aaudio
        mediandk
        camera2ndk 
)

target_link_options(native-lib PRIVATE
        "-Wl,-u,ANativeActivity_onCreate"
)