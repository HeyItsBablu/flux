cmake_minimum_required(VERSION 3.21)

project(MyCppProject
    VERSION 1.0
    LANGUAGES CXX
)

# 🔒 Enforce MSVC only
if (NOT MSVC)
    message(FATAL_ERROR "MyCppProject must be built with MSVC (Visual C++).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific compile options
add_compile_options(
    /W4                 # High warning level
    /permissive-        # Strict standard conformance
    /Zc:__cplusplus     # Correct __cplusplus value
    /EHsc               # Standard C++ exceptions
)

add_executable(my_app WIN32
    src/main.cpp
)

target_include_directories(my_app
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# MSVC-specific link options (optional but common for GUI apps)
target_link_options(my_app
    PRIVATE
        /SUBSYSTEM:WINDOWS
)
