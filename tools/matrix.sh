#!/bin/sh
# Матрица бэкендов БЕЗ Qt (после сноса): view_test на каждый бэкенд
# (домен воксель): 8 кадров + вердикты ADVANCE/PICTURE.
# Запуск из любого места: ./tools/matrix.sh
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
[ -f "$ROOT/engine/build/CMakeCache.txt" ] || cmake -S "$ROOT/engine" -B "$ROOT/engine/build"
cmake --build "$ROOT/engine/build" --target view_test
fail=0
for be in 2 3 1; do
    ./engine/build/view_test "$be" || fail=1
done
if [ "$fail" = 0 ]; then
    echo "matrix DONE"
else
    echo "matrix FAIL"
    exit 1
fi
