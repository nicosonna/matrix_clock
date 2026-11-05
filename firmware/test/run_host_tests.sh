#!/usr/bin/env bash
# Host tests of the pure logic (no ESP-IDF, no ESP32).
# Usage: bash firmware/test/run_host_tests.sh [--coverage]
# Runs under Git Bash (unlike idf.py). Needs MinGW g++ and CMake.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COVERAGE=OFF
[ "${1:-}" = "--coverage" ] && COVERAGE=ON
# Separate build directory for coverage: the flags differ.
BUILD="$HERE/build"
[ "$COVERAGE" = "ON" ] && BUILD="$HERE/build-cov"

# MinGW g++ (msys64) first in PATH so no other compiler is picked up.
export PATH="/c/msys64/mingw64/bin:$PATH"

# Ninja: ESP-IDF's copy if present, otherwise the one on PATH.
NINJA="$(ls -d "$HOME"/.espressif/tools/ninja/*/ninja.exe 2>/dev/null | sort -V | tail -1 || true)"
if [ -z "$NINJA" ]; then
    NINJA="$(command -v ninja || true)"
fi
if [ -z "$NINJA" ]; then
    echo "ninja not found (neither ~/.espressif/tools/ninja nor PATH)." >&2
    exit 2
fi

cmake -S "$HERE" -B "$BUILD" -G Ninja \
      -DCMAKE_MAKE_PROGRAM="$NINJA" \
      -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCLOCK_COVERAGE="$COVERAGE"
cmake --build "$BUILD"
# Stale profile data from a previous run would mismatch the rebuilt objects.
find "$BUILD" -name '*.gcda' -delete
ctest --test-dir "$BUILD" --output-on-failure

if [ "$COVERAGE" = "ON" ]; then
    # Home-made gcov aggregation (no gcovr needed): target >= 95 % on the pure
    # components; non-zero exit code otherwise.
    python -I "$HERE/coverage_summary.py" "$BUILD" "$HERE/../components/display"
fi
