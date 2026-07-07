@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM build_web.bat — builds the Emscripten (WASM) web target and serves it
REM locally with SPA-fallback routing (Phase 2's real URL paths need this;
REM emrun's dev server 404s on anything but the exact built filename).
REM
REM Expected layout: this script lives in <repo>\scripts\build_web.bat
REM
REM Usage:
REM   scripts\build_web.bat              (build + serve on :6931, as before)
REM   scripts\build_web.bat --no-serve   (build only, don't serve — used by
REM                                       run-web-ssr.bat, which builds this
REM                                       bundle then runs flux_ssr instead)
REM ============================================================================

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."

REM ── 1. Activate emsdk ────────────────────────────────────────────────────
set "EMSDK_ENV=%REPO_ROOT%\external\emsdk\emsdk_env.bat"
if not exist "%EMSDK_ENV%" (
    echo [build_web] ERROR: emsdk_env.bat not found at "%EMSDK_ENV%"
    echo [build_web] Did you init the emsdk submodule? e.g.:
    echo     git submodule update --init external\emsdk
    exit /b 1
)
call "%EMSDK_ENV%"
if errorlevel 1 (
    echo [build_web] ERROR: emsdk_env.bat failed to run.
    exit /b 1
)

REM ── 2. Move to repo root ─────────────────────────────────────────────────
if not exist "%REPO_ROOT%\CMakeLists.txt" (
    echo [build_web] ERROR: "%REPO_ROOT%" doesn't look like the repo root
    echo [build_web] ^(no CMakeLists.txt found there^).
    exit /b 1
)
pushd "%REPO_ROOT%"

REM ── 3. Configure ─────────────────────────────────────────────────────────
REM -DFLUX_WEB_RENDERER=dom pinned explicitly rather than relying on the
REM root CMakeLists.txt default — a stale build\web cache from an earlier
REM run (e.g. before this variable existed, or manually set to "canvas"
REM for a quick test) would otherwise silently keep building the WRONG
REM renderer, and Phase 4/5's SSR hydration needs the DOM one specifically.
echo [build_web] Configuring...
call emcmake cmake -S . -B build\web -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DFLUX_WEB_RENDERER=dom
if errorlevel 1 (
    echo [build_web] ERROR: cmake configure failed.
    popd
    exit /b 1
)

REM ── 4. Build ──────────────────────────────────────────────────────────────
echo [build_web] Building...
cmake --build build\web
if errorlevel 1 (
    echo [build_web] ERROR: build failed.
    popd
    exit /b 1
)

REM ── 5. Locate the actual output directory ───────────────────────────────
REM add_subdirectory(web) from the root CMakeLists.txt mirrors the source
REM tree, so flux_app.html lands under build\web\web\ — but if that ever
REM changes (generator differences, moved add_subdirectory call, etc.),
REM fail loudly here instead of silently serving an empty/wrong directory.
set "WEB_OUT=%REPO_ROOT%\build\web\web"
if not exist "%WEB_OUT%\flux_app.html" (
    echo [build_web] ERROR: flux_app.html not found under "%WEB_OUT%"
    echo [build_web] Build may have succeeded but output moved — check
    echo [build_web] build\web for the real location and update WEB_OUT above.
    popd
    exit /b 1
)
popd

REM ── 6. Serve with SPA fallback (skip if --no-serve was passed) ───────────
REM run-web-ssr.bat calls this script to build the bundle only, then runs
REM flux_ssr instead of spa_server.py — without this guard, control would
REM never return to run-web-ssr.bat since spa_server.py blocks forever.
if /I "%~1"=="--no-serve" goto :skip_serve

echo [build_web] Serving "%WEB_OUT%" at http://localhost:6931
echo [build_web] Point flux_ssr's FLUX_SSR_WEB_BUNDLE_DIR at this same
echo [build_web] directory to enable hydration (Phase 5).
python "%SCRIPT_DIR%spa_server.py" "%WEB_OUT%"
goto :eof

:skip_serve
echo [build_web] --no-serve passed — build complete, not starting server.

:eof
endlocal