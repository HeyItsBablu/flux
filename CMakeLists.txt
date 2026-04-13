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
# FetchContent dependencies
# ============================================================================

include(FetchContent)

if(NOT ANDROID)
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
endif()

if(ANDROID)
    FetchContent_Declare(mbedtls
            GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
            GIT_TAG        v3.6.2
    )
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mbedtls)

    set(MBEDTLS_INCLUDE_DIRS
            "${mbedtls_SOURCE_DIR}/include"
            CACHE PATH "" FORCE)
    set(MBEDTLS_LIBRARY     mbedtls    CACHE STRING "" FORCE)
    set(MBEDX509_LIBRARY    mbedx509   CACHE STRING "" FORCE)
    set(MBEDCRYPTO_LIBRARY  mbedcrypto CACHE STRING "" FORCE)
    set(MBEDTLS_ROOT_DIR "${mbedtls_SOURCE_DIR}" CACHE PATH "" FORCE)
endif()

# ── libcurl ───────────────────────────────────────────────────────────────────

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

if(ANDROID)
    set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS  ON  CACHE BOOL "" FORCE)
    set(CURL_ENABLE_SSL   ON  CACHE BOOL "" FORCE)
elseif(WIN32)
    set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
elseif(UNIX AND NOT APPLE)
    set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(curl)

# ============================================================================
# GLAD  (Windows + Linux desktop only — Android uses EGL/GLES directly)
# ============================================================================

if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
    add_library(glad STATIC external/glad/src/glad.c)
    set_source_files_properties(external/glad/src/glad.c PROPERTIES LANGUAGE C)
    target_include_directories(glad PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
            $<INSTALL_INTERFACE:include>
    )
endif()

# ============================================================================
# NanoVG  (all platforms)
#
# Android  → GLES2 backend, no glad, nanovg_gl_impl.c is Android-specific
# Win/Linux → GL3 backend, glad provides the loader, nanovg_gl_impl.c shared
# ============================================================================

if(NOT TARGET nanovg)
    # Core source is always compiled
    set(NANOVG_SOURCES external/nanovg/src/nanovg.c)

    if(ANDROID)
        # Android ships its own GL implementation file (GLES2 variant)
        list(APPEND NANOVG_SOURCES external/nanovg/src/nanovg_gl_impl.c)
    else()
        # Desktop shares the same nanovg_gl_impl.c compiled for GL3
        list(APPEND NANOVG_SOURCES external/nanovg/src/nanovg_gl_impl.c)
    endif()

    add_library(nanovg STATIC ${NANOVG_SOURCES})
    set_source_files_properties(${NANOVG_SOURCES} PROPERTIES LANGUAGE C)

    target_include_directories(nanovg PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(nanovg PRIVATE STB_IMAGE_STATIC)

    if(ANDROID)
        # Compile the GLES2 implementation into this translation unit
        target_compile_definitions(nanovg PRIVATE NANOVG_GLES2_IMPLEMENTATION)
    else()
        # Compile the GL3 implementation into this translation unit
        target_compile_definitions(nanovg PRIVATE NANOVG_GL3_IMPLEMENTATION)
        # glad must be loaded before any GL call — link it so consumers get it too
        target_include_directories(nanovg PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
        )
        target_link_libraries(nanovg PUBLIC glad)
    endif()
endif()

# ============================================================================
# Linux platform dependencies
# ============================================================================

if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(SDL2   REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(JPEG   REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO      REQUIRED cairo)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
    pkg_check_modules(ALSA       REQUIRED alsa)

    pkg_check_modules(AVFORMAT  REQUIRED libavformat)
    pkg_check_modules(AVCODEC   REQUIRED libavcodec)
    pkg_check_modules(AVUTIL    REQUIRED libavutil)
    pkg_check_modules(SWSCALE   REQUIRED libswscale)

    message(STATUS "FFmpeg avformat : ${AVFORMAT_VERSION}")
    message(STATUS "FFmpeg avcodec  : ${AVCODEC_VERSION}")
    message(STATUS "FFmpeg swscale  : ${SWSCALE_VERSION}")
endif()

# ============================================================================
# Android platform dependencies
# ============================================================================

if(ANDROID)
    set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

    if(NOT TARGET app_glue)
        add_library(app_glue STATIC
                ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
        )
        target_include_directories(app_glue PUBLIC ${NATIVE_APP_GLUE_DIR})
    endif()
endif()

# ============================================================================
# FluxUI library sources
# ============================================================================

file(GLOB_RECURSE FLUX_SOURCES CONFIGURE_DEPENDS src/*.cpp)

# flux_canvas2d_win32.cpp is the legacy GDI+ backend.
# On Windows we now use the NanoVG/GL3 backend, so exclude it.
if(WIN32)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_canvas2d_win32\\.cpp$")
endif()

add_library(flux STATIC ${FLUX_SOURCES})
add_library(flux::flux ALIAS flux)

# ── Include directories ───────────────────────────────────────────────────────

target_include_directories(flux
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # nanovg headers are already exposed via the nanovg target but we also
        # expose them directly so platform headers that #include <nanovg.h>
        # without going through the target still resolve correctly.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${SDL2_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${CAIRO_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${PANGOCAIRO_INCLUDE_DIRS}>
        $<$<NOT:$<BOOL:${ANDROID}>>:${stb_SOURCE_DIR}>
        PRIVATE
        $<$<NOT:$<BOOL:${ANDROID}>>:${stb_SOURCE_DIR}>
        $<$<OR:$<BOOL:${WIN32}>,$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>>:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${AVFORMAT_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${AVCODEC_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${AVUTIL_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${SWSCALE_INCLUDE_DIRS}>
        $<$<BOOL:${ANDROID}>:${NATIVE_APP_GLUE_DIR}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${ALSA_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${JPEG_INCLUDE_DIRS}>
)

# ── Compile definitions ───────────────────────────────────────────────────────

target_compile_definitions(flux PUBLIC
        $<$<CONFIG:Debug>:FLUX_DEBUG>
        $<$<BOOL:${WIN32}>:NOMINMAX>
)

# Expose the correct NanoVG backend declaration guard to all consumers.
# The *_IMPLEMENTATION variant is only defined in the nanovg target itself
# (the translation unit that actually compiles the GL code).
if(ANDROID)
    target_compile_definitions(flux PUBLIC NANOVG_GLES2)
else()
    target_compile_definitions(flux PUBLIC NANOVG_GL3)
endif()

# ── Link libraries ────────────────────────────────────────────────────────────

if(WIN32)
    target_link_libraries(flux PUBLIC
            glad
            nanovg
            Clipper2
            CURL::libcurl
            Msimg32
            Gdiplus
            opengl32
            glu32
            xaudio2
            ole32
            mf
            mfplat
            mfreadwrite
            mfuuid
            propsys
            windowscodecs
    )

elseif(ANDROID)
    find_library(log-lib     log)
    find_library(android-lib android)
    find_library(egl-lib     EGL)
    find_library(gles-lib    GLESv2)

    target_link_libraries(flux PUBLIC
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
            glad
            nanovg
            OpenGL::GL
            Clipper2
            CURL::libcurl
            ${SDL2_LIBRARIES}
            ${CAIRO_LIBRARIES}
            ${PANGOCAIRO_LIBRARIES}
            ${ALSA_LIBRARIES}
            ${AVFORMAT_LIBRARIES}
            ${AVCODEC_LIBRARIES}
            ${AVUTIL_LIBRARIES}
            ${SWSCALE_LIBRARIES}
            ${JPEG_LIBRARIES}
    )
    target_compile_options(flux PRIVATE
            ${CAIRO_CFLAGS_OTHER}
            ${PANGOCAIRO_CFLAGS_OTHER}
            ${ALSA_CFLAGS_OTHER}
            ${AVFORMAT_CFLAGS_OTHER}
            ${AVCODEC_CFLAGS_OTHER}
            ${AVUTIL_CFLAGS_OTHER}
            ${SWSCALE_CFLAGS_OTHER}
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

    set(FLUX_INSTALL_TARGETS flux nanovg)

    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
        list(APPEND FLUX_INSTALL_TARGETS glad)
    endif()

    install(TARGETS ${FLUX_INSTALL_TARGETS}
            EXPORT  fluxTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Install nanovg public headers so the INSTALL_INTERFACE path resolves
    install(DIRECTORY external/nanovg/src/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h"
    )

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