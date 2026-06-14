#!/usr/bin/env bash
# Build the browser (WASM) randomizer into ./build-wasm and assemble a
# ready-to-deploy bundle there: index.html + lagoon-randomizer.js + lagoon-randomizer.wasm.
# Upload the contents of ./build-wasm to any static web host.
#
# Requires emsdk to be sourced, or it falls back to the emsdk path baked into
# cmake/Emscripten.cmake.
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-wasm}"

cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$(pwd)/cmake/Emscripten.cmake" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Derive a content-based version from the page source + built module, stamp it
# into the page, and write a sibling version.json. The deployed page polls
# version.json and shows a "new version available" banner when it changes.
VERSION="$(cat web/index.html "$BUILD_DIR/lagoon-randomizer.js" "$BUILD_DIR/lagoon-randomizer.wasm" | sha256sum | head -c 12)"
sed "s/__APP_VERSION__/${VERSION}/" web/index.html > "$BUILD_DIR/index.html"
printf '{"version":"%s"}\n' "$VERSION" > "$BUILD_DIR/version.json"

echo
echo "Built deployable bundle in: $BUILD_DIR/  (version ${VERSION})"
echo "  - index.html"
echo "  - lagoon-randomizer.js"
echo "  - lagoon-randomizer.wasm"
echo "  - version.json"
echo
echo "Test locally:  (cd $BUILD_DIR && python3 -m http.server 8000)  then open http://localhost:8000/"
