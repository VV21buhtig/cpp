#!/bin/sh
# Редактор UI (uitoolkit): окно, панели, гизмо, EDIT-режим (L).
# Запуск из любого места: ./tools/ui.sh [--selftest]
# ENV: UI_TRACE=1 (лог ручных действий), FUI_DEMO=drag|dragfar|edit|swap,
#      UI_SHOT=shot.ppm UI_SHOT_AT=200, FUI_TIME=1, FUI_NOVSYNC=1.
# Тесты/скриншоты — из корня репо (относительные пути engine/assets).
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target ui_test
exec ./engine/build/ui_test "$@"
