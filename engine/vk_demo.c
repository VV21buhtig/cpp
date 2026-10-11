// VK-воксельное демо: статический мир 9x9 (стриминг игровой, позже) +
// настоящее соседство из массива + gl-ядро на RHI.
// --frames N, VOX_CAM="yaw,pitch", VOX_SHOT, VOX_NOCULL=1.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"

RenderCore *rc_vk_create(void);

#define GW 9

static VoxChunk world[GW][GW];
static int have[GW][GW];

static const VoxChunk *findc(int cx, int cz) {
    int ix = cx + GW / 2, iz = cz + GW / 2;
    if (ix < 0 || iz < 0 || ix >= GW || iz >= GW || !have[ix][iz]) return 0;
    return &world[ix][iz];
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan: без GL-контекста
    GLFWwindow *win = glfwCreateWindow(1280, 720, "vk vox demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_vk_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "vk init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u\n", rc->name, rc->caps, rc->backend);
    for (int cz = 0; cz < GW; cz++)
        for (int cx = 0; cx < GW; cx++) {
            int wx = cx - GW / 2, wz = cz - GW / 2;
            world[cx][cz].cx = wx;
            world[cx][cz].cz = wz;
            vox_gen(&world[cx][cz], 1337);
            have[cx][cz] = 1;
        }
    int upDone[GW][GW] = {{0}};
    int upLeft = GW * GW;
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        if (upLeft > 0) {
            int up = 0;
            for (int cz = 0; cz < GW && up < 12; cz++)
                for (int cx = 0; cx < GW && up < 12; cx++) {
                    if (upDone[cx][cz]) continue;
                    int wx = cx - GW / 2, wz = cz - GW / 2;
                    const VoxChunk *nb[6] = {
                        findc(wx - 1, wz), findc(wx + 1, wz), 0, 0,
                        findc(wx, wz - 1), findc(wx, wz + 1),
                    };
                    if (rc->upload_chunk(rc, wx, wz, world[cx][cz].id, nb)) {
                        upDone[cx][cz] = 1;
                        upLeft--;
                    }
                    up++;
                }
        }
        RcView v;
        memset(&v, 0, sizeof v);
        double t = frame * 0.004;
        float ox = (float)(90.0 * cos(t)), oz = (float)(90.0 * sin(t));
        v.camPos.x = 8 + ox; v.camPos.y = 66.0f; v.camPos.z = 8 + oz;
        float dx = -ox, dy = 26.0f - 66.0f, dz = -oz;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v.yaw = (float)atan2(dz, dx);
        v.pitch = (float)asin(dy / dl);
        v.fov = 1.6f;
        const char *ce = getenv("VOX_CAM");
        if (ce && ce[0]) {
            double yw, pt;
            if (sscanf(ce, "%lf,%lf", &yw, &pt) == 2) { v.yaw = (float)yw; v.pitch = (float)pt; }
        }
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        v.resW = ww; v.resH = hh;
        v.sunDir.x = 0.4f; v.sunDir.y = 0.75f; v.sunDir.z = 0.3f;
        v.time = (float)frame / 60.0f;
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
