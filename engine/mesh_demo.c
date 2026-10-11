// Меш-демо: вращающийся куб (36 вершин, нормали по граням) -> mesh-ядро.
// Вершины крутим на CPU каждый кадр (заодно гоняем upload).
// --frames N, VOX_CAM="yaw,pitch", VOX_SHOT.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"

RenderCore *rc_mesh_create(void);

// Грань: 4 угла CCW снаружи + нормаль + цвет. Порядок проверен правилом
// (v1-v0)x(v2-v0) = +нормаль (GL CCW front, cull back).
static void quad(float *v, int *n, const float c[4][3], const float nn[3],
                 const float col[3]) {
    int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        int q = idx[k];
        v[(*n) * 9 + 0] = c[q][0];
        v[(*n) * 9 + 1] = c[q][1];
        v[(*n) * 9 + 2] = c[q][2];
        v[(*n) * 9 + 3] = nn[0];
        v[(*n) * 9 + 4] = nn[1];
        v[(*n) * 9 + 5] = nn[2];
        v[(*n) * 9 + 6] = col[0];
        v[(*n) * 9 + 7] = col[1];
        v[(*n) * 9 + 8] = col[2];
        (*n)++;
    }
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(640, 360, "mesh demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_mesh_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "mesh init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u\n", rc->name, rc->caps, rc->backend);
    // Куб: грани +X -X +Y -Y +Z -Z.
    float base[36 * 9];
    int nb = 0;
    {
        float px[4][3] = {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}};
        float nx[4][3] = {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}};
        float py[4][3] = {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}};
        float ny[4][3] = {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}};
        float pz[4][3] = {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        float nz[4][3] = {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}};
        float cx[3] = {0.8f, 0.15f, 0.1f}, cy[3] = {1.0f, 0.8f, 0.1f};
        float cz[3] = {0.15f, 0.7f, 0.15f}, cw[3] = {0.3f, 0.3f, 0.32f};
        float czz[3] = {0.15f, 0.3f, 0.8f}, cnz[3] = {0.15f, 0.7f, 0.7f};
        float nxm[3] = {1, 0, 0}, nxp[3] = {-1, 0, 0};
        float nym[3] = {0, 1, 0}, nyn[3] = {0, -1, 0};
        float nzm[3] = {0, 0, 1}, nzn[3] = {0, 0, -1};
        quad(base, &nb, px, nxm, cx);
        quad(base, &nb, nx, nxp, cy);
        quad(base, &nb, py, nym, cz);
        quad(base, &nb, ny, nyn, cw);
        quad(base, &nb, pz, nzm, czz);
        quad(base, &nb, nz, nzn, cnz);
    }
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        // Поворот вокруг Y на CPU.
        float a = frame * 0.02f, ca = cosf(a), sa = sinf(a);
        static float rot[36 * 9];
        for (int i = 0; i < 36; i++) {
            float x = base[i * 9], y = base[i * 9 + 1], z = base[i * 9 + 2];
            float nx = base[i * 9 + 3], nz = base[i * 9 + 5];
            rot[i * 9] = x * ca + z * sa;
            rot[i * 9 + 1] = y;
            rot[i * 9 + 2] = -x * sa + z * ca;
            rot[i * 9 + 3] = nx * ca + nz * sa;
            rot[i * 9 + 4] = base[i * 9 + 4];
            rot[i * 9 + 5] = -nx * sa + nz * ca;
            rot[i * 9 + 6] = base[i * 9 + 6];
            rot[i * 9 + 7] = base[i * 9 + 7];
            rot[i * 9 + 8] = base[i * 9 + 8];
        }
        RcMesh m = {rot, 36};
        if (!rc->upload_mesh(rc, &m)) { fprintf(stderr, "upload fail\n"); return 1; }
        RcView v;
        memset(&v, 0, sizeof v);
        double t = frame * 0.004;
        v.camPos.x = (float)(6.0 * cos(t));
        v.camPos.y = 3.0f;
        v.camPos.z = (float)(6.0 * sin(t));
        float dx = -v.camPos.x, dy = -3.0f, dz = -v.camPos.z;
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
        v.sunDir.x = 0.5f; v.sunDir.y = 0.8f; v.sunDir.z = 0.3f;
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
