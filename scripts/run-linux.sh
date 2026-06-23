#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFLUX_BUILD_EXAMPLES=ON \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++

cmake --build build-linux

./build-linux/lib/flux_app