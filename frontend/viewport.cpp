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
    // Цель и окно переживут? Окно скрытое — отдаём движку? Нет: окно наше.
    // Упрощение скелета: окно и цель живут до конца процесса (утечка
    // пары дескрипторов на выход — честно зафиксировано, не скрыто).
}

bool Viewport::init(int be) {
    backend = be;
    if (!glfwInit()) return false;
    if (backend == 2) { // GL: контекст 4.5
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else { // WebGPU/Vulkan: без API (поверхность создаст RHI ядра)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "ed-viewport", 0, 0);
    if (!win) return false;
    glfwWin = win;
    // Ядро по таблице «домен x бэкенд» (воксельный домен, бэкенд из хаба).
    rc = rc_create_by(1u, (unsigned)backend);
    if (!rc || !rc->init(rc, win)) return false;
    // Vec-ядро для гизмо-оверлея делит окно/контекст (только GL-режим).
    if (backend == 2) {
        vrc = rc_create_by(4u, 2u);
        if (vrc && !vrc->init(vrc, win)) { vrc = nullptr; }
    }
    world = calloc(GW * GW, sizeof(VoxChunk));
    if (!world) return false;
    for (int cz = 0; cz < GW; cz++)
        for (int cx = 0; cx < GW; cx++) {
            VoxChunk *c = &((VoxChunk *)world)[cz * GW + cx];
            c->cx = cx - GW / 2;
            c->cz = cz - GW / 2;
            vox_gen(c, 1337);
        }
    target = (RcTarget *)calloc(1, sizeof(RcTarget));
    // Цель и показ — только под GL (frame_to есть только там).
    if (backend == 2) {
        if (!target || !comp_target_gl_new(target, 1280, 720)) return false;
        presentProg = rhi_gl_prog("engine/shaders/blit/blit.vert",
                                  "engine/shaders/blit/blit.frag");
        presentVao = rhi_gl_empty_vao();
    }
    img = QImage(1280, 720, QImage::Format_RGB888);
    img.fill(Qt::black);
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
    // Мир не трогаем (воксели общие), fed-флаги сбрасываем — перезальём.
    for (int i = 0; i < GW * GW; i++) fed[i] = 0;
    fedLeft = GW * GW;
    fedCur = 0;
    havePV = 0;
    dragAxis = -1;
    if (!init(be)) return false;
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
    if (!rc) return;
    int isGL = (backend == 2);
    // Догрузка мира по 12 чанков за тик.
    // DDA (webgpu) ест сырьё, растр (gl/vk) — через фидер движка.
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
    RcView v;
    memset(&v, 0, sizeof v);
    double t0 = glfwGetTime();
    // Орбита вокруг центра мира (8,26,8), взгляд всегда в него.
    // Орто-режимы тулбара: фиксированные камеры вместо орбиты.
    double a = frame * 0.004;
    if (viewMode == 1) { // верх
        v.camPos.x = 8; v.camPos.y = 140.0f; v.camPos.z = 8.01f;
    } else if (viewMode == 2) { // бок (+x)
        v.camPos.x = 120.0f; v.camPos.y = 30.0f; v.camPos.z = 8;
    } else if (viewMode == 3) { // фронт (+z)
        v.camPos.x = 8; v.camPos.y = 30.0f; v.camPos.z = 120.0f;
    } else {
        v.camPos.x = 8 + 90.0f * (float)cos(a);
        v.camPos.y = 66.0f;
        v.camPos.z = 8 + 90.0f * (float)sin(a);
    }
    {
        float dx = 8 - v.camPos.x, dy = 26.0f - v.camPos.y, dz = 8 - v.camPos.z;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v.yaw = (float)atan2(dz, dx);
        v.pitch = (float)asin(dy / dl);
    }
    v.fov = camFov;
    v.resW = 1280;
    v.resH = 720;
    v.sunDir.x = 0.4f; v.sunDir.y = 0.75f; v.sunDir.z = 0.3f;
    if (isGL && target && rc->frame_to) {
        rc->frame_to(rc, &v, target);
        // Гизмо-оверлей вторым проходом (vec, бленд): оси выбранной сущности.
        if (vrc && vrc->frame_to) {
            unsigned sel = ui_selected();
            RcVecShape gs[4];
            int ng = 0;
            if (sel) {
                ScTransform et = sc_get(ui_world(), sel);
                // PV из той же камеры что сцена.
                Vec3 f2, r2, u2;
                cam_basis(v.yaw, v.pitch, &f2, &r2, &u2);
                Mat4 pr = m4persp(2.0f * atanf(0.5f / (v.fov > 0.2f ? v.fov : 1.6f)),
                                  1280.0f / 720.0f, 0.1f, 600.0f);
                Mat4 vw = m4look(v.camPos.x, v.camPos.y, v.camPos.z,
                                 v.camPos.x + f2.x, v.camPos.y + f2.y, v.camPos.z + f2.z);
                Mat4 pv = m4mul(&pr, &vw);
                memcpy(pvM, pv.m, sizeof pvM);
                pcamX = v.camPos.x; pcamY = v.camPos.y; pcamZ = v.camPos.z;
                pfov = v.fov;
                havePV = 1;
                float L = 4.0f;
                float cols[3][4] = {{1, 0.2f, 0.2f, 1}, {0.2f, 1, 0.2f, 1}, {0.2f, 0.4f, 1, 1}};
                for (int ax = 0; ax < 3; ax++) {
                    float ex = et.pos.x + (ax == 0 ? L : 0);
                    float ey = et.pos.y + (ax == 1 ? L : 0);
                    float ez = et.pos.z + (ax == 2 ? L : 0);
                    float ax0, ay0, ax1, ay1;
                    if (!project_pt(pv.m, et.pos.x, et.pos.y, et.pos.z, &ax0, &ay0)) continue;
                    if (!project_pt(pv.m, ex, ey, ez, &ax1, &ay1)) continue;
                    gs[ng].kind = 2;
                    gs[ng].a = ax0; gs[ng].b = ay0; gs[ng].c = ax1; gs[ng].d = ay1;
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
    } else {
        // Не-GL: ядра рисуют в своё скрытое окно (невидимо) — держим
        // устройство живым, а в панель пишем заглушку. frame_to для них —
        // следующий шаг, честно зафиксировано.
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
    float along = ((ix - dragSX) * dx + (iy - dragSY) * dy) * wpp;
    ScTransform et = sc_get(ui_world(), sel);
    if (dragAxis == 0) et.pos.x = dragPX + along;
    else if (dragAxis == 1) et.pos.y = dragPY + along;
    else et.pos.z = dragPZ + along;
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
        // Честная заглушка вместо чёрного экрана: бэкенд жив, вьюпорта нет.
        p.setPen(Qt::yellow);
        p.drawText(rect(), Qt::AlignCenter,
                   QString("backend %1: core runs, viewport embedding later\n(frame_to pending)").arg(backend));
    } else if (!img.isNull()) {
        QSize s = img.size().scaled(size(), Qt::KeepAspectRatio);
        QPoint at((width() - s.width()) / 2, (height() - s.height()) / 2);
        p.drawImage(QRect(at, s), img);
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
