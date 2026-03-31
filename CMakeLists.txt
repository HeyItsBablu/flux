cmake_minimum_required(VERSION 3.21)
project(flux VERSION 0.1.0 LANGUAGES CXX C)

# ============================================================================
# Options
# ============================================================================

option(FLUX_BUILD_EXAMPLES "Build example applications" OFF)
option(FLUX_BUILD_TESTS    "Build tests"                OFF)

# ============================================================================
# Compiler guard
# ============================================================================

if(NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "FluxUI requires MSVC (Windows) or GCC/Clang (Linux).")
endif()

# ============================================================================
# C++ standard
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# FetchContent dependencies
# ============================================================================

include(FetchContent)

FetchContent_Declare(Clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG        Clipper2_1.4.0
    SOURCE_SUBDIR  CPP
)
FetchContent_MakeAvailable(Clipper2)

# ============================================================================
# GLAD  (Win32 / OpenGL path only)
# On Linux we use Cairo/SDL2 — no OpenGL loader needed.
# ============================================================================

if(WIN32)
    add_library(glad STATIC external/glad/src/glad.c)
    set_source_files_properties(external/glad/src/glad.c PROPERTIES LANGUAGE C)
    target_include_directories(glad PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
        $<INSTALL_INTERFACE:include>
    )
endif()

# ============================================================================
# Linux platform dependencies  (SDL2 + Cairo + Pango)
# ============================================================================

if(UNIX AND NOT APPLE)
    find_package(SDL2 REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO      REQUIRED cairo)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
endif()

# ============================================================================
# FluxUI library sources
# ============================================================================

file(GLOB_RECURSE FLUX_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(flux STATIC ${FLUX_SOURCES})
add_library(flux::flux ALIAS flux)

# ── Include directories ───────────────────────────────────────────────────────

target_include_directories(flux
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
        $<$<BOOL:${UNIX}>:${SDL2_INCLUDE_DIRS}>
        $<$<BOOL:${UNIX}>:${CAIRO_INCLUDE_DIRS}>
        $<$<BOOL:${UNIX}>:${PANGOCAIRO_INCLUDE_DIRS}>
)

# ── Link libraries ────────────────────────────────────────────────────────────

if(WIN32)
    target_link_libraries(flux PUBLIC
        glad
        Clipper2
        Msimg32
        Gdiplus
        opengl32
        glu32
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(flux PUBLIC
        Clipper2
        ${SDL2_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PANGOCAIRO_LIBRARIES}
    )
    # Extra compile/link flags from pkg-config (e.g. -pthread)
    target_compile_options(flux PRIVATE
        ${CAIRO_CFLAGS_OTHER}
        ${PANGOCAIRO_CFLAGS_OTHER}
    )
endif()

# ── Compiler options ──────────────────────────────────────────────────────────

if(MSVC)
    target_compile_options(flux PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )
else()
    target_compile_options(flux PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fexceptions
    )
endif()

# ── Compile definitions ───────────────────────────────────────────────────────

target_compile_definitions(flux PUBLIC
    $<$<CONFIG:Debug>:FLUX_DEBUG>
    $<$<BOOL:${WIN32}>:NOMINMAX>   # guard: only relevant on Win32
)

# ============================================================================
# Optional subdirectories
# ============================================================================

if(FLUX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(FLUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Install rules
# ============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Collect installable targets — glad only exists on Win32
set(FLUX_INSTALL_TARGETS flux)
if(WIN32)
    list(APPEND FLUX_INSTALL_TARGETS glad)
endif()

install(TARGETS ${FLUX_INSTALL_TARGETS}
    EXPORT  fluxTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT fluxTargets
    FILE      fluxTargets.cmake
    NAMESPACE flux::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flux
)

configure_package_config_file(
    cmake/fluxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/fluxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flux
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/fluxConfigVersion.cmake
    VERSION      ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/fluxConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/fluxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flux
)