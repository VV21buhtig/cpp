// Ядро MESH (треугольный суп): RenderCore API, caps=MESH. Первое ядро,
// написанное сразу в орфане (не порт): VBO супа + Ламберт на RHI.
// Модель жёсткая (MVP целиком из вида+вращения демо). Бейдж маджента.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "render_api.h"
#include "rtarget.h"
#include "mat4.h"
#include "rhi_gl.h"

RenderCore *rc_mesh_create(void); // определено ниже, нужно регистрации

typedef struct {
    RenderCore api;
    GLFWwindow *win;
    RhiGlProg prog;
    RhiGlProg pick; // pick-программа (тот же mesh.vert + плоский id)
    int u_pickMVP, u_pickId;
    unsigned vao;
    unsigned vbo;
    int u_mvp, u_sun;
    float *soup; // копия супа (владеем), 9 float на вершину
    int nverts;
    int ready;
    int frame;
    double prevT;
    float fpsEma;
} MeshWrap;

static float mesh_fps(RenderCore *rc) {
    return ((MeshWrap *)rc->ctx)->fpsEma;
}

// Пути шейдеров — через VFS (vfs_resolve в prog_load: проект перекрывает сток).

// Локации mesh-программы (инит и каждый reload).
static void mesh_fetch(MeshWrap *c) {
    c->u_mvp = glGetUniformLocation(c->prog.prog, "uMVP");
    c->u_sun = glGetUniformLocation(c->prog.prog, "uSun");
}

// Горячая замена шейдера (Monaco): swap + refetch + validate.
static int core_shader_reload(RenderCore *rc, const char *id, const char *code, char *log,
                              int logcap) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    char lmem[2048] = {0};
    unsigned np = rhi_gl_prog_reload(&c->prog, id, code, lmem, sizeof lmem);
    if (!np) {
        if (log && logcap > 0) {
            strncpy(log, lmem, (size_t)logcap - 1);
            log[logcap - 1] = 0;
        }
        return 0;
    }
    unsigned old = c->prog.prog;
    c->prog.prog = np;
    mesh_fetch(c);
    glValidateProgram(np);
    int vs = 0;
    glGetProgramiv(np, GL_VALIDATE_STATUS, &vs);
    if (!vs) {
        char l[1024] = {0};
        glGetProgramInfoLog(np, sizeof l, 0, l);
        c->prog.prog = old;
        mesh_fetch(c);
        glDeleteProgram(np);
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "validate fail, откат: %s", l);
        return 0;
    }
    glDeleteProgram(old);
    if (log && logcap > 0) snprintf(log, (size_t)logcap, "mesh: %s", lmem);
    return 1;
}

static int mesh_upload(RenderCore *rc, const RcMesh *m) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    if (!c->ready || !m || !m->verts || m->nverts <= 0) return 0;
    if (m->nverts > 1000000) return 0;
    size_t n = (size_t)m->nverts * 9 * sizeof(float);
    float *nv = (float *)malloc(n);
    if (!nv) return 0;
    memcpy(nv, m->verts, n);
    free(c->soup);
    c->soup = nv;
    c->nverts = m->nverts;
    glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)n, nv, GL_STATIC_DRAW);
    return 1;
}

// Рисунок без swap (цель — окно или FBO композитора).
// first=0 (поздний проход): без чисток — только глубинный тест.
static void mesh_draw(MeshWrap *c, const RcView *v, int ww, int hh, int first) {
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    Mat4 proj = m4persp(2.0f * atanf(0.5f / (v->fov > 0.2f ? v->fov : 1.6f)),
                        (float)ww / (float)hh, 0.1f, 600.0f);
    Mat4 view = m4look(v->camPos.x, v->camPos.y, v->camPos.z,
                       v->camPos.x + f.x, v->camPos.y + f.y, v->camPos.z + f.z);
    Mat4 pv = m4mul(&proj, &view);
    glViewport(0, 0, ww, hh);
    if (first) {
        glClearColor(0.05f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS); // единая глубина: у всех 3D-проходов LESS (SDF: LEQUAL)
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(c->prog.prog);
    glUniformMatrix4fv(c->u_mvp, 1, 0, pv.m);
    glUniform3f(c->u_sun, v->sunDir.x, v->sunDir.y, v->sunDir.z);
    glBindVertexArray(c->vao);
    if (c->nverts > 0) glDrawArrays(GL_TRIANGLES, 0, c->nverts);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_CULL_FACE);
    // Бейдж меша: маджента.
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 64, 32);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

static void mesh_frame(RenderCore *rc, const RcView *v) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    double now = glfwGetTime();
    float dt = (float)(now - c->prevT);
    c->prevT = now;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        c->fpsEma = c->fpsEma > 0.0f ? c->fpsEma * 0.95f + fps * 0.05f : fps;
    }
    int ww = v->resW > 0 ? v->resW : 1280;
    int hh = v->resH > 0 ? v->resH : 720;
    mesh_draw(c, v, ww, hh, 1);
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

// Пикинг: текущий суп плоским id сущности в цель (R=id+1, фон 0).
// Глубина честная (тот же тест): перекрытое не выберется.
// Чистки НЕТ (контракт Этапа Б-пик): чистит pick_begin один раз на проход,
// сюда зовут N раз подряд — nearer выигрывает по глубине, не по порядку.
static int mesh_pick(RenderCore *rc, const RcView *v, RcTarget *t, unsigned id) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    if (!t || t->backend != RC_BACKEND_GL || !t->fbo || !c->nverts) return 0;
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    Mat4 proj = m4persp(2.0f * atanf(0.5f / (v->fov > 0.2f ? v->fov : 1.6f)),
                        (float)t->w / (float)t->h, 0.1f, 600.0f);
    Mat4 view = m4look(v->camPos.x, v->camPos.y, v->camPos.z,
                       v->camPos.x + f.x, v->camPos.y + f.y, v->camPos.z + f.z);
    Mat4 pv = m4mul(&proj, &view);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE); // обе стороны: id важнее лицевой
    glUseProgram(c->pick.prog);
    glUniformMatrix4fv(c->u_pickMVP, 1, 0, pv.m);
    glUniform1ui(c->u_pickId, id + 1);
    glBindVertexArray(c->vao);
    glDrawArrays(GL_TRIANGLES, 0, c->nverts);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

// Начало прохода пикинга: цель + чистка (цвет под фон, глубина).
static int mesh_pick_begin(RenderCore *rc, const RcView *v, RcTarget *t) {
    (void)v;
    if (!rc || !t || t->backend != RC_BACKEND_GL || !t->fbo) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

// Кадр в цель композитора: тот же рисунок, без swap/шота.
static int mesh_frame_to(RenderCore *rc, const RcView *v, RcTarget *t) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    if (!t || t->backend != 2 || !t->fbo) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    mesh_draw(c, v, t->w, t->h, t->passClear);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

static int mesh_init(RenderCore *rc, void *glfwWindow) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    // НЕ memset'им всё: caps/name/указатели уже стоят из rc_mesh_create.
    c->win = (GLFWwindow *)glfwWindow;
    if (!rhi_gl_load(c->win)) return 0;
    if (!rhi_gl_prog_load(&c->prog, "shaders/mesh/mesh.vert", "shaders/mesh/mesh.frag"))
        return 0;
    // Pick-пара: тот же вершинный, плоский id во фрагменте.
    if (!rhi_gl_prog_load(&c->pick, "shaders/mesh/mesh.vert", "shaders/mesh/pick.frag"))
        return 0;
    c->u_pickMVP = glGetUniformLocation(c->pick.prog, "uMVP");
    c->u_pickId = glGetUniformLocation(c->pick.prog, "uId");
    glGenVertexArrays(1, &c->vao);
    glGenBuffers(1, &c->vbo);
    glBindVertexArray(c->vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, 9 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, 0, 9 * sizeof(float), (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, 0, 9 * sizeof(float), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    mesh_fetch(c);
    // Валидация как в gl-ядре: молчащие падения запрещены.
    glValidateProgram(c->prog.prog);
    int vs = 0;
    glGetProgramiv(c->prog.prog, GL_VALIDATE_STATUS, &vs);
    if (!vs) {
        char l[1024] = {0};
        glGetProgramInfoLog(c->prog.prog, sizeof l, 0, l);
        fprintf(stderr, "mesh: INVALID %s\n", l);
        return 0;
    }
    c->prevT = glfwGetTime();
    c->ready = 1;
    printf("mesh core OK: soup on RHI\n");
    return 1;
}

static void mesh_shutdown(RenderCore *rc) {
    MeshWrap *c = (MeshWrap *)rc->ctx;
    if (c->prog.prog) glDeleteProgram(c->prog.prog);
    if (c->pick.prog) glDeleteProgram(c->pick.prog);
    if (c->vao) glDeleteVertexArrays(1, &c->vao);
    if (c->vbo) glDeleteBuffers(1, &c->vbo);
    free(c->soup);
    c->soup = 0;
}

#ifdef __GNUC__
__attribute__((constructor))
#endif
static void rc_reg_self(void) {
    rc_register("mesh", RC_CAP_MESH, RC_BACKEND_GL, rc_mesh_create);
}

RenderCore *rc_mesh_create(void) {
    MeshWrap *w = (MeshWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_MESH;
    w->api.name = "mesh";
    w->api.backend = RC_BACKEND_GL;
    w->api.init = mesh_init;
    w->api.shutdown = mesh_shutdown;
    w->api.frame = mesh_frame;
    w->api.frame_to = mesh_frame_to;
    w->api.upload_mesh = mesh_upload;
    w->api.pick = mesh_pick;
    w->api.pick_begin = mesh_pick_begin; // однопроход с окклюзией (Б-пик)
    w->api.shader_reload = core_shader_reload;
    w->api.fps = mesh_fps;
    return &w->api;
}
