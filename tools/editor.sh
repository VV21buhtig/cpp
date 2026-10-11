#!/bin/sh
# Редактор вживую: сборка + демо-проект + окно. Просто смотри и кликай.
# Запуск из любого места:
#   ./tools/editor.sh [2|3|1]   (бэкенд: 2 GL, 3 VK, 1 WebGPU; дефолт 2)
# Нужен дисплей; без $DISPLAY скрипт откажется (offscreen тут бессмыслен).
set -e
BE=${1:-2}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
. "$ROOT/tools/mkdemo.sh"
mkbuild "$ROOT"
PROJ=$(mkdemo "$ROOT")
if [ -z "$DISPLAY" ]; then
    echo "need DISPLAY (окно редактора некуда показать)" >&2
    exit 1
fi
ED_PROJECT=$PROJ ED_BACKEND=$BE ./frontend/build/editor
