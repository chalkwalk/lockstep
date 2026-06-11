#!/usr/bin/env bash
# Usage: tools/check.sh [--tidy]
#   Format gate always runs (clang-format --dry-run -Werror).
#   --tidy: measure mode — reports tidy finding count but never fails.
#           Flip to gating later by removing -warnings-as-errors='-*'.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

mapfile -t FILES < <(git ls-files 'src/**/*.h' 'src/**/*.cpp' 'src/*.h' 'src/*.cpp' \
                                  'tests/*.h' 'tests/*.cpp')

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "check.sh: no tracked src/tests files found"
    exit 1
fi

clang-format --dry-run -Werror "${FILES[@]}"
echo "clang-format: OK (${#FILES[@]} files)"

if [[ "${1:-}" == "--tidy" ]]; then
    if [[ ! -f build/compile_commands.json ]]; then
        echo "check.sh --tidy: build/compile_commands.json not found; run cmake first"
        exit 1
    fi
    echo "clang-tidy: measuring findings (non-gating)..."
    COUNT=$(run-clang-tidy -p build -quiet -warnings-as-errors='-*' \
        2>/dev/null | grep -cE 'warning:|error:' || true)
    echo "clang-tidy: ${COUNT} finding(s)"
fi
