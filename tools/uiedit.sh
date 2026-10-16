#!/bin/sh
# Правщик фронта редактора (§13): отдельное окно, без 3D, всегда EDIT.
# Файлы-обменники с игровым редактором: layout.json + theme.json.
# Запуск из любого места: ./tools/uiedit.sh [--selftest]
# ENV: UI_EDIT_SHOT=shot.ppm UI_EDIT_SHOT_AT=60.
# Тесты/скриншоты — из корня репо (относительные пути engine/assets).
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target ui_edit
exec ./engine/build/ui_edit "$@"
