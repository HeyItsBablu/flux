@echo off
call %~dp0\..\external\emsdk\emsdk_env.bat
cd %~dp0\..

emcmake cmake -S . -B build\web -G Ninja -DCMAKE_BUILD_TYPE=Release

cmake --build build\web

REM emrun has no SPA-fallback support (unknown paths 404 instead of
REM serving flux_app.html) — needed the moment Navigator uses real
REM URL paths (Phase 2) instead of hash fragments. Use a small Python
REM server with a catch-all instead, pointed at the same build output.
python %~dp0\spa_server.py %~dp0\..\build\web\web