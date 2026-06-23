@echo off
cd %~dp0\..
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON
cmake --build build-windows
build-windows\lib\flux_app.exe