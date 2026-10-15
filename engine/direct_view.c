// Прямой вьювер (схема Roblox/UE5): ОТДЕЛЬНЫЙ процесс, своё видимое окно,
// свой swapchain — GPU->GPU, ноль копий через IPC/canvas.
// НЕ внутри Electron-main (там GLFW-окно висит десятки секунд: чужой event-loop).
// Управление: drag — орбита, wheel — зум, Esc/close — выход.
// direct_view <backend gl|vk|webgpu> [domain voxel|sdf|vector|pixel|mesh]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "view.h"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    unsigned backend = 2, domain = 1;
    if (argc > 1) {
        if (!strcmp(argv[1], "gl")) backend = 2;
        else if (!strcmp(argv[1], "vk")) backend = 3;
        else if (!strcmp(argv[1], "webgpu")) backend = 1;
        else {
            printf("direct: backend gl|vk|webgpu\n");
            return 1;
        }
    }
    if (argc > 2) {
        if (!strcmp(argv[2], "voxel")) domain = 1;
        else if (!strcmp(argv[2], "sdf")) domain = 2;
        else if (!strcmp(argv[2], "vector")) domain = 4;
        else if (!strcmp(argv[2], "pixel")) domain = 8;
        else if (!strcmp(argv[2], "mesh")) domain = 16;
        else {
            printf("direct: domain voxel|sdf|vector|pixel|mesh\n");
            return 1;
        }
    }
    // Только GL умеет нативное окно сейчас (VK/WebGPU — таргеты есть, окна нет).
    if (backend != 2) {
        printf("direct: native window — только GL (vk/webgpu через canvas-трек)\n");
        return 1;
    }
    setenv("ENG_NATIVE_VIEW", "1", 1);
    EngView *vw = eng_view_open(domain, backend);
    if (!vw) {
        printf("direct: open FAIL\n");
        return 1;
    }
    // Readback-буфер не нужен окну (нативный путь present-only, ★2), но ABI
    // eng_view_frame его пока требует (размерная проверка): статика вне цикла.
    // Фаза 2 уберёт буфер из ABI полностью.
    static uint8_t sink[(size_t)1280 * 720 * 4];
    double t0 = now_s();
    int n = 0;
    for (;;) {
        if (!eng_view_frame(vw, sink, 1280, 720)) {
            printf("direct: frame FAIL\n");
            break;
        }
        if (eng_view_closed(vw)) break; // крестик — штатный выход
        n++;
        double dt = now_s() - t0;
        if (dt >= 2.0) {
            printf("direct: fps %.0f (процесс свой, копий 0)\n", n / dt);
            n = 0;
            t0 = now_s();
        }
        // Выход — по закрытию окна (внутри frame крутится glfwPollEvents).
    }
    eng_view_close(vw);
    return 0;
}
