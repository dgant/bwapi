#!/usr/bin/env bash
set -euo pipefail
ROOT="/workspace/bwapi"
OUT_DIR="$ROOT/tests/bin"
OUT_EXE="$OUT_DIR/ExampleAIClient.exe"
mkdir -p "$OUT_DIR"

i686-w64-mingw32-g++ -std=gnu++17 -O2 -pipe -static-libgcc -static-libstdc++ -DBUILD_DEBUG=0 \
  -I"$ROOT/bwapi/include" \
  -I"$ROOT/bwapi/include/BWAPI/Client" \
  -I"$ROOT/bwapi/BWAPIClient/Source" \
  -I"$ROOT/bwapi/BWAPILIB/Source" \
  -I"$ROOT/bwapi/Shared" \
  "$ROOT/bwapi/ExampleAIClient/Source/ExampleAIClient.cpp" \
  "$ROOT"/bwapi/BWAPIClient/Source/*.cpp \
  "$ROOT"/bwapi/BWAPILIB/Source/*.cpp \
  "$ROOT"/bwapi/BWAPILIB/UnitCommand.cpp \
  "$ROOT"/bwapi/Shared/*.cpp \
  -o "$OUT_EXE"

echo "Built $OUT_EXE"
