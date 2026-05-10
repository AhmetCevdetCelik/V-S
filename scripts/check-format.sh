#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if [ "${VIS_FORMAT_ALL:-0}" = "1" ]; then
    files=$(git ls-files 'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp')
elif [ -n "${VIS_FORMAT_BASE:-}" ]; then
    files=$(git diff --name-only --diff-filter=ACMR "$VIS_FORMAT_BASE"...HEAD -- \
        'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp')
else
    files=$(
        {
            git diff --name-only --diff-filter=ACMR HEAD -- \
                'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp'
            git diff --cached --name-only --diff-filter=ACMR -- \
                'vis-jitter/include/*.hpp' 'vis-jitter/src/*.cpp' 'vis-jitter/tests/*.cpp'
        } | sort -u
    )
fi

if [ -z "$files" ]; then
    echo "[lint] no changed C++ files to format-check."
    exit 0
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "[lint] clang-format not found; install clang-format to check changed C++ files." >&2
    exit 1
fi

fail=0
for path in $files; do
    if ! clang-format --dry-run --Werror "$path" >/dev/null; then
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "[lint] formatting check failed. Run clang-format on the reported files." >&2
    exit 1
fi

echo "[lint] clang-format check passed."
