// Вьюпорт: движок в скрытом окне, кадры в QImage. C++ без Q_OBJECT.
// Порядок важен: glad ПЕРВЫМ, иначе Qt притащит системный gl.h.
#include <glad/gl.h>
#include "viewport.h"
#include <QPainter>
#include <QPaintEvent>
#include <GLFW/glfw3.h>
#include "render_api.h"
#include "rtarget.h"
#include "comp_gl.h"
#include "rhi_gl.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"
#include "vox_feed.h"

RenderCore *rc_gl_create(void);

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
    // Цель и окно переживут? Окно скрытое — отдаём движку? Нет: окно наше.
    // Упрощение скелета: окно и цель живут до конца процесса (утечка
    // пары дескрипторов на выход — честно зафиксировано, не скрыто).
}

bool Viewport::init() {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "ed-viewport", 0, 0);
    if (!win) return false;
    rc = rc_gl_create();
    if (!rc || !rc->init(rc, win)) return false;
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
    if (!target || !comp_target_gl_new(target, 1280, 720)) return false;
    presentProg = rhi_gl_prog("engine/shaders/blit/blit.vert",
                              "engine/shaders/blit/blit.frag");
    presentVao = rhi_gl_empty_vao();
    img = QImage(1280, 720, QImage::Format_RGB888);
    img.fill(Qt::black);
    return presentProg && presentVao;
}

void Viewport::setPlaying(bool on) {
    playing = on;
    if (on) timer->start(16);
    else timer->stop();
}

static const VoxChunk *vfind(void *world, int cx, int cz) {
    VoxChunk *w = (VoxChunk *)world;
    const int GW = 5;
    int ix = cx + GW / 2, iz = cz + GW / 2;
    if (ix < 0 || iz < 0 || ix >= GW || iz >= GW) return 0;
    return &w[iz * GW + ix];
}

void Viewport::renderOnce() {
    if (!rc || !target) return;
    // Догрузка мира по 12 чанков за тик (фидер движка).
    for (int k = 0; k < 12 && fedLeft > 0; k++) {
        while (fedCur < GW * GW && fed[fedCur]) fedCur++;
        if (fedCur >= GW * GW) { fedLeft = 0; break; }
        int cx = fedCur % GW, cz = fedCur / GW;
        VoxChunk *c = &((VoxChunk *)world)[cz * GW + cx];
        const VoxChunk *nb[6] = {
            vfind(world, c->cx - 1, c->cz), vfind(world, c->cx + 1, c->cz), 0, 0,
            vfind(world, c->cx, c->cz - 1), vfind(world, c->cx, c->cz + 1),
        };
        if (vox_feed_one(rc, c, nb) >= 0) { fed[fedCur] = 1; fedLeft--; }
        fedCur++;
    }
    RcView v;
    memset(&v, 0, sizeof v);
    double t0 = glfwGetTime();
    v.camPos.x = 8 + 90.0f * cosf(camYaw * 3.0f);
    v.camPos.y = 66.0f;
    v.camPos.z = 8 + 90.0f * sinf(camYaw * 3.0f);
    v.yaw = camYaw;
    v.pitch = camPitch;
    v.fov = camFov;
    v.resW = 1280;
    v.resH = 720;
    v.sunDir.x = 0.4f; v.sunDir.y = 0.75f; v.sunDir.z = 0.3f;
    if (rc->frame_to) rc->frame_to(rc, &v, target);
    // Цель -> QImage (верх GL снизу: строки флипаем).
    glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    static uint8_t row[1280 * 3];
    for (int y = 0; y < 720; y++) {
        glReadPixels(0, y, 1280, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
        uint8_t *dst = img.scanLine(719 - y);
        memcpy(dst, row, sizeof row);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

void Viewport::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!img.isNull()) {
        QSize s = img.size().scaled(size(), Qt::KeepAspectRatio);
        QPoint at((width() - s.width()) / 2, (height() - s.height()) / 2);
        p.drawImage(QRect(at, s), img);
    }
    p.setPen(Qt::white);
    p.drawText(8, 16, QString("fps %1").arg(fpsEma, 0, 'f', 0));
}
