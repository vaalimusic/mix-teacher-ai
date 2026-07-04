#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos --config Release

echo "Built VST3:"
echo "build-macos/MixTeacher_artefacts/Release/VST3/Mix Teacher.vst3"
