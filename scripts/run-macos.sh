#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build-macos -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFLUX_BUILD_EXAMPLES=ON \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++

cmake --build build-macos

./build-macos/lib/flux_app