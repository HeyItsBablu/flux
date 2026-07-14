# ssr/CMakeLists.txt
#
# Builds flux_ssr — a native, headless server binary. Deliberately does NOT
# link against the `flux` target built by the root CMakeLists.txt (that
# target is hardwired to flux_painter_linux.cpp/Cairo on native Linux —
# see the comment in the root file). Instead this hand-picks a source list
# that swaps painting/windowing/font-measurement for the SSR-specific
# equivalents, while reusing every platform-agnostic file unchanged.
#
# Font backend is stb_truetype (flux_font_ssr.cpp), NOT Pango — this is
# what makes the whole target platform-independent. No pkg-config, no
# vcpkg, no fontconfig. In exchange, SSR needs bundled .ttf files at
# runtime; see FLUX_SSR_FONT_DIR below.

set(FLUX_SSR_SOURCES
    ${CMAKE_SOURCE_DIR}/src/flux_core.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_window_headless.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_painter_dom.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_font_ssr.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_dom_adapter_stringbuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_image_ssr.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_audio_ssr.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_video_ssr.cpp
    ${CMAKE_SOURCE_DIR}/src/flux_camera_ssr.cpp
    ${CMAKE_SOURCE_DIR}/src/camera_widget_dom.cpp

)

add_executable(flux_ssr
    main.cpp
    ${CMAKE_SOURCE_DIR}/lib/main.cpp
    ${FLUX_SSR_SOURCES}
)

target_include_directories(flux_ssr PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(flux_ssr PRIVATE
    FLUX_SSR
    $<$<CONFIG:Debug>:FLUX_DEBUG>
    # Required whenever <windows.h> gets pulled in (via winsock2.h in
    # main.cpp) and this target isn't linked against `flux` (which already
    # sets this for itself, but only for ITSELF — flux_ssr is a separate
    # target with its own source list and doesn't inherit it).
    # Without NOMINMAX, windows.h #defines min/max as macros, and the
    # preprocessor blindly expands the bare "min"/"max" token inside
    # std::min(...)/std::max(...) even after the `std::` qualifier —
    # producing exactly the "illegal token on right side of '::'" errors
    # you're seeing in flux_scrollbar.hpp/flux_state.hpp/etc. Those
    # headers are fine; this is what was missing.
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
)

# ============================================================================
# Font assets
#
# stb_truetype does no fontconfig-style family resolution — flux_font_ssr.cpp
# maps every request onto one of two bundled files by weight (regular/bold),
# looked up relative to FLUX_SSR_FONT_DIR at runtime (default "fonts", next
# to flux_ssr's working directory when it runs). Override with:
#
#   cmake -B build\ssr -DFLUX_SSR_FONT_DIR=C:/absolute/path/to/fonts
#
# You must supply Regular.ttf and Regular-Bold.ttf yourself (e.g. a
# redistributable family like Noto Sans / Inter) — none are bundled by
# this repo. flux_ssr throws at first getFont() call if the directory or
# files are missing; this is a configure-time reminder, not a hard
# failure, since the directory may only exist on the target deploy host.
# ============================================================================

set(FLUX_SSR_FONT_DIR "fonts" CACHE STRING
    "Directory (relative to flux_ssr's working dir, or absolute) containing Regular.ttf / Regular-Bold.ttf")

# Windows paths use backslashes, and backslash+letter (\f, \b, \n, ...) is a
# C++ escape sequence — embedding a raw Windows path straight into a string
# literal via target_compile_definitions silently corrupts it (e.g. "...\flux\..."
# becomes "...<form-feed>lux..." because \f means form-feed, \b means
# backspace, etc). Forward slashes are accepted by every file API this
# target actually calls (fopen/ifstream), so normalize here instead of
# trying to double-escape backslashes.
string(REPLACE "\\" "/" FLUX_SSR_FONT_DIR "${FLUX_SSR_FONT_DIR}")


target_compile_definitions(flux_ssr PRIVATE
    FLUX_SSR_FONT_DIR="${FLUX_SSR_FONT_DIR}"
)

if(NOT IS_ABSOLUTE "${FLUX_SSR_FONT_DIR}")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${FLUX_SSR_FONT_DIR}/Regular.ttf")
        message(WARNING
            "flux_ssr: '${FLUX_SSR_FONT_DIR}/Regular.ttf' not found relative to "
            "the source tree. flux_ssr will look for it relative to its RUNTIME "
            "working directory instead, but if you haven't placed font files "
            "anywhere yet, do so before running flux_ssr (see the "
            "FLUX_SSR_FONT_DIR comment in this file).")
    endif()
endif()


# ============================================================================
# Web bundle location (Phase 5 — hydration)
#
# flux_ssr's HTML response boots the SAME client WASM bundle a pure
# client-rendered page would (web/CMakeLists.txt's `flux_app` target).
# This does NOT add flux_app as a build dependency of flux_ssr — the two
# targets can be built independently (e.g. web bundle built once via
# Emscripten toolchain, flux_ssr built natively) — it only tells flux_ssr
# WHERE to find the already-built .js/.wasm/.data files at runtime.
#
# Default assumes a sibling build directory; override for your actual
# layout, e.g.:
#   cmake -B build/ssr -DFLUX_SSR_WEB_BUNDLE_DIR=C:/path/to/build/web
# ============================================================================

set(FLUX_SSR_WEB_BUNDLE_DIR "${CMAKE_BINARY_DIR}/web" CACHE STRING
    "Directory containing the built flux_app.js/.wasm/.data (web/CMakeLists.txt's flux_app target)")

# Same backslash-escaping problem as FLUX_SSR_FONT_DIR above — normalize
# before this value is ever used in a compile definition or a path check.
string(REPLACE "\\" "/" FLUX_SSR_WEB_BUNDLE_DIR "${FLUX_SSR_WEB_BUNDLE_DIR}")

target_compile_definitions(flux_ssr PRIVATE
    FLUX_SSR_WEB_BUNDLE_DIR="${FLUX_SSR_WEB_BUNDLE_DIR}"
)

if(NOT EXISTS "${FLUX_SSR_WEB_BUNDLE_DIR}/flux_app.js")
    message(WARNING
        "flux_ssr: 'flux_app.js' not found under FLUX_SSR_WEB_BUNDLE_DIR "
        "(${FLUX_SSR_WEB_BUNDLE_DIR}). flux_ssr will still start, but every "
        "page it serves will show correct content with NO client-side "
        "interactivity until the web bundle is built at that path (see "
        "the FLUX_SSR_WEB_BUNDLE_DIR comment above).")
endif()


# ============================================================================
# Static assets directory
#
# The web build has no equivalent need for a runtime route — assets/ is
# baked into the WASM virtual filesystem at BUILD time via
# web/CMakeLists.txt's `--preload-file=${CMAKE_SOURCE_DIR}/assets/.@/`.
# flux_ssr has no virtual filesystem to preload into and no build step
# that runs per-deploy, so the equivalent has to be a RUNTIME static-file
# route instead: ssr/main.cpp serves real files off disk under this
# directory whenever a request path starts with /assets/.
#
# This is the same problem VideoPlayerWidget's Path/Memory sources hit —
# a widget-supplied local path (e.g. "assets/sample.mp4") lives on the
# SERVER's disk and is not directly fetchable by the browser. Once this
# route exists, widgets can translate a local assets-relative path into
# a `/assets/<relpath>` URL the browser CAN fetch, the same way images
# already do via flux_image_registry_serve's content-addressed /img/
# route — this one is a plain passthrough instead of content-addressed,
# since asset files are developer-managed and don't need cache-busting
# via hash the way uploaded/generated images do.
# ============================================================================

set(FLUX_SSR_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets" CACHE STRING
    "Directory served under the /assets/ URL prefix by flux_ssr (mirrors web/CMakeLists.txt's --preload-file=assets)")

string(REPLACE "\\" "/" FLUX_SSR_ASSETS_DIR "${FLUX_SSR_ASSETS_DIR}")

target_compile_definitions(flux_ssr PRIVATE
    FLUX_SSR_ASSETS_DIR="${FLUX_SSR_ASSETS_DIR}"
)

if(NOT EXISTS "${FLUX_SSR_ASSETS_DIR}")
    message(WARNING
        "flux_ssr: FLUX_SSR_ASSETS_DIR (${FLUX_SSR_ASSETS_DIR}) does not exist. "
        "flux_ssr will still start, but any VideoPlayer/ImageWidget using a "
        "Path source under assets/ will 404 until this directory is present "
        "at that path on the deploy host.")
endif()

# ============================================================================
# Link libraries
# ============================================================================


target_link_libraries(flux_ssr PRIVATE stb_headers)

# Native HTTP fetch (FluxHttp's libcurl path, used in synchronous SSR mode
# — see flux_http.hpp's fluxSetSSRSyncMode). CURL::libcurl is already
# built by the root CMakeLists.txt's add_subdirectory(external/curl) call,
# which runs before this file does — do NOT find_package(CURL) here, that
# searches for a SEPARATE system install instead of reusing the vendored
# submodule build, and fails on machines with no system curl at all.
target_link_libraries(flux_ssr PRIVATE CURL::libcurl)

# Winsock needed by main.cpp's WSAStartup/WSACleanup + socket calls.
if(WIN32)
    target_link_libraries(flux_ssr PRIVATE ws2_32)
endif()

target_compile_features(flux_ssr PRIVATE cxx_std_20)