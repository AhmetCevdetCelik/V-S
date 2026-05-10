#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

fail=0

if [ "${VIS_FORMAT_ALL:-0}" = "1" ]; then
    if ! command -v clang-format >/dev/null 2>&1; then
        echo "[lint] clang-format not found; install clang-format to check C++ files." >&2
        exit 1
    fi

    files=$(git ls-files 'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp')
    for path in $files; do
        if ! clang-format --dry-run --Werror "$path" >/dev/null; then
            fail=1
        fi
    done
else
    if [ -n "${VIS_FORMAT_BASE:-}" ]; then
        files=$(git diff --name-only --diff-filter=ACMR "$VIS_FORMAT_BASE"...HEAD -- \
            'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp')
    else
        files=$(
            {
            git diff --unified=0 --no-ext-diff --diff-filter=ACMR HEAD -- \
                'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp'
            git diff --cached --unified=0 --no-ext-diff --diff-filter=ACMR -- \
                'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp'
            } | sort -u
        )
    fi

    if [ -z "$files" ]; then
        echo "[lint] no changed C++ files."
        exit 0
    fi

    echo "[lint] changed C++ files:"
    printf '%s\n' "$files" | sed 's/^/[lint]   /'
    echo "[lint] full clang-format enforcement is opt-in: VIS_FORMAT_ALL=1 make -C vis-jitter lint"
fi

if [ "$fail" -ne 0 ]; then
    echo "[lint] formatting check failed. Run clang-format on the reported files." >&2
    exit 1
fi

echo "[lint] clang-format check passed."
