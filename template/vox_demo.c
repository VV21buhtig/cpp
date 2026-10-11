// Воксельное демо движка: статический мир 5x5 чанков (без стриминга —
// он игровой и переедет позже) + орбита камеры + webgpu-ядро на RHI.
// --frames N, VOX_CAM="yaw,pitch" фиксирует, VOX_DAYT=секунды, VOX_SHOT.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"
#include "sdf_ubo.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"

RenderCore *rc_webgpu_create(void);

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "vox demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_webgpu_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "webgpu init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u\n", rc->name, rc->caps, rc->backend);
    // Мир 9x9 статикой (стриминг игровой, переедет позже). Соседи не нужны (DDA).
    // Девайс встаёт на первом кадре — заливка с повтором до успеха.
    int upDone[9][9] = {{0}};
    int upLeft = 81;
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        if (upLeft > 0) {
            for (int cz = -4; cz <= 4; cz++)
                for (int cx = -4; cx <= 4; cx++) {
                    if (upDone[cz + 4][cx + 4]) continue;
                    VoxChunk c;
                    c.cx = cx;
                    c.cz = cz;
                    vox_gen(&c, 1337);
                    if (rc->upload_chunk(rc, cx, cz, c.id, 0)) {
                        upDone[cz + 4][cx + 4] = 1;
                        upLeft--;
                    }
                }
            rc->set_origin(rc, -4, -4);
        }
        RcView v;
        memset(&v, 0, sizeof v);
        Vec3 target = v3(8.0f, 26.0f, 8.0f);
        double t = frame * 0.004;
        Vec3 pos;
        // Орбита выше холмов (до 60): dist 80, pitch 0.5 -> y ~= 64.
        sdf_camera_orbit(target, (float)t, 0.5f, 80.0f, &pos);
        v.camPos = pos;
        Vec3 d = v3_sub(target, pos);
        float dl = v3_len(d);
        v.yaw = (float)atan2(d.z, d.x);
        v.pitch = (float)asin(d.y / dl);
        v.fov = 1.6f;
        const char *ce = getenv("VOX_CAM");
        if (ce && ce[0]) {
            double yw, pt;
            if (sscanf(ce, "%lf,%lf", &yw, &pt) == 2) { v.yaw = (float)yw; v.pitch = (float)pt; }
        }
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        v.resW = ww;
        v.resH = hh;
        v.maxSteps = 100.0f;
        v.gamma = 0.611f;   // как settings.cfg игры
        v.exposure = 0.508f;
        v.fog = 0.387f;
        v.shadowOn = 1.0f;
        const char *de = getenv("VOX_DAYT");
        v.dayT = (de && de[0]) ? (float)atof(de) : 0.0f;
        rc->frame(rc, &v);
        if (frame % 60 == 0) printf("f=%d fps=%.0f\n", frame, rc->fps(rc));
        frame++;
    }
    printf("done: %d frames\n", frame);
    rc->shutdown(rc);
    rc_destroy(rc);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
