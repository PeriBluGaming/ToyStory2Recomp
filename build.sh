#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_LAUNCHER=OFF -DPSX_RECOMP_UI=ON
cmake --build "$ROOT/build" --config Release --parallel
