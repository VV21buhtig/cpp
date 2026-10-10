// Ядро GL (растр): RenderCore API. v0 — окно уже есть, контекст+небо+цикл.
// Мешинг чанков — следующий шаг (upload_chunk пока заглушка, возвращает 0).
// Шейдеры и glad берём из voxel-render БЕЗ копий (ветка noflood).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/render_api.h"
#include "core/mat4.h"
#include "sdf_math.h"

#define GL_SHADER_DIR "../voxel-render/shaders"

typedef struct {
    GLFWwindow *win;
    unsigned skyProg;
    unsigned skyVAO;
    int ready;
    int frame;
    double prevT;
    float fpsEma;
} GLCore;

typedef struct {
    RenderCore api;
    GLCore core;
} GLCoreWrap;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "gl: no file %s\n", path); return 0; }
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

static unsigned gl_prog(const char *vsPath, const char *fsPath) {
    char *vs = read_file(vsPath), *fs = read_file(fsPath);
    if (!vs || !fs) { free(vs); free(fs); return 0; }
    unsigned v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, (const char **)&vs, 0);
    glCompileShader(v);
    unsigned f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, (const char **)&fs, 0);
    glCompileShader(f);
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, 0, log);
        fprintf(stderr, "gl link fail %s: %s\n", fsPath, log);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    free(vs);
    free(fs);
    return p;
}

static int gl_uniform(unsigned prog, const char *name) {
    return glGetUniformLocation(prog, name);
}

static float core_fps(RenderCore *rc) {
    GLCore *c = &((GLCoreWrap *)rc->ctx)->core;
    return c->fpsEma;
}

static int core_upload_chunk(RenderCore *rc, int cx, int cz, const uint8_t *vox16) {
    (void)rc; (void)cx; (void)cz; (void)vox16;
    return 0; // мешинг — следующий шаг
}

static void core_set_origin(RenderCore *rc, int ox, int oz) {
    (void)rc; (void)ox; (void)oz; // растр ходит в мировых координатах
}

static void core_frame(RenderCore *rc, const RcView *v) {
    GLCore *c = &((GLCoreWrap *)rc->ctx)->core;
    double now = glfwGetTime();
    float dt = (float)(now - c->prevT);
    c->prevT = now;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        c->fpsEma = c->fpsEma > 0.0f ? c->fpsEma * 0.95f + fps * 0.05f : fps;
    }
    int ww = v->resW > 0 ? v->resW : 1280;
    int hh = v->resH > 0 ? v->resH : 720;
    glViewport(0, 0, ww, hh);
    glClearColor(0.05f, 0.07f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Небо их шейдером: invVP из нашей mat4 + палитра дня.
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    Mat4 proj = m4persp(2.0f * atanf(0.5f / (v->fov > 0.2f ? v->fov : 1.6f)),
                        (float)ww / (float)hh, 0.1f, 600.0f);
    Mat4 view = m4look(v->camPos.x, v->camPos.y, v->camPos.z,
                       v->camPos.x + f.x, v->camPos.y + f.y, v->camPos.z + f.z);
    Mat4 pv = m4mul(&proj, &view);
    Mat4 inv = m4inv(&pv);
    float dayF = v->sunDir.y > 1.0f ? 1.0f : (v->sunDir.y < -1.0f ? -1.0f : v->sunDir.y);
    float nightF = dayF < 0.02f ? (dayF < -0.12f ? 1.0f : (0.02f - dayF) / 0.14f) : 0.0f;
    glUseProgram(c->skyProg);
    glUniformMatrix4fv(gl_uniform(c->skyProg, "invVP"), 1, 0, inv.m);
    glUniform3f(gl_uniform(c->skyProg, "topColor"), 0.16f, 0.32f, 0.58f);
    glUniform3f(gl_uniform(c->skyProg, "horizonColor"), 0.55f, 0.60f, 0.68f);
    glUniform3f(gl_uniform(c->skyProg, "sunDir"), v->sunDir.x, v->sunDir.y, v->sunDir.z);
    glUniform1f(gl_uniform(c->skyProg, "nightF"), nightF);
    glUniform2f(gl_uniform(c->skyProg, "res"), (float)ww, (float)hh);
    glBindVertexArray(c->skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glfwSwapBuffers(c->win);
    c->frame++;
}

static int core_init(RenderCore *rc, void *glfwWindow) {
    GLCore *c = &((GLCoreWrap *)rc->ctx)->core;
    memset(c, 0, sizeof *c);
    c->win = (GLFWwindow *)glfwWindow;
    glfwMakeContextCurrent(c->win);
    if (!gladLoadGL(glfwGetProcAddress)) { fprintf(stderr, "gl: glad fail\n"); return 0; }
    printf("gl: %s\n", (const char *)glGetString(GL_RENDERER));
    if (!GLAD_GL_VERSION_4_5) { fprintf(stderr, "gl: need 4.5\n"); return 0; }
    c->skyProg = gl_prog(GL_SHADER_DIR "/sky.vs", GL_SHADER_DIR "/sky.fs");
    if (!c->skyProg) return 0;
    glGenVertexArrays(1, &c->skyVAO);
    c->prevT = glfwGetTime();
    printf("gl core OK: sky only (meshing next)\n");
    return 1;
}

static void core_shutdown(RenderCore *rc) {
    GLCore *c = &((GLCoreWrap *)rc->ctx)->core;
    if (c->skyProg) glDeleteProgram(c->skyProg);
    if (c->skyVAO) glDeleteVertexArrays(1, &c->skyVAO);
}

RenderCore *rc_gl_create(void) {
    GLCoreWrap *w = (GLCoreWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.init = core_init;
    w->api.shutdown = core_shutdown;
    w->api.frame = core_frame;
    w->api.upload_chunk = core_upload_chunk;
    w->api.set_origin = core_set_origin;
    w->api.fps = core_fps;
    return &w->api;
}
