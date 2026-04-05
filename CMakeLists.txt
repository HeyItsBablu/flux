cmake_minimum_required(VERSION 3.21)
project(flux VERSION 0.1.0 LANGUAGES CXX C)

# ============================================================================
# Options
# ============================================================================

option(FLUX_BUILD_EXAMPLES "Build example applications" OFF)
option(FLUX_BUILD_TESTS    "Build tests"                OFF)

# ============================================================================
# Compiler guard  (skip on Android — NDK uses Clang but reports differently)
# ============================================================================

if(NOT ANDROID)
    if(NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "FluxUI requires MSVC (Windows) or GCC/Clang (Linux).")
    endif()
endif()

# ============================================================================
# C++ standard
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# FetchContent dependencies  (desktop only — Android uses prebuilt NDK libs)
# ============================================================================

if(NOT ANDROID)
    include(FetchContent)

    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
    )
    FetchContent_MakeAvailable(stb)

    FetchContent_Declare(Clipper2
        GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
        GIT_TAG        Clipper2_1.4.0
        SOURCE_SUBDIR  CPP
    )
    FetchContent_MakeAvailable(Clipper2)

    # ── libcurl ───────────────────────────────────────────────────────────────
    FetchContent_Declare(curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_8_0
    )

    set(BUILD_CURL_EXE     OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TESTS ON  CACHE BOOL "" FORCE)
    set(HTTP_ONLY          ON  CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2   OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL    OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBRTMP   OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP  ON  CACHE BOOL "" FORCE)

    if(WIN32)
        set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
    elseif(UNIX AND NOT APPLE)
        set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
        set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(curl)
endif()

# ============================================================================
# GLAD  (Win32 only)
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
# NanoVG  (Android only — GLES2 2-D painter backend)
# ============================================================================

if(ANDROID)
    if(NOT TARGET nanovg)
        add_library(nanovg STATIC
            external/nanovg/src/nanovg.c
        )
        set_source_files_properties(
            external/nanovg/src/nanovg.c PROPERTIES LANGUAGE C
        )
        target_include_directories(nanovg PUBLIC
            external/nanovg/src
        )
        target_compile_definitions(nanovg PRIVATE NANOVG_GLES2_IMPLEMENTATION)
    endif()
endif()

# ============================================================================
# Linux platform dependencies  (SDL2 + Cairo + Pango)
# ============================================================================

if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(SDL2 REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO      REQUIRED cairo)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
endif()

# ============================================================================
# Android platform dependencies
# ============================================================================

if(ANDROID)
    set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

    add_library(app_glue STATIC
        ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
    )
    target_include_directories(app_glue PUBLIC ${NATIVE_APP_GLUE_DIR})

    find_package(CURL REQUIRED)   # supplied by vcpkg android triplet or prebuilt
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
        $<$<NOT:$<BOOL:${ANDROID}>>:${stb_SOURCE_DIR}>
        $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${SDL2_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${CAIRO_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${PANGOCAIRO_INCLUDE_DIRS}>
        $<$<BOOL:${ANDROID}>:${NATIVE_APP_GLUE_DIR}>
        $<$<BOOL:${ANDROID}>:${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src>
)

# ── Link libraries ────────────────────────────────────────────────────────────

if(WIN32)
    target_link_libraries(flux PUBLIC
        glad
        Clipper2
        CURL::libcurl
        Msimg32
        Gdiplus
        opengl32
        glu32
    )
elseif(ANDROID)
    find_library(log-lib     log)
    find_library(android-lib android)
    find_library(egl-lib     EGL)
    find_library(gles-lib    GLESv2)

    target_link_libraries(flux PRIVATE
        app_glue
        nanovg
        CURL::libcurl
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${gles-lib}
    )

    target_link_options(flux PUBLIC
        "-Wl,-u,ANativeActivity_onCreate"
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(flux PUBLIC
        Clipper2
        CURL::libcurl
        ${SDL2_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PANGOCAIRO_LIBRARIES}
    )
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
    $<$<BOOL:${WIN32}>:NOMINMAX>
)

# ============================================================================
# Optional subdirectories  (desktop only)
# ============================================================================

if(NOT ANDROID)
    if(FLUX_BUILD_EXAMPLES)
        add_subdirectory(examples)
    endif()

    if(FLUX_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

# ============================================================================
# Install rules  (desktop only — Android ships the .so inside the APK)
# ============================================================================

if(NOT ANDROID)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

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
endif()