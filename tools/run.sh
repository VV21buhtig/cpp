#!/bin/sh
# НАЧАЛО — движок: общая 3D-сцена (террейн 9x9 + SDF-витрина + куб-меш),
# каждое ядро рисует её по-своему. Окно закрывается само / по Esc... нет:
# окно живёт пока не закроешь, либо --frames N для автовихвату.
# Запуск из любого места:
#   ./tools/run.sh [gl|vk|webgpu|sdf|mesh] [--frames N]
#   VOX_CAM="yaw,pitch" VOX_DAYT=242 VOX_SHOT=shot.ppm VOX_SHOT_AT=60 ./tools/run.sh webgpu
# Нужен дисплей (настоящее окно GLFW).
set -e
CORE=${1:-gl}
shift 2>/dev/null || true
case "$CORE" in
    gl|vk|webgpu|sdf|mesh) ;;
    *) echo "core: gl|vk|webgpu|sdf|mesh (а не '$CORE')" >&2; exit 1 ;;
esac
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target scene3d
if [ -z "$DISPLAY" ]; then
    echo "need DISPLAY (движок открывает настоящее окно)" >&2
    exit 1
fi
exec "$ROOT/engine/build/scene3d" --core "$CORE" "$@"
