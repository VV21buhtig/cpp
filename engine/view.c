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
#include "comp.h"
#include "rhi_gl.h"
#include "scene.h"
#include "project.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"
#include "vox_feed.h"
#include "vfs.h"
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
    // Орбита-камера: статична по умолчанию (автовращения нет — крутит юзер).
    // orbA — азимут, orbH — высота камеры, orbZoom-множитель — зум (0.35..3).
    float orbA, orbH, orbZoom;
    // Нативный вьюпорт-фолбэк (ENG_NATIVE_VIEW=1, только GL): движок рисует
    // в СВОЁ видимое окно рядом с Electron (как монолит), фронт — только меню.
    int native;
    // Drag-орбита нативного окна (свой процесс — колбэков наружу нет).
    double dragX, dragY;
    int dragOn;
    unsigned presentProg, presentVao;
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

static void vw_cube_at(float *cube, float cx, float cy, float cz, float h);

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
    // Фолбэк-монолит: видимое окно движка (позиция ENG_VIEW_POS="X,Y").
    vw->native = backend == 2 && getenv("ENG_NATIVE_VIEW") && getenv("ENG_NATIVE_VIEW")[0] &&
                 strcmp(getenv("ENG_NATIVE_VIEW"), "0");
    if (vw->native) {
        // Рядом с окном Electron (1600px): движок справа, фронт слева.
        const char *pos = getenv("ENG_VIEW_POS");
        int px = 1610, py = 30;
        if (pos) sscanf(pos, "%d,%d", &px, &py);
        glfwSetWindowPos(win, px, py);
        glfwShowWindow(win);
    }
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
        if (!eng_target_new(2, w, h, &vw->target)) {
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
    // Камера по умолчанию: статичный ракурс (автовращения нет).
    // orbA подобран под куб (видно 3 грани), высота/зум — нейтраль.
    vw->orbA = -2.2f;
    vw->orbH = 60.0f;
    vw->orbZoom = 1.0f;
    // Презент-программа нативного окна (только native-режим, пути через VFS).
    if (vw->native) {
        char vs[256], fs[256];
        if (!vfs_resolve("shaders/blit/blit.vert", vs, sizeof vs) ||
            !vfs_resolve("shaders/blit/blit.frag", fs, sizeof fs))
            return 0;
        vw->presentProg = rhi_gl_prog(vs, fs);
        vw->presentVao = rhi_gl_empty_vao();
        if (!vw->presentProg || !vw->presentVao) return 0;
    }
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
        // Демо-меш Hero (тот же куб что рисует feed): для eng_view_pick.
        // Мировые координаты (трансформ сущностей меш-подача v1 игнорит).
        if (hero) {
            static float hc[36 * 9];
            static int hcMade = 0;
            if (!hcMade) {
                vw_cube_at(hc, -24.0f, 56.0f, -16.0f, 3.0f);
                hcMade = 1;
            }
            sc_mesh_set(&vw->scene, hero, hc, 36);
        }
        // Демо-окклюдер Sword (Этап Б-пик): тот же центр, 1.5x — concentric
        // куб строго содержит Hero, nearer по любому лучу через центр.
        if (sword) {
            static float sc2[36 * 9];
            static int scMade = 0;
            if (!scMade) {
                vw_cube_at(sc2, -24.0f, 56.0f, -16.0f, 4.5f);
                scMade = 1;
            }
            sc_mesh_set(&vw->scene, sword, sc2, 36);
        }
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

static void vw_cube_at(float *cube, float cx, float cy, float cz, float h) {
    // Куб-суп 36 вершин x 9 float (pos3 nrm3 col3), как в scene3d.
    float C[6][3] = {{0.8f, 0.15f, 0.1f}, {1, 0.8f, 0.1f}, {0.15f, 0.7f, 0.15f},
                     {0.3f, 0.3f, 0.32f}, {0.15f, 0.3f, 0.8f}, {0.15f, 0.7f, 0.7f}};
    float NN[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    float P[6][4][3] = {{{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
                        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
                        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
                        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
                        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
                        {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}};
    int idx[6] = {0, 1, 2, 0, 2, 3}, n = 0;
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 6; k++) {
            int q = idx[k];
            cube[n * 9 + 0] = cx + P[f][q][0] * h;
            cube[n * 9 + 1] = cy + P[f][q][1] * h;
            cube[n * 9 + 2] = cz + P[f][q][2] * h;
            cube[n * 9 + 3] = NN[f][0];
            cube[n * 9 + 4] = NN[f][1];
            cube[n * 9 + 5] = NN[f][2];
            cube[n * 9 + 6] = C[f][0];
            cube[n * 9 + 7] = C[f][1];
            cube[n * 9 + 8] = C[f][2];
            n++;
        }
}

static void vw_feed_mesh(EngView *vw) {
    if (vw->meshFed || !vw->rc->upload_mesh) return;
    // Куб-суп 36 вершин x 9 float (pos3 nrm3 col3), как в scene3d.
    static float cube[36 * 9];
    static int made = 0;
    if (!made) {
        vw_cube_at(cube, -24.0f, 56.0f, -16.0f, 3.0f);
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
    double a = (double)vw->orbA; // статика: кадр на угол не влияет (крутит юзер)
    float rEff = orbitR * vw->orbZoom;
    v->camPos.x = lookX + rEff * (float)cos(a);
    v->camPos.y = vw->orbH;
    v->camPos.z = lookZ + rEff * (float)sin(a);
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
        eng_pass_begin(&vw->target, 0.05f, 0.07f, 0.12f, 1.0f, 1.0f, 1);
        if (vw->rc->frame_to(vw->rc, &v, &vw->target)) {
            // Нативный монолит: презент БЕЗ readback (★2: никакого glReadPixels
            // в sink — кадр не покидает GPU вообще).
            if (vw->native && vw->presentProg && vw->presentVao) {
                GLFWwindow *nwin = (GLFWwindow *)vw->win;
                // Drag тянет камеру (клик без протяжки ничего не делает —
                // пикинг живёт во фронте, тут только орбита).
                if (glfwGetMouseButton(nwin, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                    double cx, cy;
                    glfwGetCursorPos(nwin, &cx, &cy);
                    if (vw->dragOn)
                        eng_view_orbit(vw, (float)(-(cx - vw->dragX) * 0.008),
                                       (float)(-(cy - vw->dragY) * 0.15), 0.0f);
                    vw->dragX = cx;
                    vw->dragY = cy;
                    vw->dragOn = 1;
                } else {
                    vw->dragOn = 0;
                }
                comp_present_gl(vw->presentProg, vw->presentVao, &vw->target, vw->w, vw->h);
                glfwSwapBuffers(nwin);
                glfwPollEvents();
                ok = 1;
            } else {
                // Canvas-тракт: ОДНО чтение + memcpy-флип строк
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
        }
    } else if (vw->rc->frame_cpu) {
        ok = vw->rc->frame_cpu(vw->rc, &v, rgba, vw->w, vw->h);
    } else {
        vw->rc->frame(vw->rc, &v);
    }
    eng_pass_end(&vw->target); // чистка потреблена (даже мимо веток — флаг не висит)
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

const char *eng_entity_get_json(EngView *vw, unsigned ent) {
    if (!vw || !sc_alive(&vw->scene, (Entity)ent)) return 0;
    char *js = eng_scene_entity_json(&vw->scene, (Entity)ent);
    if (!js) return 0;
    size_t n = strlen(js) + 1;
    if (n > vw->jsonCap) {
        char *nb = (char *)realloc(vw->json, n);
        if (!nb) {
            free(js);
            return 0;
        }
        vw->json = nb;
        vw->jsonCap = n;
    }
    memcpy(vw->json, js, n);
    free(js);
    return vw->json;
}

int eng_entity_set_json(EngView *vw, unsigned ent, const char *js) {
    if (!vw || !js || !sc_alive(&vw->scene, (Entity)ent)) return 0;
    return eng_scene_entity_parse(&vw->scene, (Entity)ent, js);
}

int eng_view_frames(const EngView *vw) {
    return vw ? vw->frame : 0;
}

unsigned eng_view_pick(EngView *vw, int x, int y) {
    if (!vw || !vw->rc || !vw->rc->pick) return 0;
    if (vw->domain != RC_CAP_MESH || vw->backend != 2) return 0;
    if (x < 0 || y < 0 || x >= vw->w || y >= vw->h) return 0;
    if (!vw->rc->pick_begin) return 0; // без однопрохода окклюзии нет (Б-пик)
    // Цель пикинга на один клик (создать-использовать-снести: клики редки).
    RcTarget pt;
    memset(&pt, 0, sizeof pt);
    if (!eng_target_new(2, vw->w, vw->h, &pt)) return 0;
    RcView v;
    vw_view(vw, &v);
    Entity list[1024];
    int n = sc_list(&vw->scene, list, 1024);
    // Один проход: все супы подряд в одну цель, nearer выигрывает глубиной
    // (порядок списка НЕ решает — Sword позже Hero, но nearer и побеждает).
    // Невидимые не кликаются.
    eng_pass_begin(&pt, 0, 0, 0, 1, 1.0f, 0); // чистку делает pick_begin
    if (!vw->rc->pick_begin(vw->rc, &v, &pt)) {
        eng_target_free(&pt);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (!sc_visible(&vw->scene, list[i])) continue;
        int mn = 0;
        const float *mv = sc_mesh_get(&vw->scene, list[i], &mn);
        if (!mv || mn <= 0) continue;
        RcMesh m;
        m.verts = mv;
        m.nverts = mn;
        if (!vw->rc->upload_mesh(vw->rc, &m)) continue;
        if (!vw->rc->pick(vw->rc, &v, &pt, (unsigned)list[i])) continue;
    }
    eng_pass_end(&pt);
    unsigned hit = 0;
    {
        uint8_t px[3] = {0};
        glBindFramebuffer(GL_FRAMEBUFFER, pt.fbo);
        glReadPixels(x, vw->h - 1 - y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        unsigned id = (unsigned)px[0] + ((unsigned)px[1] << 8) + ((unsigned)px[2] << 16);
        if (id) hit = id - 1;
    }
    // Вернуть меш демо-кадра (пик перетёр суп кадра).
    vw->meshFed = 0;
    eng_target_free(&pt);
    return hit;
}

int eng_view_shader(EngView *vw, const char *id, const char *code, char *log, int logcap) {
    if (log && logcap > 0) log[0] = 0;
    // code NULL = из файлов (WG Этап 3.1); id обязателен всегда.
    if (!vw || !vw->rc || !id) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "нет вида/ядра/id");
        return 0;
    }
    if (!code && vw->backend != RC_BACKEND_WEBGPU) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "нет кода (только WG умеет из файлов)");
        return 0;
    }
    if (!vw->rc->shader_reload) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "ядро не умеет (backend %d)", vw->backend);
        return 0;
    }
    return vw->rc->shader_reload(vw->rc, id, code, log, logcap);
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
        eng_target_free(&vw->target);
        vw->hasTarget = 0;
    }
    // Презент-программа привязана к контексту окна: снести до него.
    if (vw->presentProg) glDeleteProgram(vw->presentProg);
    if (vw->presentVao) glDeleteVertexArrays(1, &vw->presentVao);
    vw->presentProg = vw->presentVao = 0;
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
    // Камера переживает ресайз (иначе каждый ресайз щёлкал бы ракурсом).
    float oA = vw->orbA, oH = vw->orbH, oZ = vw->orbZoom;
    if (!vw_boot(vw, w, h)) return 0;
    vw->orbA = oA;
    vw->orbH = oH;
    vw->orbZoom = oZ;
    return 1;
}

// Орбита-камера (drag/zoom из клиента): dA — азимут, dH — высота,
// dZ — множитель зума (>1 дальше). Клаampы редакторные, не физические.
void eng_view_orbit(EngView *vw, float dA, float dH, float dZ) {
    if (!vw) return;
    vw->orbA += dA;
    vw->orbH += dH;
    if (vw->orbH < 8.0f) vw->orbH = 8.0f;
    if (vw->orbH > 160.0f) vw->orbH = 160.0f;
    if (dZ != 0.0f) {
        vw->orbZoom *= dZ;
        if (vw->orbZoom < 0.35f) vw->orbZoom = 0.35f;
        if (vw->orbZoom > 3.0f) vw->orbZoom = 3.0f;
    }
}

// Нативное окно закрыли крестиком (1 да). Для direct-процесса (свой цикл).
int eng_view_closed(EngView *vw) {
    if (!vw || !vw->win) return 1;
    if (!vw->native) return 0;
    return glfwWindowShouldClose((GLFWwindow *)vw->win);
}

void eng_view_close(EngView *vw) {
    if (!vw) return;
    if (vw->rc) {
        vw->rc->shutdown(vw->rc);
        rc_destroy(vw->rc);
    }
    if (vw->hasTarget) eng_target_free(&vw->target);
    // Презент-программа умирает раньше контекста (окно ещё живо).
    if (vw->presentProg) glDeleteProgram(vw->presentProg);
    if (vw->presentVao) glDeleteVertexArrays(1, &vw->presentVao);
    vw->presentProg = vw->presentVao = 0;
    if (vw->win) glfwDestroyWindow((GLFWwindow *)vw->win);
    free(vw->world);
    free(vw->json);
    free(vw->stage);
    free(vw);
}
