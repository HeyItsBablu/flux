@echo off
call %~dp0\..\external\emsdk\emsdk_env.bat
cd %~dp0\..

emcmake cmake -S . -B build\web -G Ninja -DCMAKE_BUILD_TYPE=Release

cmake --build build\web

emrun build\web\web\flux_app.html