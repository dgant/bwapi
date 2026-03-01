#!/usr/bin/env bash
set -euo pipefail

ROOT="/workspace/bwapi"
OUT_DIR="$ROOT/tests/bin"
OUT_EXE="$OUT_DIR/asynchrony_tests.exe"
WINE_BIN="${WINE_BIN:-/opt/wine-staging/bin/wine}"
mkdir -p "$OUT_DIR"

cd "$ROOT"

i686-w64-mingw32-g++ -std=gnu++17 -O0 -g0 -pipe -static-libgcc -static-libstdc++ -DBUILD_DEBUG=0 \
  -I"$ROOT/bwapi/include" \
  -I"$ROOT/bwapi/include/BWAPI/Client" \
  -I"$ROOT/bwapi/BWAPIClient/Source" \
  -I"$ROOT/bwapi/BWAPILIB/Source" \
  -I"$ROOT/bwapi/Shared" \
  "$ROOT/tests/asynchrony_tests.cpp" \
  "$ROOT"/bwapi/BWAPIClient/Source/*.cpp \
  "$ROOT"/bwapi/BWAPILIB/Source/*.cpp \
  "$ROOT"/bwapi/BWAPILIB/UnitCommand.cpp \
  "$ROOT"/bwapi/Shared/*.cpp \
  -o "$OUT_EXE"

if [[ "${RUN_WINE_TESTS:-0}" == "1" ]]; then
  "$WINE_BIN" "$OUT_EXE"
else
  echo "Built $OUT_EXE (set RUN_WINE_TESTS=1 to execute under Wine)"
fi
