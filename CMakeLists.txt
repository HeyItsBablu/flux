cmake_minimum_required(VERSION 3.21)
project(flux VERSION 0.1.0 LANGUAGES CXX C)

# ── Options ──────────────────────────────────────────────────────────────────
option(FLUX_BUILD_EXAMPLES "Build example applications" OFF)
option(FLUX_BUILD_TESTS    "Build tests"                OFF)

if(NOT MSVC)
    message(FATAL_ERROR "FluxUI requires MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── FetchContent ─────────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(Clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG        Clipper2_1.4.0
    SOURCE_SUBDIR  CPP
)
FetchContent_MakeAvailable(Clipper2)

# ── GLAD ─────────────────────────────────────────────────────────────────────
add_library(glad STATIC external/glad/src/glad.c)
set_source_files_properties(external/glad/src/glad.c PROPERTIES LANGUAGE C)
target_include_directories(glad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
    $<INSTALL_INTERFACE:include>
)

# ── FluxUI library ───────────────────────────────────────────────────────────
file(GLOB_RECURSE FLUX_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(flux STATIC ${FLUX_SOURCES})
add_library(flux::flux ALIAS flux)

target_include_directories(flux
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        external/glad/include
        external/stb
)

target_link_libraries(flux PUBLIC
    glad
    Clipper2
    Msimg32
    Gdiplus
    opengl32
    glu32
)

target_compile_options(flux PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
target_compile_definitions(flux PUBLIC $<$<CONFIG:Debug>:FLUX_DEBUG>)

# ── Optional subdirectories ───────────────────────────────────────────────────
if(FLUX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(FLUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS flux glad
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