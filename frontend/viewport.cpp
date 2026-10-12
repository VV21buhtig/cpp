// Вьюпорт: движок в скрытом окне, кадры в QImage. C++ без Q_OBJECT.
// Порядок важен: glad ПЕРВЫМ, иначе Qt притащит системный gl.h.
#include <glad/gl.h>
#include "viewport.h"
#include <QPainter>
#include <QPaintEvent>
#include <QRgb>
#include <GLFW/glfw3.h>
#include "render_api.h"
#include "rtarget.h"
#include "comp_gl.h"
#include "rhi_gl.h"
#include "mat4.h"
#include "scene.h"
#include "panels.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"
#include "vox_feed.h"
#include <QMouseEvent>

Viewport::Viewport(QWidget *parent) : QWidget(parent) {
    setMinimumSize(480, 270);
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, [this]() { tick(); });
}

Viewport::~Viewport() {
    setPlaying(false);
    if (rc) {
        rc->shutdown(rc);
        rc_destroy(rc);
        rc = nullptr;
    }
    if (vrc) {
        vrc->shutdown(vrc);
        rc_destroy(vrc);
        vrc = nullptr;
    }
    if (glfwWin) {
        glfwDestroyWindow((GLFWwindow *)glfwWin);
        glfwWin = nullptr;
    }
    if (target) {
        comp_target_gl_free(target);
        // presentProg/Vao переживают (дешёвые, общие для GL-сессий).
    }
    if (cpuBuf) {
        free(cpuBuf);
        cpuBuf = nullptr;
    }
    if (world) {
        free(world);
        world = nullptr;
    }
    // Цель и окно переживут? Окно скрытое — отдаём движку? Нет: окно наше.
    // Упрощение скелета: окно и цель живут до конца процесса (утечка
    // пары дескрипторов на выход — честно зафиксировано, не скрыто).
}

bool Viewport::init(int be) {
    return initDomain(RC_CAP_VOXEL, be);
}

bool Viewport::initDomain(unsigned dom, int be) {
    // Транзакция: члены коммитим только при успехе. Иначе после FAILED
    // врали бы чип, кнопки и тайтл (бэкенд новый, ядро старое).
    if (!glfwInit()) return false;
    // Хинты липкие между окнами: CLIENT_API сбрасываем явно, иначе после
    // NO_API-окна (VK/WebGPU) следующее GL-окно родится без контекста.
    if (be == 2) { // GL: контекст 4.5
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else { // WebGPU/Vulkan: без API (поверхность создаст RHI ядра)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "ed-viewport", 0, 0);
    if (!win) return false;
    // Ядро по таблице «домен x бэкенд» (домен из шаблона, бэкенд из хаба).
    // Нет ячейки (напр. SDF под Vulkan) — честный false, редактор скажет.
    struct RenderCore *newRc = rc_create_by(dom, (unsigned)be);
    if (!newRc || !newRc->init(newRc, win)) {
        if (newRc) rc_destroy(newRc);
        glfwDestroyWindow(win);
        return false;
    }
    vox_tiles_feed(newRc, 0); // атлас — фидер, не ядро
    struct RenderCore *newVrc = nullptr;
    // Vec-ядро для гизмо-оверлея делит окно/контекст (любой GL-домен:
    // оверлей рисует поверх готовой цели, домен ему без разницы).
    if (be == 2) {
        newVrc = rc_create_by(RC_CAP_VECTOR, 2u);
        if (newVrc && !newVrc->init(newVrc, win)) {
            rc_destroy(newVrc);
            newVrc = nullptr;
        }
    }
    void *newWorld = nullptr;
    // Воксельный мир — только воксельному домену.
    if (dom == RC_CAP_VOXEL) {
        newWorld = calloc(GW * GW, sizeof(VoxChunk));
        if (!newWorld) {
            newRc->shutdown(newRc);
            rc_destroy(newRc);
            if (newVrc) {
                newVrc->shutdown(newVrc);
                rc_destroy(newVrc);
            }
            glfwDestroyWindow(win);
            return false;
        }
        for (int cz = 0; cz < GW; cz++)
            for (int cx = 0; cx < GW; cx++) {
                VoxChunk *c = &((VoxChunk *)newWorld)[cz * GW + cx];
                c->cx = cx - GW / 2;
                c->cz = cz - GW / 2;
                vox_gen(c, 1337);
            }
    }
    RcTarget *newTarget = (RcTarget *)calloc(1, sizeof(RcTarget));
    // Цель и показ — только под GL (frame_to есть только там).
    if (be == 2) {
        if (!newTarget || !comp_target_gl_new(newTarget, 1280, 720)) {
            free(newTarget);
            free(newWorld);
            newRc->shutdown(newRc);
            rc_destroy(newRc);
            if (newVrc) {
                newVrc->shutdown(newVrc);
                rc_destroy(newVrc);
            }
            glfwDestroyWindow(win);
            return false;
        }
        presentProg = rhi_gl_prog("engine/shaders/blit/blit.vert",
                                  "engine/shaders/blit/blit.frag");
        presentVao = rhi_gl_empty_vao();
    }
    // Успех: коммит. Старое окно/ядра уже снесены вызывателем.
    glfwWin = win;
    rc = newRc;
    vrc = newVrc;
    world = newWorld;
    target = newTarget;
    backend = be;
    domain = dom;
    sdfFed = vecFed = meshFed = 0;
    img = QImage(1280, 720, QImage::Format_RGB888);
    img.fill(Qt::black);
    if (!cpuBuf) cpuBuf = (uint8_t *)calloc((size_t)1280 * 720, 4); // разово
    if (!cpuBuf) return false;
    return true;
}

void Viewport::setPlaying(bool on) {
    playing = on;
    if (on) timer->start(16);
    else timer->stop();
}

bool Viewport::switchBackend(int be) {
    if (be == backend) return true;
    setPlaying(false);
    if (rc) {
        rc->shutdown(rc);
        rc_destroy(rc);
        rc = nullptr;
    }
    if (vrc) {
        vrc->shutdown(vrc);
        rc_destroy(vrc);
        vrc = nullptr;
    }
    if (glfwWin) {
        glfwDestroyWindow((GLFWwindow *)glfwWin);
        glfwWin = nullptr;
    }
    if (target) {
        comp_target_gl_free(target);
        // presentProg/Vao переживают (дешёвые, общие для GL-сессий).
    }
    // Мир пересоздаём (initDomain заливает заново под домен).
    if (world) {
        free(world);
        world = nullptr;
    }
    // fed-флаги сбрасываем — перезальём.
    for (int i = 0; i < GW * GW; i++) fed[i] = 0;
    fedLeft = GW * GW;
    fedCur = 0;
    sdfFed = vecFed = meshFed = 0;
    havePV = 0;
    dragAxis = -1;
    if (!initDomain(domain, be)) return false;
    return true;
}

static const VoxChunk *vfind(void *world, int cx, int cz) {    VoxChunk *w = (VoxChunk *)world;
    const int GW = 5;
    int ix = cx + GW / 2, iz = cz + GW / 2;
    if (ix < 0 || iz < 0 || ix >= GW || iz >= GW) return 0;
    return &w[iz * GW + ix];
}

// Проекция мировой точки в пиксели кадра 1280x720 (GL Y-вверх -> вниз).
// Возвращает 1, wout[0..1] = пиксели. w<=0 (за камерой) -> 0.
static int project_pt(const float *pv, float x, float y, float z, float *ox, float *oy) {
    float cx = pv[0] * x + pv[4] * y + pv[8] * z + pv[12];
    float cy = pv[1] * x + pv[5] * y + pv[9] * z + pv[13];
    float cw = pv[3] * x + pv[7] * y + pv[11] * z + pv[15];
    if (cw <= 0.0f) return 0;
    float nx = cx / cw, ny = cy / cw;
    *ox = (nx * 0.5f + 0.5f) * 1280.0f;
    *oy = (1.0f - (ny * 0.5f + 0.5f)) * 720.0f;
    return 1;
}

// Дистанция точка-отрезок в пикселях (пикинг осей).
static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 1e-9f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float qx = ax + dx * t - px, qy = ay + dy * t - py;
    return sqrtf(qx * qx + qy * qy);
}

void Viewport::renderOnce() {
    if (!rc) {
        placeheld = 1; // ядро снесли (FAILED-пересадка): честная заглушка
        return;
    }
    int isGL = (backend == 2);
    // Догрузка мира по 12 чанков за тик — только вокселю.
    // DDA (webgpu) ест сырьё, растр (gl/vk) — через фидер движка.
    if (domain == RC_CAP_VOXEL) {
    for (int k = 0; k < 12 && fedLeft > 0; k++) {
        while (fedCur < GW * GW && fed[fedCur]) fedCur++;
        if (fedCur >= GW * GW) { fedLeft = 0; break; }
        int cx = fedCur % GW, cz = fedCur / GW;
        VoxChunk *c = &((VoxChunk *)world)[cz * GW + cx];
        int ok = 0;
        if (backend == 1 && rc->upload_chunk) {
            ok = rc->upload_chunk(rc, c->cx, c->cz, c->id, 0);
        } else {
            const VoxChunk *nb[6] = {
                vfind(world, c->cx - 1, c->cz), vfind(world, c->cx + 1, c->cz), 0, 0,
                vfind(world, c->cx, c->cz - 1), vfind(world, c->cx, c->cz + 1),
            };
            ok = vox_feed_one(rc, c, nb) >= 0;
        }
        if (ok) { fed[fedCur] = 1; fedLeft--; }
        fedCur++;
    }
    } // domain == VOXEL
    // Не-воксель: разовая заливка демо-сцены шаблона (та же что в scene3d).
    if (domain == RC_CAP_SDF && !sdfFed && rc->upload_sdf) {
        RcSdfObj prims[4];
        memset(prims, 0, sizeof prims);
        prims[0].kind = 0; // сфера
        prims[0].p0.x = 8; prims[0].p0.y = 56; prims[0].p0.z = 8;
        prims[0].r = 3.0f; prims[0].mat = 0;
        prims[1].kind = 1; // короб
        prims[1].p0.x = 20; prims[1].p0.y = 54; prims[1].p0.z = -6;
        prims[1].p1.x = 2.5f; prims[1].p1.y = 2.5f; prims[1].p1.z = 2.5f;
        prims[1].mat = 1;
        prims[2].kind = 2; // тор
        prims[2].p0.x = -4; prims[2].p0.y = 55; prims[2].p0.z = 14;
        prims[2].p1.x = 1.0f;
        prims[2].r = 3.0f; prims[2].mat = 2;
        prims[3].kind = 4; // капсула
        prims[3].p0.x = -12; prims[3].p0.y = 58; prims[3].p0.z = 2;
        prims[3].p1.x = -6; prims[3].p1.y = 62; prims[3].p1.z = 2;
        prims[3].r = 0.8f; prims[3].mat = 3;
        if (rc->upload_sdf(rc, prims, 4)) sdfFed = 1;
    }
    if (domain == RC_CAP_VECTOR && !vecFed && rc->upload_vector) {
        RcVecShape sh[4];
        memset(sh, 0, sizeof sh);
        float white[4] = {0.9f, 0.9f, 0.9f, 1};
        float red[4] = {1, 0.25f, 0.25f, 1};
        float green[4] = {0.25f, 1, 0.25f, 1};
        float blue[4] = {0.3f, 0.5f, 1, 1};
        sh[0].kind = 0; // рамка: x,y,w,h
        sh[0].a = 440; sh[0].b = 220; sh[0].c = 400; sh[0].d = 280;
        memcpy(sh[0].rgba, white, sizeof white);
        sh[1].kind = 1; // круг: cx,cy,r
        sh[1].a = 640; sh[1].b = 360; sh[1].c = 120;
        memcpy(sh[1].rgba, red, sizeof red);
        sh[2].kind = 2; // диагональ: x0,y0,x1,y1
        sh[2].a = 100; sh[2].b = 600; sh[2].c = 1180; sh[2].d = 120;
        memcpy(sh[2].rgba, green, sizeof green);
        sh[3].kind = 0; // плашка снизу
        sh[3].a = 100; sh[3].b = 620; sh[3].c = 300; sh[3].d = 40;
        memcpy(sh[3].rgba, blue, sizeof blue);
        if (rc->upload_vector(rc, sh, 4)) vecFed = 1;
    }
    if (domain == RC_CAP_MESH && !meshFed && rc->upload_mesh) {
        // Куб-суп 36 вершин x 9 float (pos3 norm3 col3), как в scene3d.
        static float cube[36 * 9];
        static int cubeMade = 0;
        if (!cubeMade) {
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
            cubeMade = 1;
        }
        RcMesh m;
        m.verts = cube;
        m.nverts = 36;
        if (rc->upload_mesh(rc, &m)) meshFed = 1;
    }
    // Пиксель: живой тест-паттерн каждый кадр (полосы + бегущая строка).
    // cpuBuf тут свободен (frame_cpu у пикселя не используется).
    if (domain == RC_CAP_PIXEL && rc->upload_pixels && cpuBuf) {
        static const uint8_t bars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255},
                                           {0, 255, 0}, {255, 0, 255}, {255, 0, 0},
                                           {0, 0, 255}, {0, 0, 0}};
        int ly = (frame * 4) % 720;
        for (int y = 0; y < 720; y++) {
            uint8_t *row = cpuBuf + (size_t)y * 1280 * 4;
            for (int x = 0; x < 1280; x++) {
                const uint8_t *c = bars[(x / 160) % 8];
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
        rc->upload_pixels(rc, cpuBuf, 1280, 720);
    }
    RcView v;
    memset(&v, 0, sizeof v);
    double t0 = glfwGetTime();
    // Орбита вокруг центра мира (8,26,8), взгляд всегда в него.
    // Орто-режимы тулбара: фиксированные камеры вместо орбиты.
    // Не-воксель смотрит на свою витрину (SDF/меш висят на +50y).
    float lookX = 8, lookY = 26.0f, lookZ = 8, orbitR = 90.0f, orbitY = 66.0f;
    if (domain == RC_CAP_SDF) {
        lookY = 52.0f;
        orbitR = 60.0f;
        orbitY = 62.0f;
    } else if (domain == RC_CAP_MESH) {
        lookX = -24.0f; lookY = 54.0f; lookZ = -16.0f;
        orbitR = 30.0f;
        orbitY = 60.0f;
    }
    double a = frame * 0.004;
    if (viewMode == 1) { // верх
        v.camPos.x = lookX; v.camPos.y = 140.0f; v.camPos.z = lookZ + 0.01f;
    } else if (viewMode == 2) { // бок (+x)
        v.camPos.x = lookX + 120.0f; v.camPos.y = 30.0f; v.camPos.z = lookZ;
    } else if (viewMode == 3) { // фронт (+z)
        v.camPos.x = lookX; v.camPos.y = 30.0f; v.camPos.z = lookZ + 120.0f;
    } else {
        v.camPos.x = lookX + orbitR * (float)cos(a);
        v.camPos.y = orbitY;
        v.camPos.z = lookZ + orbitR * (float)sin(a);
    }
    {
        float dx = lookX - v.camPos.x, dy = lookY - v.camPos.y, dz = lookZ - v.camPos.z;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v.yaw = (float)atan2(dz, dx);
        v.pitch = (float)asin(dy / dl);
    }
    v.fov = camFov;
    v.resW = 1280;
    v.resH = 720;
    v.sunDir.x = 0.4f; v.sunDir.y = 0.75f; v.sunDir.z = 0.3f;
    // Грейд DDA как в референсной сцене template/scene3d (иначе WebGPU в нулях:
    // шейдер множит всё на exposure). GL/VK эти поля не едят.
    v.gamma = 0.611f; v.exposure = 0.508f; v.fog = 0.387f;
    v.shadowOn = 1.0f;
    v.maxSteps = 100.0f;
    v.time = (float)frame / 60.0f;
    // Время суток как в демо (VOX_DAYT, секунды): дефолт полдень (242с,
    // верх дуги sdf_sun), чтобы фаза WebGPU была днём как GL/VK, не сумерками.
    v.dayT = getenv("VOX_DAYT") && getenv("VOX_DAYT")[0]
                 ? (float)atof(getenv("VOX_DAYT"))
                 : 242.0f;
    // PV-кадр для пикинга/таскания — ВСЕМ путям (раньше только воксель-GL,
    // на VK/WebGPU/SDF/вектор/меш мышь была мертва). Чистая математика, без GL.
    {
        Vec3 f2, r2, u2;
        cam_basis(v.yaw, v.pitch, &f2, &r2, &u2);
        Mat4 pr = m4persp(2.0f * atanf(0.5f / (v.fov > 0.2f ? v.fov : 1.6f)), 1280.0f / 720.0f,
                          0.1f, 600.0f);
        Mat4 vw = m4look(v.camPos.x, v.camPos.y, v.camPos.z, v.camPos.x + f2.x,
                         v.camPos.y + f2.y, v.camPos.z + f2.z);
        Mat4 pv = m4mul(&pr, &vw);
        memcpy(pvM, pv.m, sizeof pvM);
        pcamX = v.camPos.x;
        pcamY = v.camPos.y;
        pcamZ = v.camPos.z;
        pfov = v.fov;
        havePV = 1;
        // Экранные концы осей выбранной (для QPainter-осей не-GL путей).
        sHave = 0;
        unsigned sel = ui_selected();
        if (sel) {
            ScTransform et = sc_get(ui_world(), sel);
            float L = 4.0f;
            for (int ax = 0; ax < 3; ax++) {
                float ex = et.pos.x + (ax == 0 ? L : 0);
                float ey = et.pos.y + (ax == 1 ? L : 0);
                float ez = et.pos.z + (ax == 2 ? L : 0);
                float ax0, ay0, ax1, ay1;
                sOk[ax] = 0;
                if (!project_pt(pv.m, et.pos.x, et.pos.y, et.pos.z, &ax0, &ay0)) continue;
                if (!project_pt(pv.m, ex, ey, ez, &ax1, &ay1)) continue;
                sAx[ax][0] = ax0;
                sAx[ax][1] = ay0;
                sAx[ax][2] = ax1;
                sAx[ax][3] = ay1;
                sOk[ax] = 1;
                sHave = 1;
            }
        }
    }
    if (isGL && target && rc->frame_to) {
        rc->frame_to(rc, &v, target);
        // Гизмо-оверлей вторым проходом (vec, бленд): оси выбранной сущности.
        // Концы осей уже посчитаны выше (sAx) — тут только заливка в цель.
        if (vrc && vrc->frame_to) {
            unsigned sel = ui_selected();
            RcVecShape gs[4];
            int ng = 0;
            if (sel && sHave) {
                float cols[3][4] = {{1, 0.2f, 0.2f, 1}, {0.2f, 1, 0.2f, 1}, {0.2f, 0.4f, 1, 1}};
                for (int ax = 0; ax < 3; ax++) {
                    if (!sOk[ax]) continue;
                    gs[ng].kind = 2;
                    gs[ng].a = sAx[ax][0];
                    gs[ng].b = sAx[ax][1];
                    gs[ng].c = sAx[ax][2];
                    gs[ng].d = sAx[ax][3];
                    gs[ng].rgba[0] = cols[ax][0];
                    gs[ng].rgba[1] = cols[ax][1];
                    gs[ng].rgba[2] = cols[ax][2];
                    gs[ng].rgba[3] = cols[ax][3];
                    ng++;
                }
                if (dragAxis >= 0 && dragAxis < 3 && ng > dragAxis) {
                    // Активная ось поверх жёлтой.
                    gs[3] = gs[dragAxis];
                    gs[3].rgba[0] = 1; gs[3].rgba[1] = 1; gs[3].rgba[2] = 0;
                    ng = 4;
                }
            }
            if (ng > 0) {
                vrc->upload_vector(vrc, gs, ng);
            } else {
                // Чистим оверлей: вырожденный примитив (fb memset внутри).
                RcVecShape clr;
                memset(&clr, 0, sizeof clr);
                clr.kind = 0;
                vrc->upload_vector(vrc, &clr, 1);
            }
            RcView vv;
            memset(&vv, 0, sizeof vv);
            vv.resW = 1280;
            vv.resH = 720;
            vrc->frame_to(vrc, &vv, target);
        }
        // Цель -> QImage (верх GL снизу: строки флипаем).
        glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
        static uint8_t row[1280 * 3];
        for (int y = 0; y < 720; y++) {
            glReadPixels(0, y, 1280, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
            uint8_t *dst = img.scanLine(719 - y);
            memcpy(dst, row, sizeof row);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (rc->frame_cpu && cpuBuf) {
        // VK/WebGPU: ядро рендерит и скачивает кадр в CPU (RGBA, верх первый).
        // Синхронный сталл на забор — честная цена встраивания, как glReadPixels.
        if (rc->frame_cpu(rc, &v, cpuBuf, 1280, 720)) {
            for (int y = 0; y < 720; y++) {
                uint8_t *s = cpuBuf + (size_t)y * 1280 * 4;
                uint8_t *d = img.scanLine(y);
                for (int x = 0; x < 1280; x++) {
                    d[x * 3 + 0] = s[x * 4 + 0];
                    d[x * 3 + 1] = s[x * 4 + 1];
                    d[x * 3 + 2] = s[x * 4 + 2];
                }
            }
            placeheld = 0;
        } else {
            img.fill(Qt::black);
            placeheld = 1;
        }
    } else {
        // Нет ни frame_to, ни frame_cpu: ядро рисует в своё скрытое окно
        // (невидимо) — держим устройство живым, в панель пишем заглушку.
        rc->frame(rc, &v);
        img.fill(Qt::black);
        placeheld = 1;
    }
    double dt = glfwGetTime() - t0;
    if (dt > 0) {
        double fps = 1.0 / dt;
        fpsEma = fpsEma > 0 ? fpsEma * 0.95 + fps * 0.05 : fps;
    }
    frame++;
}

void Viewport::tick() {
    renderOnce();
    update();
}

void Viewport::frameNow() {
    if (rc) renderOnce();
    update();
}

bool Viewport::hasPicture() const {
    if (img.isNull()) return false;
    // Центр 100x100 ярче порога (небо/террейн, не чернота).
    long sum = 0;
    int n = 0;
    for (int y = 310; y < 410; y += 2)
        for (int x = 590; x < 690; x += 2) {
            QRgb p = img.pixel(x, y);
            sum += qRed(p) + qGreen(p) + qBlue(p);
            n++;
        }
    return n > 0 && sum / n > 36; // средняя >12 на канал
}

// Виджет -> пиксели кадра 1280x720 (с учётом letterbox).
static int to_image(Viewport *vp, float mx, float my, float *ix, float *iy) {
    int w = vp->width(), h = vp->height();
    if (w <= 0 || h <= 0) return 0;
    float sc = w / 1280.0f < h / 720.0f ? w / 1280.0f : h / 720.0f;
    float ox = (w - 1280.0f * sc) * 0.5f, oy = (h - 720.0f * sc) * 0.5f;
    *ix = (mx - ox) / sc;
    *iy = (my - oy) / sc;
    return *ix >= 0 && *ix < 1280 && *iy >= 0 && *iy < 720;
}

void Viewport::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || !havePV) return;
    unsigned sel = ui_selected();
    if (!sel) return;
    ScTransform et = sc_get(ui_world(), sel);
    float ix, iy;
    if (!to_image(this, (float)e->pos().x(), (float)e->pos().y(), &ix, &iy)) return;
    float L = 4.0f, best = 10.0f;
    int hit = -1;
    for (int ax = 0; ax < 3; ax++) {
        float ex = et.pos.x + (ax == 0 ? L : 0);
        float ey = et.pos.y + (ax == 1 ? L : 0);
        float ez = et.pos.z + (ax == 2 ? L : 0);
        float ax0, ay0, ax1, ay1;
        if (!project_pt(pvM, et.pos.x, et.pos.y, et.pos.z, &ax0, &ay0)) continue;
        if (!project_pt(pvM, ex, ey, ez, &ax1, &ay1)) continue;
        float d = seg_dist(ix, iy, ax0, ay0, ax1, ay1);
        if (d < best) { best = d; hit = ax; }
    }
    if (hit >= 0) {
        dragAxis = hit;
        dragSX = ix;
        dragSY = iy;
        dragPX = et.pos.x;
        dragPY = et.pos.y;
        dragPZ = et.pos.z;
        dragRX = et.rot.x;
        dragRY = et.rot.y;
        dragRZ = et.rot.z;
        dragEX = et.scale.x;
        dragEY = et.scale.y;
        dragEZ = et.scale.z;
    }
}

void Viewport::mouseMoveEvent(QMouseEvent *e) {
    if (dragAxis < 0 || !havePV) return;
    unsigned sel = ui_selected();
    if (!sel) { dragAxis = -1; return; }
    float ix, iy;
    if (!to_image(this, (float)e->pos().x(), (float)e->pos().y(), &ix, &iy)) return;
    // Экранное направление оси из стартовой проекции.
    float L = 4.0f;
    float ex = dragPX + (dragAxis == 0 ? L : 0);
    float ey = dragPY + (dragAxis == 1 ? L : 0);
    float ez = dragPZ + (dragAxis == 2 ? L : 0);
    float ax0, ay0, ax1, ay1;
    if (!project_pt(pvM, dragPX, dragPY, dragPZ, &ax0, &ay0)) return;
    if (!project_pt(pvM, ex, ey, ez, &ax1, &ay1)) return;
    float dx = ax1 - ax0, dy = ay1 - ay0;
    float dl = sqrtf(dx * dx + dy * dy);
    if (dl < 1e-6f) return;
    dx /= dl;
    dy /= dl;
    // Мировых единиц на пиксель на глубине сущности.
    float dist = sqrtf((pcamX - dragPX) * (pcamX - dragPX) +
                       (pcamY - dragPY) * (pcamY - dragPY) +
                       (pcamZ - dragPZ) * (pcamZ - dragPZ));
    float wpp = dist / (pfov > 0.2f ? pfov : 1.6f) / 720.0f;
    float pixAlong = (ix - dragSX) * dx + (iy - dragSY) * dy;
    float along = pixAlong * wpp;
    ScTransform et = sc_get(ui_world(), sel);
    if (gizmoMode == 1) {
        // Крутить: пиксели вдоль оси -> радианы (0.01/px).
        float a = pixAlong * 0.01f;
        if (dragAxis == 0) et.rot.x = dragRX + a;
        else if (dragAxis == 1) et.rot.y = dragRY + a;
        else et.rot.z = dragRZ + a;
    } else if (gizmoMode == 2) {
        // Тянуть: пиксели вдоль оси -> множитель (0.004/px), пол 0.05.
        float k = 1.0f + pixAlong * 0.004f;
        if (k < 0.05f) k = 0.05f;
        if (dragAxis == 0) et.scale.x = dragEX * k;
        else if (dragAxis == 1) et.scale.y = dragEY * k;
        else et.scale.z = dragEZ * k;
    } else {
        // Двигать: мировые единицы вдоль оси.
        if (dragAxis == 0) et.pos.x = dragPX + along;
        else if (dragAxis == 1) et.pos.y = dragPY + along;
        else et.pos.z = dragPZ + along;
    }
    sc_set(ui_world(), sel, &et);
}

void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    (void)e;
    if (dragAxis >= 0) {
        dragAxis = -1;
        ui_select(ui_selected()); // обновить спинбоксы инспектора
    }
}

void Viewport::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (placeheld) {
        // Честная заглушка вместо чёрного экрана: забор кадра не встал.
        p.setPen(Qt::yellow);
        p.drawText(rect(), Qt::AlignCenter,
                   QString("backend %1: core runs, readback failed\n(frame_cpu=0)").arg(backend));
    } else if (!img.isNull()) {
        QSize s = img.size().scaled(size(), Qt::KeepAspectRatio);
        QPoint at((width() - s.width()) / 2, (height() - s.height()) / 2);
        p.drawImage(QRect(at, s), img);
        // Оси гизмо поверх: у GL их рисует vec-оверлей в цели, у VK/WebGPU
        // цели нет — те же концы (sAx) доводим QPainter'ом. Масштаб как у кадра.
        if (!vrc && sHave && havePV) {
            double kx = (double)s.width() / 1280.0, ky = (double)s.height() / 720.0;
            QColor cols[3] = {QColor(255, 51, 51), QColor(51, 255, 51), QColor(51, 102, 255)};
            for (int ax = 0; ax < 3; ax++) {
                if (!sOk[ax]) continue;
                QColor cc = (dragAxis == ax) ? QColor(255, 255, 0) : cols[ax];
                p.setPen(QPen(cc, 2));
                p.drawLine(at.x() + (int)(sAx[ax][0] * kx), at.y() + (int)(sAx[ax][1] * ky),
                           at.x() + (int)(sAx[ax][2] * kx), at.y() + (int)(sAx[ax][3] * ky));
            }
        }
    }
    p.setPen(Qt::white);
    p.drawText(8, 16, QString("fps %1").arg(fpsEma, 0, 'f', 0));
    // Чип как в референсах: FPS + бэкенд справа сверху.
    {
        const char *be = backend == 1 ? "WebGPU" : (backend == 3 ? "Vulkan" : "OpenGL");
        QString chip = QString("FPS: %1 / RHI: %2").arg(fpsEma, 0, 'f', 0).arg(be);
        QFontMetrics fm(p.font());
        int tw = fm.horizontalAdvance(chip) + 16;
        p.fillRect(width() - tw - 8, 8, tw, 22, QColor(0, 0, 0, 160));
        p.drawText(width() - tw, 24, chip);
    }
}
