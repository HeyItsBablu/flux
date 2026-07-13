@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM run-web-ssr.bat — builds the web WASM bundle (needed for hydration),
REM builds the native flux_ssr host, and runs it.
REM
REM Two separate build trees, on purpose (see ssr/CMakeLists.txt's header
REM comment): build\web is the Emscripten cross-compile of flux_app, and
REM build\ssr is a native MSVC build of flux_ssr. flux_ssr's HTML response
REM boots the SAME flux_app.js/.wasm/.data the web build produces — so the
REM web bundle must exist and be up to date before flux_ssr can serve a
REM working hydrated page. This script builds both, in the right order.
REM
REM Usage:
REM   scripts\run-web-ssr.bat            (defaults to port 8080)
REM   scripts\run-web-ssr.bat 9000       (custom port)
REM ============================================================================

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "PORT=%~1"
if "%PORT%"=="" set "PORT=8080"

pushd "%REPO_ROOT%"

REM ── 1. Build the web (Emscripten) bundle first ──────────────────────────
REM flux_ssr serves flux_app.js/.wasm/.data straight off disk at runtime
REM (see FLUX_SSR_WEB_BUNDLE_DIR in ssr/CMakeLists.txt) — it does NOT
REM rebuild them itself. Reusing build_web.bat here keeps this in one
REM place instead of duplicating the emcmake/ninja invocation.
echo [run-web-ssr] Step 1/3: building web bundle (Emscripten)...
call "%SCRIPT_DIR%build_web.bat" --no-serve
if errorlevel 1 (
    echo [run-web-ssr] ERROR: web bundle build failed.
    popd
    exit /b 1
)

REM ── 2. Configure the native SSR build (only if not already configured) ──
set "SSR_BUILD_DIR=%REPO_ROOT%\build\ssr"
if not exist "%SSR_BUILD_DIR%\CMakeCache.txt" (
    echo [run-web-ssr] Step 2/3: configuring flux_ssr...
    cmake -B build\ssr ^
        -DFLUX_BUILD_SSR=ON ^
        -DFLUX_BUILD_DESKTOP=OFF ^
        -DFLUX_SSR_WEB_BUNDLE_DIR="%REPO_ROOT%\build\web\web"
    if errorlevel 1 (
        echo [run-web-ssr] ERROR: cmake configure failed for build\ssr.
        popd
        exit /b 1
    )
) else (
    echo [run-web-ssr] Step 2/3: build\ssr already configured, skipping cmake configure.
    echo [run-web-ssr]           ^(delete build\ssr to force a fresh configure^)
)

REM ── 3. Build flux_ssr ─────────────────────────────────────────────────────
echo [run-web-ssr] Step 3/3: building flux_ssr...
cmake --build build\ssr
if errorlevel 1 (
    echo [run-web-ssr] ERROR: flux_ssr build failed.
    popd
    exit /b 1
)

REM ── 4. Locate the actual .exe ─────────────────────────────────────────────
REM MSBuild's multi-config layout puts this under a config subfolder
REM (Debug\ by default) — mirrors build_web.bat's "fail loudly if the
REM output moved" philosophy instead of silently guessing wrong.
set "SSR_EXE="
if exist "%SSR_BUILD_DIR%\ssr\Debug\flux_ssr.exe" (
    set "SSR_EXE=%SSR_BUILD_DIR%\ssr\Debug\flux_ssr.exe"
) else if exist "%SSR_BUILD_DIR%\ssr\Release\flux_ssr.exe" (
    set "SSR_EXE=%SSR_BUILD_DIR%\ssr\Release\flux_ssr.exe"
) else if exist "%SSR_BUILD_DIR%\ssr\flux_ssr.exe" (
    set "SSR_EXE=%SSR_BUILD_DIR%\ssr\flux_ssr.exe"
)

if "%SSR_EXE%"=="" (
    echo [run-web-ssr] ERROR: flux_ssr.exe not found under "%SSR_BUILD_DIR%\ssr".
    echo [run-web-ssr] Build may have succeeded but output moved — check
    echo [run-web-ssr] build\ssr\ssr for the real location and update this script.
    popd
    exit /b 1
)

REM ── 5. Font check — flux_ssr throws at first getFont() call without these ─
if not exist "%REPO_ROOT%\fonts\Regular.ttf" (
    echo [run-web-ssr] WARNING: fonts\Regular.ttf not found. flux_ssr will
    echo [run-web-ssr] crash on the first request that renders text. See
    echo [run-web-ssr] ssr/CMakeLists.txt's FLUX_SSR_FONT_DIR comment.
)

popd

REM ── 6. Run it ──────────────────────────────────────────────────────────────
echo [run-web-ssr] Starting flux_ssr on port %PORT%...
echo [run-web-ssr] Open http://localhost:%PORT% — this should show real
echo [run-web-ssr] content INSTANTLY, no loading spinner.
"%SSR_EXE%" %PORT%

endlocal