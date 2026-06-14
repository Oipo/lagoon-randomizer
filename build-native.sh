#!/usr/bin/env bash
# Build the native Linux CLI into ./build, producing ./build/lagoon-randomizer.
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build}"

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "Built: $BUILD_DIR/lagoon-randomizer"
echo "Run:   ./$BUILD_DIR/lagoon-randomizer -i input.sfc -o output.sfc"
