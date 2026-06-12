cmake_minimum_required(VERSION 3.21)
project(flux VERSION 0.1.0 LANGUAGES CXX C)

# ============================================================================
# ccache
# ============================================================================

if(NOT ANDROID)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_C_COMPILER_LAUNCHER   ${CCACHE_PROGRAM})
    else()
        message(STATUS "ccache not found — builds will not be cached")
    endif()
endif()

# ============================================================================
# Options
# ============================================================================

option(FLUX_BUILD_EXAMPLES "Build example applications" OFF)
option(FLUX_BUILD_TESTS    "Build tests"                OFF)

# ============================================================================
# Compiler guard  (skip on Android — NDK uses Clang but reports differently)
# ============================================================================

if(NOT ANDROID)
    if(NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "FluxUI requires MSVC (Windows), GCC/Clang (Linux), or AppleClang (macOS).")
    endif()
endif()

# ============================================================================
# C++ standard
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# mbedtls  (Android only — git submodule at external/mbedtls)
# ============================================================================

if(ANDROID)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/mbedtls)

    set(MBEDTLS_INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/external/mbedtls/include"
            CACHE PATH "" FORCE)
    set(MBEDTLS_LIBRARY     mbedtls    CACHE STRING "" FORCE)
    set(MBEDX509_LIBRARY    mbedx509   CACHE STRING "" FORCE)
    set(MBEDCRYPTO_LIBRARY  mbedcrypto CACHE STRING "" FORCE)
    set(MBEDTLS_ROOT_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/external/mbedtls"
            CACHE PATH "" FORCE)
endif()

# ============================================================================
# libcurl  (git submodule at external/curl)
# ============================================================================

set(BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE         OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS     ON  CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2       OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL        OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBRTMP       OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP      ON  CACHE BOOL "" FORCE)
set(CURL_ENABLE_WEBSOCKETS ON  CACHE BOOL "" FORCE)

if(NOT ANDROID)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(ANDROID)
    set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS  ON  CACHE BOOL "" FORCE)
    set(CURL_ENABLE_SSL   ON  CACHE BOOL "" FORCE)
elseif(WIN32)
    set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
elseif(APPLE)
    set(CURL_USE_SECTRANSP ON  CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL   OFF CACHE BOOL "" FORCE)
elseif(UNIX AND NOT APPLE)
    set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(external/curl)

# ============================================================================
# FreeType  (git submodule at external/freetype — all platforms)
# ============================================================================

set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
add_subdirectory(external/freetype)

# ============================================================================
# stb  (local — header-only + single implementation file)
# ============================================================================

add_library(stb STATIC src/stb_impl.cpp)
target_include_directories(stb PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/stb>
        $<INSTALL_INTERFACE:include>
)

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
# NanoVG
#
# Android  → GLES2 backend
# Win/Linux → GL3 backend via glad
# macOS    → not used (Metal + CoreGraphics path)
# ============================================================================

if(NOT TARGET nanovg AND NOT APPLE)
    set(NANOVG_SOURCES
            external/nanovg/src/nanovg.c
            external/nanovg/src/nanovg_gl_impl.c
    )
    add_library(nanovg STATIC ${NANOVG_SOURCES})
    set_source_files_properties(${NANOVG_SOURCES} PROPERTIES LANGUAGE C)

    target_include_directories(nanovg PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src>
            $<INSTALL_INTERFACE:include>
    )

    target_compile_definitions(nanovg PRIVATE STB_IMAGE_STATIC)
    target_link_libraries(nanovg PUBLIC stb)

    if(ANDROID)
        target_compile_definitions(nanovg PRIVATE NANOVG_GLES2_IMPLEMENTATION)
        find_library(gles-lib GLESv3)
        target_link_libraries(nanovg PUBLIC ${gles-lib})
    else()
        target_compile_definitions(nanovg PRIVATE NANOVG_GL3_IMPLEMENTATION)
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


if(APPLE)
    file(GLOB_RECURSE FLUX_MM_SOURCES CONFIGURE_DEPENDS src/*.mm)
    foreach(_mm_src ${FLUX_MM_SOURCES})
        set_source_files_properties(${_mm_src}
            PROPERTIES
                COMPILE_FLAGS "-x objective-c++"
                # ARC is enabled target-wide via -fobjc-arc in compile options;
                # no per-file flag needed here.
        )
    endforeach()
    list(APPEND FLUX_SOURCES ${FLUX_MM_SOURCES})
endif()

# Platform source exclusions — order matters: most-specific first.
if(NOT WIN32)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_win32\\.cpp$")
endif()


if(NOT (UNIX AND NOT APPLE AND NOT ANDROID))
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_linux\\.cpp$")
endif()

if(NOT ANDROID)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_android\\.cpp$")
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_android\\.mm$")
endif()


if(NOT APPLE)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_macos\\.mm$")
endif()

list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*stb_impl\\.cpp$")

add_library(flux STATIC ${FLUX_SOURCES})
add_library(flux::flux ALIAS flux)

# ── Include directories ───────────────────────────────────────────────────────

target_include_directories(flux
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/freetype/include>

        $<$<NOT:$<BOOL:${APPLE}>>:$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src>>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${SDL2_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${CAIRO_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${PANGOCAIRO_INCLUDE_DIRS}>
        PRIVATE
        $<$<OR:$<BOOL:${WIN32}>,$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>,$<NOT:$<BOOL:${ANDROID}>>>>:${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${AVFORMAT_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${AVCODEC_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${AVUTIL_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${SWSCALE_INCLUDE_DIRS}>
        $<$<BOOL:${ANDROID}>:${NATIVE_APP_GLUE_DIR}>
        $<$<BOOL:${ANDROID}>:${MBEDTLS_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${ALSA_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>:${JPEG_INCLUDE_DIRS}>
)

# ── Compile definitions ───────────────────────────────────────────────────────

target_compile_definitions(flux PUBLIC
        $<$<CONFIG:Debug>:FLUX_DEBUG>
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<OR:$<BOOL:${WIN32}>,$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>>>:CURL_STATICLIB>
        FLUX_DEBUG_OVERFLOW=1
        CURL_ENABLE_WEBSOCKETS
)

if(ANDROID)
    target_compile_definitions(flux PUBLIC NANOVG_GLES2)
elseif(NOT APPLE)
    target_compile_definitions(flux PUBLIC NANOVG_GL3)
endif()

# ── Link libraries ────────────────────────────────────────────────────────────

if(WIN32)
    target_link_libraries(flux PUBLIC
            stb
            freetype
            glad
            nanovg
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
    find_library(gles-lib    GLESv3)

    target_link_libraries(flux PUBLIC
            stb
            freetype
            app_glue
            nanovg
            CURL::libcurl
            mbedtls
            mbedcrypto
            ${log-lib}
            ${android-lib}
            ${egl-lib}
            ${gles-lib}
    )

    target_link_options(flux PUBLIC
            "-Wl,-u,ANativeActivity_onCreate"
    )

elseif(APPLE)

    target_link_libraries(flux PUBLIC
            stb
            freetype
            CURL::libcurl
            "-framework Cocoa"
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework CoreGraphics"
            "-framework CoreText"
            "-framework CoreFoundation"
            "-framework AudioToolbox"
            "-framework AVFoundation"
            "-framework CoreMedia"      
            "-framework CoreVideo"      
            "-framework UniformTypeIdentifiers"
    )
    target_compile_options(flux PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -fexceptions
            -fobjc-arc
    )

elseif(UNIX AND NOT APPLE)
    target_link_libraries(flux PUBLIC
            stb
            freetype
            glad
            nanovg
            OpenGL::GL
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
    target_compile_options(flux PUBLIC /utf-8)
    target_compile_options(flux PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
elseif(APPLE)
    # Already handled in the Apple target_link_libraries block above
elseif(UNIX)
    target_compile_options(flux PRIVATE
            -Wall -Wextra -Wpedantic -fexceptions
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

    set(FLUX_INSTALL_TARGETS flux stb freetype)

    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
        list(APPEND FLUX_INSTALL_TARGETS glad nanovg)
    endif()

    install(TARGETS ${FLUX_INSTALL_TARGETS}
            EXPORT  fluxTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY external/nanovg/src/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h"
    )

    install(DIRECTORY external/stb/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h"
    )

    install(DIRECTORY external/freetype/include/
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