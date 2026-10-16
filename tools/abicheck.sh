#!/bin/sh
# ABICHECK (P4 тёплого ABI): карантин CPU-пути + версия.
# frame_cpu/download — ТОЛЬКО фолбэк view.c (VK/WG до нативных целей),
# реализации ядер и тесты. Интерактивный GL-путь их не касается.
# W4: нарушение = упавшая матрица, чинить код, не gate.
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
fail=0
# 1. Вызовы frame_cpu/download вне разрешённых мест.
hits=$(grep -rn -- "->frame_cpu\|->download(" engine/*.c engine/*.h engine/ui/*.c template/*.c electron/src/*.ts 2>/dev/null \
  | grep -v "/build/" || true)
rest=$(echo "$hits" \
  | grep -v "engine/view\.c" \
  | grep -v "_test\.c" \
  | grep -v "vk_core\.c\|webgpu_core\.c\|gl_core\.c\|pix_core\.c\|vec_core\.c\|mesh_core\.c\|sdf_core\.c" \
  | grep -v "rhi_" \
  | grep -v "render_api\.h" || true)
if [ -n "$rest" ]; then
    echo "ABICHECK FAIL: cpu-путь вне фолбэка/тестов/ядер:"; echo "$rest"; fail=1
fi
# 2. noClear мёртв как код (комменты-история не в счёт).
if grep -rn -- "\.noClear\|->noClear\|noClear *=\|noClear;" engine/*.c engine/*.h engine/ui/*.c template/*.c electron/src/*.ts 2>/dev/null \
    | grep -v "/build/" | grep -q .; then
    echo "ABICHECK FAIL: noClear жив"; fail=1
fi
# 3. Версия едина: CMake (OUTPUT_NAME-строка) == corelib.
cv=$(awk '/OUTPUT_NAME engine/{for(i=1;i<=NF;i++) if($i=="VERSION") print $(i+1)}' engine/CMakeLists.txt | head -1)
cc=$(grep -o '"[0-9.]*"' engine/corelib.c | head -1 | tr -d '"')
if [ "$cv" != "$cc" ]; then
    echo "ABICHECK FAIL: версия $cv != $cc"; fail=1
fi
if [ "$fail" = 0 ]; then echo "ABICHECK OK (ABI $cv)"; fi
exit $fail
