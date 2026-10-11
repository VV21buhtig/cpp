#!/bin/sh
# Матрица редактора: все 3 бэкенда (GL -> VK -> WebGPU) по N мс каждый
# в одном окне редактора, с вердиктами ADVANCE/PICTURE и кадрами фаз.
# Запуск из любого места:
#   ./tools/matrix.sh [мс_на_фазу]   (дефолт 30000)
# Итог: вердикты в stdout, кадры в run/matrix/shots/phase*-*.png.
# Без дисплея (нет $DISPLAY) — offscreen, доказательство всё равно честное.
set -e
MS=${1:-30000}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
. "$ROOT/tools/mkdemo.sh"
mkbuild "$ROOT"
PROJ=$(mkdemo "$ROOT")
SHOTS=$ROOT/run/matrix/shots
mkdir -p "$SHOTS"
if [ -z "$DISPLAY" ]; then
    echo "note: no DISPLAY, offscreen (окна не видно, кадры и вердикты — да)"
    export QT_QPA_PLATFORM=offscreen
fi
ED_PROJECT=$PROJ ED_MATRIX=1 ED_MATRIX_MS=$MS ED_MATRIX_SHOTDIR=$SHOTS \
    ./frontend/build/editor
echo "shots: $SHOTS"
