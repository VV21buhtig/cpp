// Ядро SDF (сферотрейсинг): RenderCore API, caps=SDF. Бэкенд GL 4.5
// (там готовый VOX_SHOT-стенд). Сцена — upload_sdf, камера — из RcView.
// Бейдж синий (GL-воксель зелёный, VK красный).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"
#include "sdf_math.h"

#define SDF_SHADER_DIR "shaders/sdf_gl"
#define SDF_SHADER_ALT "../webgpuelectronsdh_engine/shaders/sdf_gl"
#define SDF_MAX 64

typedef struct {
    GLFWwindow *win;
    unsigned prog;
    unsigned vao;
    unsigned ubo;
    int u_nPrim;
    RcSdfObj objs[SDF_MAX];
    int nObjs;
    int ready;
    int frame;
    double prevT;
    float fpsEma;
} SDFCore;

typedef struct {
    RenderCore api;
    SDFCore core;
} SDFCoreWrap;

static char *sdf_read(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        // второй шанс: запуск из каталога CPP (алиасы идут из корня движка,
        // но мало ли) — префикс родителя.
        char alt[512];
        snprintf(alt, sizeof alt, "%s/%s", SDF_SHADER_ALT, path + strlen(SDF_SHADER_DIR) + 1);
        f = fopen(alt, "rb");
        if (!f) { fprintf(stderr, "sdf: no file %s\n", path); return 0; }
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) { fclose(f); return 0; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return 0; }
    s[n] = 0;
    fclose(f);
    return s;
}

static unsigned sdf_prog(const char *vs, const char *fs) {
    char *vsc = sdf_read(vs), *fsc = sdf_read(fs);
    if (!vsc || !fsc) { free(vsc); free(fsc); return 0; }
    unsigned v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, (const char **)&vsc, 0);
    glCompileShader(v);
    int ok = 0;
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[1024]; glGetShaderInfoLog(v, sizeof l, 0, l); fprintf(stderr, "sdf vs: %s\n", l); }
    unsigned f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, (const char **)&fsc, 0);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[1024]; glGetShaderInfoLog(f, sizeof l, 0, l); fprintf(stderr, "sdf fs: %s\n", l); }
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char l[1024];
        glGetProgramInfoLog(p, sizeof l, 0, l);
        fprintf(stderr, "sdf link: %s\n", l);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    free(vsc);
    free(fsc);
    return p;
}

static float sdf_fps(RenderCore *rc) {
    return ((SDFCoreWrap *)rc->ctx)->core.fpsEma;
}

static int sdf_upload_sdf(RenderCore *rc, const RcSdfObj *objs, int n) {
    SDFCore *c = &((SDFCoreWrap *)rc->ctx)->core;
    if (!c->ready || !objs || n <= 0) return 0;
    if (n > SDF_MAX) n = SDF_MAX;
    memcpy(c->objs, objs, (size_t)n * sizeof(RcSdfObj));
    c->nObjs = n;
    return 1;
}

static void sdf_frame(RenderCore *rc, const RcView *v) {
    SDFCore *c = &((SDFCoreWrap *)rc->ctx)->core;
    double now = glfwGetTime();
    float dt = (float)(now - c->prevT);
    c->prevT = now;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        c->fpsEma = c->fpsEma > 0.0f ? c->fpsEma * 0.95f + fps * 0.05f : fps;
    }
    int ww = v->resW > 0 ? v->resW : 1280;
    int hh = v->resH > 0 ? v->resH : 720;
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    float dayF = v->sunDir.y > 1.0f ? 1.0f : (v->sunDir.y < -1.0f ? -1.0f : v->sunDir.y);
    float nightF = dayF < 0.02f ? (dayF < -0.12f ? 1.0f : (0.02f - dayF) / 0.14f) : 0.0f;
    // UBO: шапка 6 vec4 + 64 x 3 vec4.
    static float ub[6 * 4 + 64 * 3 * 4];
    ub[0] = v->camPos.x; ub[1] = v->camPos.y; ub[2] = v->camPos.z; ub[3] = 0;
    ub[4] = f.x; ub[5] = f.y; ub[6] = f.z; ub[7] = 0;
    ub[8] = r.x; ub[9] = r.y; ub[10] = r.z; ub[11] = 0;
    ub[12] = u.x; ub[13] = u.y; ub[14] = u.z; ub[15] = 0;
    ub[16] = (float)ww; ub[17] = (float)hh;
    ub[18] = v->fov > 0.2f ? v->fov : 1.6f; ub[19] = v->time;
    ub[20] = v->sunDir.x; ub[21] = v->sunDir.y; ub[22] = v->sunDir.z; ub[23] = nightF;
    for (int i = 0; i < c->nObjs; i++) {
        float *a = ub + 24 + (i * 3 + 0) * 4;
        float *b = ub + 24 + (i * 3 + 1) * 4;
        float *cc = ub + 24 + (i * 3 + 2) * 4;
        a[0] = c->objs[i].p0.x; a[1] = c->objs[i].p0.y; a[2] = c->objs[i].p0.z;
        a[3] = c->objs[i].r;
        b[0] = c->objs[i].p1.x; b[1] = c->objs[i].p1.y; b[2] = c->objs[i].p1.z;
        b[3] = (float)c->objs[i].mat;
        cc[0] = (float)c->objs[i].kind; cc[1] = cc[2] = cc[3] = 0;
    }
    glViewport(0, 0, ww, hh);
    glUseProgram(c->prog);
    glBindBuffer(GL_UNIFORM_BUFFER, c->ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof ub, ub);
    glUniform1i(c->u_nPrim, c->nObjs);
    glBindVertexArray(c->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    // Бейдж SDF: синий угол.
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 64, 32);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    // Скриншот из буфера (тот же протокол что VOX_SHOT у gl-ядра).
    {
        const char *sp = getenv("VOX_SHOT");
        if (sp && sp[0]) {
            const char *sa = getenv("VOX_SHOT_AT");
            int at = sa && sa[0] ? atoi(sa) : 60;
            if (c->frame == at) {
                uint8_t *px = (uint8_t *)malloc((size_t)ww * hh * 3);
                if (px) {
                    glReadPixels(0, 0, ww, hh, GL_RGB, GL_UNSIGNED_BYTE, px);
                    FILE *ff = fopen(sp, "wb");
                    if (ff) {
                        fprintf(ff, "P6\n%d %d\n255\n", ww, hh);
                        for (int y = hh - 1; y >= 0; y--)
                            fwrite(px + (size_t)y * ww * 3, 1, (size_t)ww * 3, ff);
                        fclose(ff);
                        fprintf(stderr, "shot: %s (%dx%d)\n", sp, ww, hh);
                    }
                    free(px);
                }
            }
        }
    }
    glfwSwapBuffers(c->win);
    c->frame++;
}

static int sdf_init(RenderCore *rc, void *glfwWindow) {
    SDFCore *c = &((SDFCoreWrap *)rc->ctx)->core;
    memset(c, 0, sizeof *c);
    c->win = (GLFWwindow *)glfwWindow;
    glfwMakeContextCurrent(c->win);
    if (!gladLoadGL(glfwGetProcAddress)) { fprintf(stderr, "sdf: glad fail\n"); return 0; }
    if (!GLAD_GL_VERSION_4_5) { fprintf(stderr, "sdf: need 4.5\n"); return 0; }
    c->prog = sdf_prog(SDF_SHADER_DIR "/sdf_ray.vert", SDF_SHADER_DIR "/sdf_ray.frag");
    if (!c->prog) return 0;
    glGenVertexArrays(1, &c->vao);
    glGenBuffers(1, &c->ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, c->ubo);
    glBufferData(GL_UNIFORM_BUFFER, 6 * 16 + 64 * 3 * 16, 0, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, c->ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    c->u_nPrim = glGetUniformLocation(c->prog, "nPrim");
    c->prevT = glfwGetTime();
    c->ready = 1;
    printf("sdf core OK: raymarch %d prims max\n", SDF_MAX);
    return 1;
}

static void sdf_shutdown(RenderCore *rc) {
    SDFCore *c = &((SDFCoreWrap *)rc->ctx)->core;
    if (c->prog) glDeleteProgram(c->prog);
    if (c->vao) glDeleteVertexArrays(1, &c->vao);
    if (c->ubo) glDeleteBuffers(1, &c->ubo);
}


// Саморегистрация в реестре corelib (выбор по имени/домену безifndef).
#ifdef __GNUC__
__attribute__((constructor))
#endif
static void rc_reg_self(void) {
    rc_register("sdf", RC_CAP_SDF, RC_BACKEND_GL, rc_sdf_create);
}

RenderCore *rc_sdf_create(void) {
    SDFCoreWrap *w = (SDFCoreWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_SDF;
    w->api.name = "sdf";
    w->api.backend = RC_BACKEND_GL;
    w->api.init = sdf_init;
    w->api.shutdown = sdf_shutdown;
    w->api.frame = sdf_frame;
    w->api.upload_sdf = sdf_upload_sdf;
    w->api.fps = sdf_fps;
    return &w->api;
}
