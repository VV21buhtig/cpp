// Общая 3D-сцена движка: ОДНА на всех (вместо демок под каждое ядро).
// Аспекты: воксели 9x9 (террейн), SDF-примитивы x2.5 в небе (+50y),
// куб-меш x3 рядом. Каждое ядро рисует свой аспект с общей камеры.
// --core gl|vk|webgpu|sdf|mesh, --frames N, VOX_CAM, VOX_SHOT, VOX_DAYT.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"
#include "vox_feed.h"

#define GW 9

static VoxChunk world[GW][GW];

static const VoxChunk *findc(int cx, int cz) {
    int ix = cx + GW / 2, iz = cz + GW / 2;
    if (ix < 0 || iz < 0 || ix >= GW || iz >= GW) return 0;
    return &world[ix][iz];
}

static void obj(RcSdfObj *o, int kind, float x0, float y0, float z0,
                float x1, float y1, float z1, float r, int mat) {
    o->kind = kind;
    o->p0.x = x0; o->p0.y = y0; o->p0.z = z0;
    o->p1.x = x1; o->p1.y = y1; o->p1.z = z1;
    o->r = r;
    o->mat = mat;
}

// Куб-суп 36 вершин (грани CCW снаружи). half = полуразмер.
static int cube_soup(float *v, float cx, float cy, float cz, float half) {
    float C[6][3] = {
        {0.8f, 0.15f, 0.1f}, {1.0f, 0.8f, 0.1f}, {0.15f, 0.7f, 0.15f},
        {0.3f, 0.3f, 0.32f}, {0.15f, 0.3f, 0.8f}, {0.15f, 0.7f, 0.7f},
    };
    float NN[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    float h = half;
    float P[6][4][3] = {
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
        {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
    };
    int n = 0, idx[6] = {0, 1, 2, 0, 2, 3};
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 6; k++) {
            int q = idx[k];
            v[n * 9 + 0] = cx + P[f][q][0] * h;
            v[n * 9 + 1] = cy + P[f][q][1] * h;
            v[n * 9 + 2] = cz + P[f][q][2] * h;
            v[n * 9 + 3] = NN[f][0];
            v[n * 9 + 4] = NN[f][1];
            v[n * 9 + 5] = NN[f][2];
            v[n * 9 + 6] = C[f][0];
            v[n * 9 + 7] = C[f][1];
            v[n * 9 + 8] = C[f][2];
            n++;
        }
    return n;
}

int main(int argc, char **argv) {
    const char *coreName = "gl";
    int maxFrames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--core") && i + 1 < argc) coreName = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    }
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    RenderCore *rc = rc_create(coreName);
    if (!rc) { fprintf(stderr, "no core '%s'\n", coreName); return 1; }
    if (rc->backend == RC_BACKEND_GL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    GLFWwindow *win = glfwCreateWindow(1280, 720, "scene3d", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    if (!rc->init(rc, win)) { fprintf(stderr, "core init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u\n", rc->name, rc->caps, rc->backend);
    vox_tiles_feed(rc, 0); // атлас — фидер, не ядро (нет API/файлов — белая заглушка)
    // Воксели: статика 9x9.
    for (int cz = 0; cz < GW; cz++)
        for (int cx = 0; cx < GW; cx++) {
            world[cx][cz].cx = cx - GW / 2;
            world[cx][cz].cz = cz - GW / 2;
            vox_gen(&world[cx][cz], 1337);
        }
    // SDF-витрина x2.5 на высоте (над террейном): сфера, короб, тор.
    RcSdfObj prims[4];
    obj(&prims[0], 0, 8, 56, 8, 0, 0, 0, 3.0f, 0);
    obj(&prims[1], 1, 20, 54, -6, 2.5f, 2.5f, 2.5f, 0, 1);
    obj(&prims[2], 2, -4, 55, 14, 1.0f, 0, 0, 3.0f, 2);
    obj(&prims[3], 4, -12, 58, 2, -6, 62, 2, 0.8f, 3);
    // Куб-меш x3 рядом с витриной.
    static float cube[36 * 9];
    int cubeN = cube_soup(cube, -24.0f, 56.0f, -16.0f, 3.0f);
    if (rc->caps & RC_CAP_SDF) {
        if (!rc->upload_sdf(rc, prims, 4)) { fprintf(stderr, "sdf upload fail\n"); return 1; }
    }
    if (rc->caps & RC_CAP_MESH) {
        RcMesh m = {cube, cubeN};
        if (!rc->upload_mesh(rc, &m)) { fprintf(stderr, "mesh upload fail\n"); return 1; }
    }
    if ((rc->caps & RC_CAP_VOXEL) && rc->backend == RC_BACKEND_WEBGPU && rc->set_origin)
        rc->set_origin(rc, -4, -4);
    int upDone[GW][GW] = {{0}};
    int upLeft = GW * GW, cur = 0;
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        // Воксельный аспект: DDA ест сырьё, растр — через фидер.
        if ((rc->caps & RC_CAP_VOXEL) && upLeft > 0) {
            if (rc->backend == RC_BACKEND_WEBGPU && rc->upload_chunk) {
                for (int k = 0; k < 12 && upLeft > 0; k++) {
                    while (cur < GW * GW && upDone[cur % GW][cur / GW]) cur++;
                    if (cur >= GW * GW) { upLeft = 0; break; }
                    int cx = cur % GW, cz = cur / GW;
                    if (rc->upload_chunk(rc, world[cx][cz].cx, world[cx][cz].cz,
                                         world[cx][cz].id, 0)) {
                        upDone[cx][cz] = 1;
                        upLeft--;
                    }
                    cur++;
                }
            } else if (rc->upload_voxmesh) {
                for (int k = 0; k < 12 && upLeft > 0; k++) {
                    while (cur < GW * GW && upDone[cur % GW][cur / GW]) cur++;
                    if (cur >= GW * GW) { upLeft = 0; break; }
                    int cx = cur % GW, cz = cur / GW;
                    int wx = world[cx][cz].cx, wz = world[cx][cz].cz;
                    const VoxChunk *nb[6] = {
                        findc(wx - 1, wz), findc(wx + 1, wz), 0, 0,
                        findc(wx, wz - 1), findc(wx, wz + 1),
                    };
                    if (vox_feed_one(rc, &world[cx][cz], nb) >= 0) {
                        upDone[cx][cz] = 1;
                        upLeft--;
                    }
                    cur++;
                }
            } else {
                upLeft = 0; // воксельный вход нечем кормить
            }
        }
        RcView v;
        memset(&v, 0, sizeof v);
        // Орбита видит и террейн, и витрину: r=120, y=90, цель (8,42,8).
        double t = frame * 0.004;
        v.camPos.x = 8 + (float)(120.0 * cos(t));
        v.camPos.y = 90.0f;
        v.camPos.z = 8 + (float)(120.0 * sin(t));
        {
            float dx = 8 - v.camPos.x, dy = 42 - 90.0f, dz = 8 - v.camPos.z;
            float dl = sqrtf(dx * dx + dy * dy + dz * dz);
            v.yaw = (float)atan2(dz, dx);
            v.pitch = (float)asin(dy / dl);
        }
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
        v.maxSteps = 100.0f;
        v.gamma = 0.611f; v.exposure = 0.508f; v.fog = 0.387f;
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
