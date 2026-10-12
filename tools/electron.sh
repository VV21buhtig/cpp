#!/bin/sh
# Electron-скелет: тонкий клиент над libengine.so (koffi, без сборки биндингов).
# Нужен дисплей (или xvfb-run). Первый запуск ставит npm-пакеты сам.
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target engine_shared
if [ ! -x "$ROOT/electron/node_modules/.bin/electron" ]; then
    (cd "$ROOT/electron" && npm install --no-audit --no-fund)
fi
(cd "$ROOT/electron" && npm run build)
if [ -z "$DISPLAY" ]; then
    echo "no DISPLAY: пробую xvfb-run" >&2
    # shellcheck disable=SC2093
    exec xvfb-run -a "$ROOT/electron/node_modules/.bin/electron" "$ROOT/electron"
fi
exec "$ROOT/electron/node_modules/.bin/electron" "$ROOT/electron"
