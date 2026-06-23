@echo off
call %~dp0\..\external\emsdk\emsdk_env.bat
cd %~dp0\..
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLUX_BUILD_EXAMPLES=ON
cmake --build build-web
emrun build-web/lib/flux_app.html