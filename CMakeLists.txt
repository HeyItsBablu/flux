cmake_minimum_required(VERSION 3.21)
project(MyCppProject VERSION 1.0 LANGUAGES CXX C)   # <-- add C here

if (NOT MSVC)
    message(FATAL_ERROR "MyCppProject must be built with MSVC (Visual C++).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    /W4
    /permissive-
    /Zc:__cplusplus
    /EHsc
)

# ── GLAD ─────────────────────────────────────────────────────────────────────
add_library(glad STATIC
    external/glad/src/glad.c
)
set_source_files_properties(external/glad/src/glad.c PROPERTIES LANGUAGE C)
target_include_directories(glad PUBLIC
    ${PROJECT_SOURCE_DIR}/external/glad/include
)

# ── Clipper2 ──────────────────────────────────────────────────────────────────
add_library(clipper2
    external/Clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp
    external/Clipper2/CPP/Clipper2Lib/src/clipper.offset.cpp
    external/Clipper2/CPP/Clipper2Lib/src/clipper.rectclip.cpp
)
target_include_directories(clipper2 PUBLIC
    ${PROJECT_SOURCE_DIR}/external/Clipper2/CPP/Clipper2Lib/include
)

# ── App ───────────────────────────────────────────────────────────────────────
add_executable(my_app WIN32
    src/main.cpp
)

target_include_directories(my_app PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/glad/include   # explicit for IntelliSense
)

target_link_libraries(my_app PRIVATE
    glad
    clipper2
    Msimg32
    Gdiplus
    opengl32
    glu32
)

target_link_options(my_app PRIVATE /SUBSYSTEM:WINDOWS)

target_compile_definitions(my_app PRIVATE
    $<$<CONFIG:Debug>:FLUX_DEBUG>
)