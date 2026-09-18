#!/usr/bin/env bash
# Build e verificacao do PlayAmpNG.
#   ./build.sh          configura, compila e roda os testes
#   ./build.sh clean    remove o diretorio de build antes
set -euo pipefail

BUILD_DIR=${BUILD_DIR:-build}
[[ ${1:-} == clean ]] && rm -rf "$BUILD_DIR"

cmake -S . -B "$BUILD_DIR" -G Ninja
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo
echo "gui:    $BUILD_DIR/playampng"
echo "probe:  $BUILD_DIR/pang_probe [arquivo-ou-url ...]"
