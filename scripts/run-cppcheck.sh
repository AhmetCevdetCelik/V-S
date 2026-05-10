#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "[lint] cppcheck not found; skipping optional static analysis."
    exit 0
fi

cppcheck \
    --enable=warning,style,performance,portability \
    --std=c++17 \
    --inline-suppr \
    --suppress=missingIncludeSystem \
    --error-exitcode=1 \
    -I vis-jitter/include \
    vis-jitter/include \
    vis-jitter/src \
    vis-jitter/tests
