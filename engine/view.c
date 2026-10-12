// Headless-вьювер: логика переехала из Qt-viewport'а слово в слово
// (камера, грейд, подача по доменам). Поведение НЕ менялось — владелец новый.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "view.h"
#include "render_api.h"
#include "rtarget.h"
#include "comp_gl.h"
#include "rhi_gl.h"
#include "scene.h"
#include "project.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"
#include "vox_feed.h"
#include "vec.h"
#include "mat4.h"

#define VW_GW 5 // мир 5x5 чанков (воксель)

struct EngView {
    void *win;
    RenderCore *rc;
    RcTarget target;
    int hasTarget;
    unsigned domain;
    int backend;
    int frame;
    int w, h; // текущий размер кадра (динамика через resize)
    uint8_t *stage; // RGBA-стейджинг чтения (w*h*4, realloc на resize)
    // воксель: мир + подача
    VoxChunk *world;
    int fed[VW_GW * VW_GW];
    int fedLeft, fedCur;
    // разовые заливки
    int sdfFed, vecFed, meshFed;
    // демо-сцена для JSON
    ScWorld scene;
    char *json;
    size_t jsonCap;
};

static const VoxChunk *vw_find(EngView *vw, int cx, int cz) {
    for (int i = 0; i < VW_GW * VW_GW; i++) {
        const VoxChunk *c = &vw->world[i];
        if (c->cx == cx && c->cz == cz) return c;
    }
    return 0;
}

// Подъём окна+ядра+мира под размер (вызывается из open и resize).
static int vw_boot(EngView *vw, int w, int h) {
    unsigned domain = vw->domain;
    int backend = vw->backend;
    if (backend == 2) {
        // CLIENT_API сбрасываем явно: хинты липкие, после NO_API-окна
        // следующее GL-окно родилось бы без контекста (та же ловушка была в Qt).
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(w, h, "eng-view", 0, 0);
    if (!win) return 0;
    vw->win = win;
    vw->rc = rc_create_by(domain, (unsigned)backend);
    if (!vw->rc || !vw->rc->init(vw->rc, win)) {
        if (vw->rc) rc_destroy(vw->rc);
        vw->rc = 0;
        glfwDestroyWindow(win);
        vw->win = 0;
        return 0;
    }
    vox_tiles_feed(vw->rc, 0); // атлас — фидер, не ядро
    if (domain == RC_CAP_VOXEL) {
        vw->world = (VoxChunk *)calloc((size_t)VW_GW * VW_GW, sizeof(VoxChunk));
        if (!vw->world) {
            vw->rc->shutdown(vw->rc);
            rc_destroy(vw->rc);
            vw->rc = 0;
            glfwDestroyWindow(win);
            vw->win = 0;
            return 0;
        }
        for (int cz = 0; cz < VW_GW; cz++)
            for (int cx = 0; cx < VW_GW; cx++) {
                VoxChunk *c = &vw->world[cz * VW_GW + cx];
                c->cx = cx - VW_GW / 2;
                c->cz = cz - VW_GW / 2;
                vox_gen(c, 1337);
            }
        vw->fedLeft = VW_GW * VW_GW;
    }
    if (backend == 2) {
        memset(&vw->target, 0, sizeof vw->target);
        if (!comp_target_gl_new(&vw->target, w, h)) {
            free(vw->world);
            vw->world = 0;
            vw->rc->shutdown(vw->rc);
            rc_destroy(vw->rc);
            vw->rc = 0;
            glfwDestroyWindow(win);
            vw->win = 0;
            return 0;
        }
        vw->hasTarget = 1;
    }
    vw->w = w;
    vw->h = h;
    // Стейджинг чтения RGBA (одно чтение + memcpy-флип, не 720 синхронов).
    free(vw->stage);
    vw->stage = (uint8_t *)malloc((size_t)w * h * 4);
    if (!vw->stage) return 0;
    return 1;
}
EngView *eng_view_open(unsigned domain, unsigned backend) {
    EngView *vw = (EngView *)calloc(1, sizeof *vw);
    if (!vw) return 0;
    vw->domain = domain;
    vw->backend = (int)backend;
    if (!glfwInit()) {
        free(vw);
        return 0;
    }
    if (!vw_boot(vw, VW_W, VW_H)) {
        free(vw);
        return 0;
    }
    // Демо-сцена (та же что в Qt-скелете): Scene/Hero/Sword.
    sc_init(&vw->scene);
    {
        Entity scene = sc_spawn(&vw->scene, "Scene");
        Entity hero = sc_spawn(&vw->scene, "Hero");
        Entity sword = sc_spawn(&vw->scene, "Sword");
        ScTransform t;
        memset(&t, 0, sizeof t);
        t.scale.x = t.scale.y = t.scale.z = 1.0f;
        t.pos.x = 10;
        t.pos.y = 20;
        t.pos.z = 30;
        if (hero) sc_set(&vw->scene, hero, &t);
        if (sword && scene) sc_parent(&vw->scene, sword, scene);
        (void)scene;
    }
    return vw;
}

// --- Подача по доменам (из viewport.cpp) ---
static void vw_feed_voxel(EngView *vw) {
    for (int k = 0; k < 12 && vw->fedLeft > 0; k++) {
        while (vw->fedCur < VW_GW * VW_GW && vw->fed[vw->fedCur]) vw->fedCur++;
        if (vw->fedCur >= VW_GW * VW_GW) {
            vw->fedLeft = 0;
            break;
        }
        int cx = vw->fedCur % VW_GW, cz = vw->fedCur / VW_GW;
        VoxChunk *c = &vw->world[cz * VW_GW + cx];
        int ok = 0;
        if (vw->backend == 1 && vw->rc->upload_chunk) {
            ok = vw->rc->upload_chunk(vw->rc, c->cx, c->cz, c->id, 0);
        } else {
            const VoxChunk *nb[6] = {
                vw_find(vw, c->cx - 1, c->cz), vw_find(vw, c->cx + 1, c->cz), 0, 0,
                vw_find(vw, c->cx, c->cz - 1), vw_find(vw, c->cx, c->cz + 1),
            };
            ok = vox_feed_one(vw->rc, c, nb) >= 0;
        }
        if (ok) {
            vw->fed[vw->fedCur] = 1;
            vw->fedLeft--;
        }
        vw->fedCur++;
    }
}

static void vw_feed_sdf(EngView *vw) {
    if (vw->sdfFed || !vw->rc->upload_sdf) return;
    RcSdfObj prims[4];
    memset(prims, 0, sizeof prims);
    prims[0].kind = 0;
    prims[0].p0.x = 8; prims[0].p0.y = 56; prims[0].p0.z = 8;
    prims[0].r = 3.0f; prims[0].mat = 0;
    prims[1].kind = 1;
    prims[1].p0.x = 20; prims[1].p0.y = 54; prims[1].p0.z = -6;
    prims[1].p1.x = 2.5f; prims[1].p1.y = 2.5f; prims[1].p1.z = 2.5f;
    prims[1].mat = 1;
    prims[2].kind = 2;
    prims[2].p0.x = -4; prims[2].p0.y = 55; prims[2].p0.z = 14;
    prims[2].p1.x = 1.0f;
    prims[2].r = 3.0f; prims[2].mat = 2;
    prims[3].kind = 4;
    prims[3].p0.x = -12; prims[3].p0.y = 58; prims[3].p0.z = 2;
    prims[3].p1.x = -6; prims[3].p1.y = 62; prims[3].p1.z = 2;
    prims[3].r = 0.8f; prims[3].mat = 3;
    if (vw->rc->upload_sdf(vw->rc, prims, 4)) vw->sdfFed = 1;
}

static void vw_feed_vec(EngView *vw) {
    if (vw->vecFed || !vw->rc->upload_vector) return;
    RcVecShape sh[4];
    memset(sh, 0, sizeof sh);
    sh[0].kind = 0;
    sh[0].a = 440; sh[0].b = 220; sh[0].c = 400; sh[0].d = 280;
    sh[0].rgba[0] = 0.9f; sh[0].rgba[1] = 0.9f; sh[0].rgba[2] = 0.9f; sh[0].rgba[3] = 1;
    sh[1].kind = 1;
    sh[1].a = 640; sh[1].b = 360; sh[1].c = 120;
    sh[1].rgba[0] = 1; sh[1].rgba[1] = 0.25f; sh[1].rgba[2] = 0.25f; sh[1].rgba[3] = 1;
    sh[2].kind = 2;
    sh[2].a = 100; sh[2].b = 600; sh[2].c = 1180; sh[2].d = 120;
    sh[2].rgba[0] = 0.25f; sh[2].rgba[1] = 1; sh[2].rgba[2] = 0.25f; sh[2].rgba[3] = 1;
    sh[3].kind = 0;
    sh[3].a = 100; sh[3].b = 620; sh[3].c = 300; sh[3].d = 40;
    sh[3].rgba[0] = 0.3f; sh[3].rgba[1] = 0.5f; sh[3].rgba[2] = 1; sh[3].rgba[3] = 1;
    if (vw->rc->upload_vector(vw->rc, sh, 4)) vw->vecFed = 1;
}

static void vw_feed_mesh(EngView *vw) {
    if (vw->meshFed || !vw->rc->upload_mesh) return;
    // Куб-суп 36 вершин x 9 float (pos3 nrm3 col3), как в scene3d.
    static float cube[36 * 9];
    static int made = 0;
    if (!made) {
        float C[6][3] = {{0.8f, 0.15f, 0.1f}, {1, 0.8f, 0.1f}, {0.15f, 0.7f, 0.15f},
                         {0.3f, 0.3f, 0.32f}, {0.15f, 0.3f, 0.8f}, {0.15f, 0.7f, 0.7f}};
        float NN[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        float P[6][4][3] = {{{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
                            {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
                            {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
                            {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
                            {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
                            {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {-1, 1, -1}}};
        int idx[6] = {0, 1, 2, 0, 2, 3}, n = 0;
        for (int f = 0; f < 6; f++)
            for (int k = 0; k < 6; k++) {
                int q = idx[k];
                cube[n * 9 + 0] = -24.0f + P[f][q][0] * 3.0f;
                cube[n * 9 + 1] = 56.0f + P[f][q][1] * 3.0f;
                cube[n * 9 + 2] = -16.0f + P[f][q][2] * 3.0f;
                cube[n * 9 + 3] = NN[f][0];
                cube[n * 9 + 4] = NN[f][1];
                cube[n * 9 + 5] = NN[f][2];
                cube[n * 9 + 6] = C[f][0];
                cube[n * 9 + 7] = C[f][1];
                cube[n * 9 + 8] = C[f][2];
                n++;
            }
        made = 1;
    }
    RcMesh m;
    m.verts = cube;
    m.nverts = 36;
    if (vw->rc->upload_mesh(vw->rc, &m)) vw->meshFed = 1;
}

static void vw_feed_pix(EngView *vw, uint8_t *rgba) {
    if (!vw->rc->upload_pixels || !rgba) return;
    // Живой тест-паттерн: полосы + бегущая строка (номер кадра видно).
    static const uint8_t bars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255},
                                       {0, 255, 0}, {255, 0, 255}, {255, 0, 0},
                                       {0, 0, 255}, {0, 0, 0}};
    int ly = (vw->frame * 4) % vw->h;
    for (int y = 0; y < vw->h; y++) {
        uint8_t *row = rgba + (size_t)y * vw->w * 4;
        for (int x = 0; x < vw->w; x++) {
            const uint8_t *c = bars[(x / (vw->w / 8)) % 8];
            row[x * 4 + 0] = c[0];
            row[x * 4 + 1] = c[1];
            row[x * 4 + 2] = c[2];
            row[x * 4 + 3] = 255;
            if (y >= ly && y < ly + 6) {
                row[x * 4 + 0] = 255;
                row[x * 4 + 1] = 255;
                row[x * 4 + 2] = 255;
            }
        }
    }
    vw->rc->upload_pixels(vw->rc, rgba, vw->w, vw->h);
}

// Камера: орбита + цели по доменам (из viewport.cpp).
static void vw_view(EngView *vw, RcView *v) {
    memset(v, 0, sizeof *v);
    float lookX = 8, lookY = 26.0f, lookZ = 8, orbitR = 90.0f, orbitY = 66.0f;
    if (vw->domain == RC_CAP_SDF) {
        lookY = 52.0f;
        orbitR = 60.0f;
        orbitY = 62.0f;
    } else if (vw->domain == RC_CAP_MESH) {
        lookX = -24.0f;
        lookY = 54.0f;
        lookZ = -16.0f;
        orbitR = 30.0f;
        orbitY = 60.0f;
    }
    double a = vw->frame * 0.004;
    v->camPos.x = lookX + orbitR * (float)cos(a);
    v->camPos.y = orbitY;
    v->camPos.z = lookZ + orbitR * (float)sin(a);
    {
        float dx = lookX - v->camPos.x, dy = lookY - v->camPos.y, dz = lookZ - v->camPos.z;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v->yaw = (float)atan2(dz, dx);
        v->pitch = (float)asin(dy / dl);
    }
    v->fov = 1.6f;
    v->resW = vw->w;
    v->resH = vw->h;
    v->sunDir.x = 0.4f;
    v->sunDir.y = 0.75f;
    v->sunDir.z = 0.3f;
    // Грейд DDA как в template/scene3d (иначе WebGPU в нулях).
    v->gamma = 0.611f;
    v->exposure = 0.508f;
    v->fog = 0.387f;
    v->shadowOn = 1.0f;
    v->maxSteps = 100.0f;
    v->time = (float)vw->frame / 60.0f;
    v->dayT = 242.0f; // полдень (верх дуги sdf_sun)
}

int eng_view_frame(EngView *vw, uint8_t *rgba, int w, int h) {
    if (!vw || !vw->rc || !rgba || w != vw->w || h != vw->h) return 0;
    if (vw->domain == RC_CAP_VOXEL) vw_feed_voxel(vw);
    else if (vw->domain == RC_CAP_SDF) vw_feed_sdf(vw);
    else if (vw->domain == RC_CAP_VECTOR) vw_feed_vec(vw);
    else if (vw->domain == RC_CAP_MESH) vw_feed_mesh(vw);
    else if (vw->domain == RC_CAP_PIXEL) vw_feed_pix(vw, rgba);
    RcView v;
    vw_view(vw, &v);
    int ok = 0;
    if (vw->backend == 2 && vw->hasTarget && vw->rc->frame_to) {
        vw->target.noClear = 0; // одиночка: каждый кадр с чисток
        if (vw->rc->frame_to(vw->rc, &v, &vw->target)) {
            // Цель -> RGBA верх первый: ОДНО чтение + memcpy-флип строк
            // (было 720 синхронных чтений + попиксельный свизл = ~100мс).
            glBindFramebuffer(GL_FRAMEBUFFER, vw->target.fbo);
            glReadPixels(0, 0, vw->w, vw->h, GL_RGBA, GL_UNSIGNED_BYTE, vw->stage);
            for (int y = 0; y < vw->h; y++)
                memcpy(rgba + (size_t)y * vw->w * 4,
                       vw->stage + (size_t)(vw->h - 1 - y) * vw->w * 4,
                       (size_t)vw->w * 4);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            ok = 1;
        }
    } else if (vw->rc->frame_cpu) {
        ok = vw->rc->frame_cpu(vw->rc, &v, rgba, vw->w, vw->h);
    } else {
        vw->rc->frame(vw->rc, &v);
    }
    vw->frame++;
    return ok;
}

const char *eng_view_scene(EngView *vw) {
    if (!vw) return "{\"entities\":[]}";
    char *js = eng_scene_json(&vw->scene);
    if (!js) return "{\"entities\":[]}";
    size_t n = strlen(js) + 1;
    if (n > vw->jsonCap) {
        char *nb = (char *)realloc(vw->json, n);
        if (!nb) {
            free(js);
            return "{\"entities\":[]}";
        }
        vw->json = nb;
        vw->jsonCap = n;
    }
    memcpy(vw->json, js, n);
    free(js);
    return vw->json;
}

int eng_view_frames(const EngView *vw) {
    return vw ? vw->frame : 0;
}

int eng_view_resize(EngView *vw, int w, int h) {
    if (!vw || !vw->rc) return 0;
    if (w < VW_MIN) w = VW_MIN;
    if (h < VW_MIN) h = VW_MIN;
    if (w > VW_MAX) w = VW_MAX;
    if (h > VW_MAX) h = VW_MAX;
    if (w == vw->w && h == vw->h) return 1;
    // Снос внутренностей (сцена, JSON и счётчик кадров живут дальше).
    vw->rc->shutdown(vw->rc);
    rc_destroy(vw->rc);
    vw->rc = 0;
    if (vw->hasTarget) {
        comp_target_gl_free(&vw->target);
        vw->hasTarget = 0;
    }
    if (vw->win) {
        glfwDestroyWindow((GLFWwindow *)vw->win);
        vw->win = 0;
    }
    free(vw->world);
    vw->world = 0;
    memset(vw->fed, 0, sizeof vw->fed);
    vw->fedLeft = VW_GW * VW_GW;
    vw->fedCur = 0;
    vw->sdfFed = vw->vecFed = vw->meshFed = 0;
    return vw_boot(vw, w, h);
}

void eng_view_close(EngView *vw) {
    if (!vw) return;
    if (vw->rc) {
        vw->rc->shutdown(vw->rc);
        rc_destroy(vw->rc);
    }
    if (vw->hasTarget) comp_target_gl_free(&vw->target);
    if (vw->win) glfwDestroyWindow((GLFWwindow *)vw->win);
    free(vw->world);
    free(vw->json);
    free(vw->stage);
    free(vw);
}
