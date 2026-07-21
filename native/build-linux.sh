#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$root_dir/native/linux/build"

cmake -S "$root_dir/native/linux" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel
cp "$build_dir/libMediaPlayer.so" "$root_dir/common/src/main/resources/libMediaPlayer.so"
echo "Built common/src/main/resources/libMediaPlayer.so"
