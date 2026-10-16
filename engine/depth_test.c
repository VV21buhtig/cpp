// Unified Depth пруф: сфера SDF спереди + куб меша сзади.
// Центр кадра обязан быть сферой ВНЕ зависимости от порядка проходов
// ([sdf,mesh] и [mesh,sdf]). Без общей глубины порядок решал бы.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "rhi_gl.h"
#include "render_api.h"
#include "rtarget.h"
#include "comp.h"
#include "comp_gl.h"

#define W 640
#define H 480

// Куб-суп: центр/полуразмер/цвет, нормали наружу. 36 вершин x 9 float.
static int g_cube(float *v, float cx, float cy, float cz, float h, const float *col) {
    float P[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                     {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    int F[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                   {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7}};
    float NN[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    int n = 0;
    int idx[6] = {0, 2, 1, 0, 3, 2}; // обратный порядок: наружу (было наизнанку)
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 6; k++) {
            int q = F[f][idx[k]];
            v[n * 9 + 0] = cx + P[q][0] * h;
            v[n * 9 + 1] = cy + P[q][1] * h;
            v[n * 9 + 2] = cz + P[q][2] * h;
            v[n * 9 + 3] = NN[f][0];
            v[n * 9 + 4] = NN[f][1];
            v[n * 9 + 5] = NN[f][2];
            v[n * 9 + 6] = col[0];
            v[n * 9 + 7] = col[1];
            v[n * 9 + 8] = col[2];
            n++;
        }
    return n;
}

typedef struct {
    RenderCore *rc;
    int fed;
} Ctx;

static void fixed_view(void *ctx, RcView *v) {
    (void)ctx;
    memset(v, 0, sizeof *v);
    v->camPos.x = 0;
    v->camPos.y = 2;
    v->camPos.z = 14;
    // Взгляд в (0,0,0).
    {
        float dx = -v->camPos.x, dy = -v->camPos.y, dz = -v->camPos.z;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v->yaw = (float)atan2(dz, dx);
        v->pitch = (float)asin(dy / dl);
    }
    v->fov = 1.6f;
    v->resW = W;
    v->resH = H;
    v->sunDir.x = 0.4f;
    v->sunDir.y = 0.75f;
    v->sunDir.z = 0.3f;
    v->gamma = 0.611f;
    v->exposure = 0.508f;
    v->fog = 0.387f;
    v->shadowOn = 1.0f;
    v->maxSteps = 100.0f;
    v->dayT = 242.0f;
}

static void feed_sdf(void *ctx, RenderCore *rc) {
    Ctx *c = (Ctx *)ctx;
    if (c->fed) return;
    RcSdfObj o;
    memset(&o, 0, sizeof o);
    o.kind = 0; // сфера в начале координат, r=2 (спереди)
    o.r = 2.0f;
    o.mat = 0; // красная
    if (rc->upload_sdf(rc, &o, 1)) c->fed = 1;
}

static void feed_mesh(void *ctx, RenderCore *rc) {
    Ctx *c = (Ctx *)ctx;
    if (c->fed) return;
    static float v[36 * 9];
    float blue[3] = {0.1f, 0.2f, 0.8f};
    RcMesh m;
    m.verts = v;
    m.nverts = g_cube(v, 0, 0, -8, 2.0f, blue); // сзади сферы
    if (rc->upload_mesh(rc, &m)) c->fed = 1;
}

// Средний RGB центра 40x40. Возвращает 1 если «сфера» (красный явно выше).
static int center_is_sphere(const uint8_t *px, int *outR, int *outG, int *outB) {    long sr = 0, sg = 0, sb = 0;
    int n = 0;
    for (int y = H / 2 - 20; y < H / 2 + 20; y += 2)
        for (int x = W / 2 - 20; x < W / 2 + 20; x += 2) {
            sr += px[((size_t)y * W + x) * 3 + 0];
            sg += px[((size_t)y * W + x) * 3 + 1];
            sb += px[((size_t)y * W + x) * 3 + 2];
            n++;
        }
    *outR = (int)(sr / n);
    *outG = (int)(sg / n);
    *outB = (int)(sb / n);
    return *outR > 120 && *outR > *outG + 40 && *outR > *outB + 40;
}

static int quad12(float *v, float z, float half, float tile) {
    // Квад 2 треугольника в плоскости z (нормаль +z, uv весь тайл).
    // v12: pos3 norm3 uv2 tile ao day night.
    float P[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        int q = idx[k];
        v[k * 12 + 0] = P[q][0] * half;
        v[k * 12 + 1] = P[q][1] * half;
        v[k * 12 + 2] = z;
        v[k * 12 + 3] = 0;
        v[k * 12 + 4] = 0;
        v[k * 12 + 5] = 1;
        v[k * 12 + 6] = (P[q][0] * 0.5f + 0.5f);
        v[k * 12 + 7] = (P[q][1] * 0.5f + 0.5f);
        v[k * 12 + 8] = tile;
        v[k * 12 + 9] = 1;
        v[k * 12 + 10] = 1;
        v[k * 12 + 11] = 1;
    }
    return 6;
}

// VK-ветка: два перекрывающихся квада в offscreen-цели (спереди z=-5,
// сзади z=-10). Порядок заливки меняем — центр обязан совпасть попиксельно
// (решает глубина цели, не порядок) и отличаться от фона.
static int depth_vk(void) {
    if (!glfwInit()) {
        printf("DEPTH FAIL: glfw\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(W, H, "depth-vk", 0, 0);
    if (!win) {
        printf("DEPTH FAIL: window\n");
        return 1;
    }
    RenderCore *vk = rc_create_by(RC_CAP_VOXEL, RC_BACKEND_VK);
    if (!vk || !vk->init(vk, win)) {
        printf("DEPTH FAIL: init\n");
        return 1;
    }
    if (!vk->frame_to || !vk->download) {
        printf("DEPTH FAIL: no frame_to/download\n");
        return 1;
    }
    static float qf[6 * 12], qb[6 * 12];
    float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    quad12(qf, -5.0f, 6.0f, 2.0f);
    quad12(qb, -10.0f, 6.0f, 2.0f);
    RcView v;
    memset(&v, 0, sizeof v);
    v.camPos.x = 0;
    v.camPos.y = 2;
    v.camPos.z = 14;
    {
        float dx = -v.camPos.x, dy = -v.camPos.y, dz = -v.camPos.z;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v.yaw = (float)atan2(dz, dx);
        v.pitch = (float)asin(dy / dl);
    }
    v.fov = 1.6f;
    v.resW = W;
    v.resH = H;
    v.sunDir.x = 0.4f;
    v.sunDir.y = 0.75f;
    v.sunDir.z = 0.3f;
    v.gamma = 0.611f;
    v.exposure = 0.508f;
    v.fog = 0.387f;
    v.shadowOn = 1.0f;
    v.maxSteps = 100.0f;
    v.dayT = 242.0f;
    static uint8_t px[(size_t)W * H * 4];
    int bad = 0;
    int ref[3] = {0, 0, 0};
    for (int round = 0; round < 2; round++) {
        // Порядок заливки: 0 = [спереди, сзади], 1 = [сзади, спереди].
        vk->unload_voxmesh(vk, 0);
        vk->unload_voxmesh(vk, 1);
        if (round == 0) {
            if (!vk->upload_voxmesh(vk, 0, qf, 6, ident) ||
                !vk->upload_voxmesh(vk, 1, qb, 6, ident)) {
                printf("DEPTH FAIL: upload\n");
                bad = 1;
                break;
            }
        } else {
            if (!vk->upload_voxmesh(vk, 0, qb, 6, ident) ||
                !vk->upload_voxmesh(vk, 1, qf, 6, ident)) {
                printf("DEPTH FAIL: upload\n");
                bad = 1;
                break;
            }
        }
        RcTarget t;
        memset(&t, 0, sizeof t);
        t.backend = RC_BACKEND_VK;
        t.w = W;
        t.h = H;
        t.noClear = 0;
        if (!vk->frame_to(vk, &v, &t)) {
            printf("DEPTH FAIL: frame_to\n");
            bad = 1;
            break;
        }
        memset(px, 0, sizeof px);
        if (!vk->download(vk, &t, px, W, H)) {
            printf("DEPTH FAIL: download\n");
            bad = 1;
            break;
        }
        long sr = 0, sg = 0, sb = 0;
        int n = 0;
        for (int y = H / 2 - 10; y < H / 2 + 10; y++)
            for (int x = W / 2 - 10; x < W / 2 + 10; x++) {
                sr += px[((size_t)y * W + x) * 4 + 0];
                sg += px[((size_t)y * W + x) * 4 + 1];
                sb += px[((size_t)y * W + x) * 4 + 2];
                n++;
            }
        int cr = (int)(sr / n), cg = (int)(sg / n), cb = (int)(sb / n);
        printf("depth-vk round %d: center=(%d,%d,%d)\n", round, cr, cg, cb);
        if (round == 0) {
            ref[0] = cr;
            ref[1] = cg;
            ref[2] = cb;
            // Не фон (clear 0.05,0.07,0.12 ~ (13,18,31)): что-то нарисовано.
            if (cr < 30 && cg < 30 && cb < 60) {
                printf("DEPTH FAIL: empty\n");
                bad = 1;
                break;
            }
        } else if (abs(cr - ref[0]) + abs(cg - ref[1]) + abs(cb - ref[2]) > 12) {
            printf("DEPTH FAIL: order-dependent (%d,%d,%d) vs (%d,%d,%d)\n", cr, cg, cb,
                   ref[0], ref[1], ref[2]);
            bad = 1;
        }
    }
    vk->shutdown(vk);
    rc_destroy(vk);
    glfwDestroyWindow(win);
    glfwTerminate();
    printf(bad ? "DEPTH VK FAIL\n" : "DEPTH VK OK\n");
    return bad;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "vk")) return depth_vk();
    if (!glfwInit()) {
        printf("DEPTH FAIL: glfw\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(W, H, "depth", 0, 0);
    if (!win) {
        printf("DEPTH FAIL: window\n");
        return 1;
    }
    RenderCore *sdf = rc_create_by(RC_CAP_SDF, RC_BACKEND_GL);
    RenderCore *mesh = rc_create_by(RC_CAP_MESH, RC_BACKEND_GL);
    if (!sdf || !mesh || !sdf->init(sdf, win) || !mesh->init(mesh, win)) {
        printf("DEPTH FAIL: init\n");
        return 1;
    }
    RcTarget t;
    memset(&t, 0, sizeof t);
    if (!eng_target_new(2, W, H, &t)) {
        printf("DEPTH FAIL: target\n");
        return 1;
    }
    static uint8_t px[(size_t)W * H * 3];
    int bad = 0;
    // Порядок A: [sdf, mesh]. Порядок B: [mesh, sdf]. Оба — сфера.
    for (int round = 0; round < 2; round++) {
        Ctx cs = {sdf, 0}, cm = {mesh, 0};
        EngPass ps[2];
        if (round == 0) {
            ps[0].rc = sdf;
            ps[0].ctx = &cs;
            ps[0].view = fixed_view;
            ps[0].feed = feed_sdf;
            ps[1].rc = mesh;
            ps[1].ctx = &cm;
            ps[1].view = fixed_view;
            ps[1].feed = feed_mesh;
        } else {
            ps[0].rc = mesh;
            ps[0].ctx = &cm;
            ps[0].view = fixed_view;
            ps[0].feed = feed_mesh;
            ps[1].rc = sdf;
            ps[1].ctx = &cs;
            ps[1].view = fixed_view;
            ps[1].feed = feed_sdf;
        }
        int r = eng_comp_frame(ps, 2, &t);
        if (r != ENG_COMP_OK) {
            printf("DEPTH FAIL: comp %d\n", r);
            bad = 1;
            continue;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int cr, cg, cb;
        // GL-строки снизу: перевернуть блок центра чтением сверху.
        static uint8_t flip[(size_t)W * H * 3];
        for (int y = 0; y < H; y++)
            memcpy(flip + (size_t)y * W * 3, px + (size_t)(H - 1 - y) * W * 3, (size_t)W * 3);
        int ok = center_is_sphere(flip, &cr, &cg, &cb);
        printf("depth round %d: center rgb=(%d,%d,%d) %s\n", round, cr, cg, cb,
               ok ? "SPHERE" : "WRONG");
        if (!ok) bad = 1;
    }
    sdf->shutdown(sdf);
    mesh->shutdown(mesh);
    rc_destroy(sdf);
    rc_destroy(mesh);
    eng_target_free(&t);
    glfwDestroyWindow(win);
    glfwTerminate();
    printf(bad ? "DEPTH FAIL\n" : "DEPTH OK\n");
    return bad;
}
