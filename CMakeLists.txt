cmake_minimum_required(VERSION 3.21)
project(flux VERSION 0.1.0 LANGUAGES CXX C)

include(config/AppConfig.cmake)

# Override project version from config
cmake_policy(SET CMP0048 NEW)
project(flux VERSION ${FLUX_APP_VERSION} LANGUAGES CXX C)


# ============================================================================
# ccache
# ============================================================================

if(NOT ANDROID AND NOT EMSCRIPTEN)
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
# Desktop library toggle
#
# The `flux` target (and everything downstream of it — linux/windows/macos
# platform apps, cli) pulls in SDL2 + Cairo + Pango + ALSA + FFmpeg on
# native Linux via find_package/pkg_check_modules below. flux_ssr
# (ssr/CMakeLists.txt) does NOT link against `flux` at all — it hand-picks
# its own DOM/stb-based source list specifically so a thin headless SSR
# deployment never needs a desktop's worth of system packages installed.
#
# Turning this OFF only has an effect on plain native Linux — Windows,
# macOS, Android, and Emscripten builds all still need `flux` itself (the
# platform app IS the flux library there), so FLUX_NEED_DESKTOP_LIB below
# forces it back on for them regardless of this setting. This flag exists
# purely so `cmake -DFLUX_BUILD_SSR=ON -DFLUX_BUILD_DESKTOP=OFF` on Linux
# can configure and build flux_ssr without SDL2/Cairo/Pango/ALSA/FFmpeg
# ever being searched for.
# ============================================================================
option(FLUX_BUILD_DESKTOP "Build the native desktop flux library, platform app, and cli (requires SDL2/Cairo/Pango/ALSA/FFmpeg on Linux)" ON)

# True whenever the flux library is actually needed: either the desktop
# build was explicitly requested, or the current platform has no other way
# to produce an app at all (Windows/macOS/Android/Emscripten all build
# their platform app FROM the flux library — there is no SSR-only path for
# them the way there is on native Linux).
if(FLUX_BUILD_DESKTOP OR NOT (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN))
    set(FLUX_NEED_DESKTOP_LIB TRUE)
else()
    set(FLUX_NEED_DESKTOP_LIB FALSE)
    message(STATUS "FLUX_BUILD_DESKTOP=OFF on native Linux — skipping SDL2/Cairo/Pango/ALSA/FFmpeg and the `flux` desktop library entirely. Only flux_ssr (if FLUX_BUILD_SSR=ON) will be built.")
endif()

# ============================================================================
# Web renderer selection
#
#   canvas (default) — existing Canvas2D-based Painter (flux_painter_web.cpp,
#                       flux_font_web.cpp, flux_window_web.cpp).
#   dom               — new DOM-based Painter (flux_painter_dom.cpp,
#                       flux_font_dom.cpp, flux_window_dom.cpp,
#                       flux_dom_adapter_live.cpp). See flux_dom_adapter.hpp
#                       for why these two are mutually exclusive, never both.
# ============================================================================

set(FLUX_WEB_RENDERER "dom" CACHE STRING "Web renderer: canvas or dom")
set_property(CACHE FLUX_WEB_RENDERER PROPERTY STRINGS canvas dom)


# ============================================================================
# SSR host (native, non-Emscripten) — see ssr/CMakeLists.txt.
#
# Deliberately built as its OWN, separate static library + executable, NOT
# folded into the shared `flux` target's FLUX_WEB_RENDERER logic above.
# The SSR host needs flux_painter_dom.cpp (DOM/string-builder painting)
# while a normal native Linux build needs flux_painter_linux.cpp (Cairo) —
# both define the same Painter:: symbols, so they can never coexist in one
# library target. ssr/CMakeLists.txt hand-picks its own source list.
# ============================================================================
option(FLUX_BUILD_SSR "Build the native SSR host (flux_ssr)" OFF)


# ============================================================================
# Compiler guard  (skip on Android — NDK uses Clang but reports differently)
# ============================================================================

if(NOT ANDROID AND NOT EMSCRIPTEN)
    if(NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "FluxUI requires MSVC (Windows), GCC/Clang (Linux), or AppleClang (macOS).")
    endif()
endif()

# ============================================================================
# Emscripten detection
# ============================================================================

if(EMSCRIPTEN)
    message(STATUS "FluxUI: building for Web (Emscripten ${EMSCRIPTEN_VERSION})")
    set(FLUX_WEB TRUE)
else()
    set(FLUX_WEB FALSE)
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
# Skip on Web — replaced by Emscripten Fetch API (flux_http_web.cpp)
# ============================================================================

if(NOT EMSCRIPTEN)
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
endif()

# ============================================================================
# stb  (local — header-only + single implementation file)
# ============================================================================
# Headers-only interface target — no compiled implementation. Exists
# separately from the `stb` STATIC target below so consumers that provide
# their own STB_*_IMPLEMENTATION in a self-contained TU (flux_ssr's
# flux_font_ssr.cpp / flux_image_ssr.cpp) can get the include path
# WITHOUT linking stb_impl.cpp.o's compiled symbols into their binary.
# Linking the compiled `stb` target there is what silently relies on
# "nothing else in the link needs stb_impl.cpp.o" to avoid duplicate
# stbi_*/stbtt_* symbols — see ssr/CMakeLists.txt's comment.
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/stb>
        $<INSTALL_INTERFACE:include>
)

add_library(stb STATIC src/stb_impl.cpp)
target_link_libraries(stb PUBLIC stb_headers)

# ============================================================================
# dr_libs  (local — header-only + single implementation file)
# ============================================================================

add_library(dr STATIC src/flux_audio_impl.cpp)
target_include_directories(dr PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/dr>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Linux platform dependencies
# Guarded against Emscripten — UNIX is TRUE on Linux host even when
# cross-compiling to web, so the guard is required.
# ============================================================================

if(FLUX_NEED_DESKTOP_LIB AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
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

if(FLUX_NEED_DESKTOP_LIB)

file(GLOB_RECURSE FLUX_SOURCES CONFIGURE_DEPENDS src/*.cpp)

if(APPLE)
    file(GLOB_RECURSE FLUX_MM_SOURCES CONFIGURE_DEPENDS src/*.mm)
    foreach(_mm_src ${FLUX_MM_SOURCES})
        set_source_files_properties(${_mm_src}
            PROPERTIES
                COMPILE_FLAGS "-x objective-c++"
        )
    endforeach()
    list(APPEND FLUX_SOURCES ${FLUX_MM_SOURCES})
endif()

endif() # FLUX_NEED_DESKTOP_LIB

if(FLUX_NEED_DESKTOP_LIB)

# ── Platform source exclusions ────────────────────────────────────────────────
# Order matters: most-specific first.

if(NOT WIN32)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_win32\\.cpp$")
endif()

# Linux sources excluded on all non-Linux platforms AND when cross-compiling
# to web from a Linux host machine.
if(NOT (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN))
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_linux\\.cpp$")
endif()

if(NOT ANDROID)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_android\\.cpp$")
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_android\\.mm$")
endif()

if(NOT APPLE)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_macos\\.mm$")
endif()

if(NOT EMSCRIPTEN)
    list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_web\\.cpp$")
endif()

# ── DOM-renderer files are ALWAYS excluded by default ────────────────────────
# Unlike _web.cpp (excluded only on non-web builds), these files match no
# platform-name pattern, so without an explicit exclusion here they'd be
# swept into FLUX_SOURCES and compiled on EVERY build (their own
# __EMSCRIPTEN__ guard only protects against non-web platforms, not against
# being compiled alongside the canvas renderer on a web build). Always
# exclude here; re-included below ONLY when FLUX_WEB_RENDERER == "dom".
list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_painter_dom\\.cpp$")
list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_font_dom\\.cpp$")
list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_window_dom\\.cpp$")
list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_dom_adapter_live\\.cpp$")



# ── Renderer-specific exclusion, web builds only ─────────────────────────────
# Both sets of files match "*_web.cpp$" / live alongside it, so this filter
# runs AFTER the general web-inclusion logic above, narrowing further based
# on which renderer was actually selected. On non-web builds both branches
# are no-ops since FLUX_SOURCES contains none of these files at all (already
# filtered out above).
if(EMSCRIPTEN)
    if(FLUX_WEB_RENDERER STREQUAL "dom")
        # Only flux_painter_web.cpp and flux_window_web.cpp are actually
        # REPLACED by the dom renderer. flux_font_web.cpp is NOT replaced —
        # flux_font_dom.cpp only adds measurement functions alongside it
        # and relies on FontCache's real implementation staying right here.
        # Excluding it (as an earlier draft of this file did) is exactly
        # what produces "undefined symbol: FontCache::getFont" at link time.
        list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_painter_web\\.cpp$")

        list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_window_web\\.cpp$")
        list(APPEND FLUX_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/flux_painter_dom.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/flux_font_dom.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/flux_window_dom.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/flux_dom_adapter_live.cpp
        )
    endif()
    # FLUX_WEB_RENDERER=canvas: the unconditional excludes above already
    # removed the dom files from FLUX_SOURCES; nothing further needed —
    # flux_painter_web.cpp/flux_font_web.cpp/flux_window_web.cpp were
    # never touched by any exclusion, so they remain, as before.
    # FLUX_WEB_RENDERER=canvas: no exclusion needed — the existing glob
    # already picked up flux_painter_web.cpp/flux_font_web.cpp/
    # flux_window_web.cpp, and flux_painter_dom.cpp etc. were never
    # globbed as *_web.cpp in the first place, so they're simply absent.
endif()


list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*stb_impl\\.cpp$")
list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*flux_audio_impl\\.cpp$")

add_library(flux STATIC ${FLUX_SOURCES})
add_library(flux::flux ALIAS flux)

# ── Include directories ───────────────────────────────────────────────────────

target_include_directories(flux
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>

        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${SDL2_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${CAIRO_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${PANGOCAIRO_INCLUDE_DIRS}>

        PRIVATE
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${AVFORMAT_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${AVCODEC_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${AVUTIL_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${SWSCALE_INCLUDE_DIRS}>
        $<$<BOOL:${ANDROID}>:${NATIVE_APP_GLUE_DIR}>
        $<$<BOOL:${ANDROID}>:${MBEDTLS_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${ALSA_INCLUDE_DIRS}>
        $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>:
            ${JPEG_INCLUDE_DIRS}>
)

# ── Compile definitions ───────────────────────────────────────────────────────

target_compile_definitions(flux PUBLIC
        $<$<CONFIG:Debug>:FLUX_DEBUG>
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<OR:$<BOOL:${WIN32}>,$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${ANDROID}>>,$<NOT:$<BOOL:${EMSCRIPTEN}>>>>:
            CURL_STATICLIB>
        FLUX_DEBUG_OVERFLOW=1
        CURL_ENABLE_WEBSOCKETS
)

if(EMSCRIPTEN)
    target_compile_definitions(flux PUBLIC FLUX_WEB)
    if(FLUX_WEB_RENDERER STREQUAL "dom")
        target_compile_definitions(flux PUBLIC FLUX_WEB_RENDERER_DOM)
    endif()
endif()

# ── Link libraries ────────────────────────────────────────────────────────────

if(WIN32)
    target_link_libraries(flux PUBLIC
            stb
            dr
            CURL::libcurl
            dxguid
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

elseif(EMSCRIPTEN)
    target_link_libraries(flux PUBLIC
            stb
            dr
    )

    # target_link_options(flux PUBLIC
    #         # WebGL2
    #         "-sUSE_WEBGL2=1"
    #         "-sFULL_ES3=1"
    #         # Memory — grow as needed, start at 64MB
    #         "-sALLOW_MEMORY_GROWTH=1"
    #         "-sINITIAL_MEMORY=67108864"
    #         # Emscripten Fetch API replaces libcurl
    #         "-sFETCH=1"
    #         # Runtime methods callable from JS side
    #         "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']"
    #         # Target environment
    #         "-sENVIRONMENT=web"
    # )

    # # Debug-only — adds bounds checking and heap validation
    # # Removed from release builds for performance
    # target_link_options(flux PUBLIC
    #         $<$<CONFIG:Debug>:-sASSERTIONS=2>
    #         $<$<CONFIG:Debug>:-sSAFE_HEAP=1>
    # )

elseif(ANDROID)
    find_library(log-lib     log)
    find_library(android-lib android)
    find_library(egl-lib     EGL)
    find_library(gles-lib    GLESv3)

    target_link_libraries(flux PUBLIC
            stb
            dr
            app_glue
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
            dr
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
            dr
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


# ============================================================================
# Registry-managed packages (added via `flux add`)
# Each external/<name>/flux-package.cmake is written by `flux add` and
# removed by `flux remove` — nothing here needs to change per package.
# ============================================================================
file(GLOB FLUX_PACKAGE_FILES CONFIGURE_DEPENDS external/*/flux-package.cmake)
foreach(pkg_file ${FLUX_PACKAGE_FILES})
    include(${pkg_file})
endforeach()

# ── Compiler options ──────────────────────────────────────────────────────────

if(MSVC)
    target_compile_options(flux PUBLIC /utf-8)
    target_compile_options(flux PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
elseif(EMSCRIPTEN)
    target_compile_options(flux PRIVATE
            -Wall
            -Wextra
            -fexceptions
    )
elseif(APPLE)
    # Already handled in the Apple target_link_libraries block above
elseif(UNIX)
    target_compile_options(flux PRIVATE
            -Wall -Wextra -Wpedantic -fexceptions
    )
endif()

endif() # FLUX_NEED_DESKTOP_LIB

# ============================================================================
# Optional subdirectories
# Tests excluded on web — no native test runner available
# ============================================================================

option(FLUX_BUILD_CLI "Build the flux CLI (cli/)" ON)

if(NOT ANDROID AND FLUX_NEED_DESKTOP_LIB)
    if(EMSCRIPTEN)
        add_subdirectory(web)
    elseif(WIN32)
        add_subdirectory(windows)
    elseif(APPLE)
        add_subdirectory(macos)
    elseif(UNIX)
        add_subdirectory(linux)
    endif()

    if(NOT EMSCRIPTEN AND FLUX_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli")
        add_subdirectory(cli)   # native desktop hosts only — skipped automatically
                                 # in projects generated by `flux create`, which
                                 # strip cli/ since users get the CLI as a
                                 # standalone binary from GitHub Releases instead
    endif()

    if(FLUX_BUILD_TESTS AND NOT EMSCRIPTEN)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

# flux_ssr never links against `flux` (see ssr/CMakeLists.txt) — deliberately
# OUTSIDE the FLUX_NEED_DESKTOP_LIB guard above, so `-DFLUX_BUILD_SSR=ON
# -DFLUX_BUILD_DESKTOP=OFF` builds flux_ssr with NO desktop deps at all.
if(NOT ANDROID AND FLUX_BUILD_SSR AND NOT EMSCRIPTEN)
    add_subdirectory(ssr)
endif()

# ============================================================================
# Install rules
# Skipped on Android (ships inside APK) and Web (ships as .html/.js/.wasm)
# ============================================================================

if(NOT ANDROID AND NOT EMSCRIPTEN)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(FLUX_INSTALL_TARGETS stb stb_headers dr)
    if(FLUX_NEED_DESKTOP_LIB)
        list(APPEND FLUX_INSTALL_TARGETS flux)
    endif()

    install(TARGETS ${FLUX_INSTALL_TARGETS}
            EXPORT  fluxTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY external/stb/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h"
    )

    install(DIRECTORY external/dr/
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