#!/usr/bin/env bash
# Builds and runs the C core tests on the host, then the Python tests.
# The C test writes the bytes of one frame; the Python test checks that its own
# encoder produces exactly the same bytes (C and Python agree on the format).
set -euo pipefail
cd "$(dirname "$0")"

SRC=firmware/rail_monitor/src
BUILD="$(realpath -m "${BUILD_DIR:-build}")"
mkdir -p "$BUILD"
gcc -std=c99 -Wall -Wextra -Wpedantic -O2 \
    firmware/tests/test_core.c $SRC/crc16.c $SRC/frame.c $SRC/fault.c $SRC/vote.c \
    -o "$BUILD/test_core"

( cd firmware/tests && "$BUILD/test_core" )
python3 -m unittest discover -s ground -v
