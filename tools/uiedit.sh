#!/bin/sh
# Режим «UI» редактора (§13): правка фронта, без 3D. Это editor --mode ui.
# Файлы-обменники с игровым редактором: layout.json + theme.json.
# Запуск из любого места: ./tools/uiedit.sh [--selftest]
# ENV: UI_EDIT_SHOT=shot.ppm UI_EDIT_SHOT_AT=60.
# Тесты/скриншоты — из корня репо (относительные пути engine/assets).
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target editor
exec ./engine/build/editor --mode ui "$@"
