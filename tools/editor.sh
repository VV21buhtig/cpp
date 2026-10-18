#!/bin/sh
# РЕДАКТОР (хост, §13/§14): приложение с режимами. Без ключей — режим "ui".
#   ./tools/editor.sh                 # режим ui (правка фронта)
#   ./tools/editor.sh --mode ui --selftest
#   ./tools/editor.sh --mode ui       # то же, что ./tools/uiedit.sh
# ENV (ручки теста режима ui): UI_EDIT_WIN=1280x720 UI_EDIT_SHOT=shot.ppm
#   UI_EDIT_SHOT_AT=60 UI_EDIT_SEED=1 UI_EDIT_VSYNC=0 UI_EDIT_BENCH=1 ...
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target editor
exec ./engine/build/editor "$@"
