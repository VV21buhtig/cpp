#!/bin/sh
# Прямой вьювер (схема Roblox/UE5): отдельный процесс, своё окно, GPU->GPU.
# Использование: ./tools/direct.sh [gl] [mesh]  (бэкенд сейчас только gl)
# Drag — орбита на каждую сторону, wheel — зум (встроить позже), Esc/крестик — выход.
# Автовращения нет. Electron при этом — только меню (чекбокс native-view
# делает то же самое из UI: спавнит этот же бинарь).
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
cmake -S engine -B engine/build >/dev/null
cmake --build engine/build --target direct_view
exec ./engine/build/direct_view "${1:-gl}" "${2:-mesh}"
