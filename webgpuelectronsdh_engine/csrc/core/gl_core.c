// Ядро GL (растр): RenderCore API. Меши чанков greedy, пул VAO/VBO.
// Шейдеры, glad и тинт-палитра — из voxel-render БЕЗ копий (ветка noflood).
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
#include "vox/vox_mesh.h"
#include "vox/vox_tex.h"

#define GL_SHADER_DIR "../voxel-render/shaders"
#define GL_MESH_SLOTS 256

typedef struct {
    int cx, cz, used;
    unsigned vao, vbo;
    int count;
} GLMesh;

typedef struct {
    GLFWwindow *win;
    unsigned skyProg;
    unsigned skyVAO;
    unsigned lightProg;
    unsigned tileTex;
    unsigned specTex;
    int tilesReady;
    GLMesh meshes[GL_MESH_SLOTS];
    int ready;
    int frame;
    double prevT;
    float fpsEma;
    // Локации lighting (кэш при ините)
    int u_model, u_view, u_proj, u_lightSpace;
    int u_viewPos, u_fogColor, u_fogRange, u_satU, u_gammaU, u_alphaU, u_uTime;
    int u_skyAmb, u_gndAmb, u_shadowOn;
    int u_dirDirection, u_dirAmbient, u_dirDiffuse, u_dirSpecular;
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

static int core_upload_chunk(RenderCore *rc, int cx, int cz, const uint8_t *vox16,
                             const VoxChunk *nb[6]) {
    GLCore *c = &((GLCoreWrap *)rc->ctx)->core;
    if (!c->ready) return 0;
    // Свой VoxChunk из плоских данных + соседи как есть.
    VoxChunk tmp;
    tmp.cx = cx;
    tmp.cz = cz;
    memcpy(tmp.id, vox16, VOX_N);
    VoxMeshOut m = {0, 0, 0};
    vox_mesh_build(&tmp, nb, &m);
    if (!m.n) return 1; // пусто — нечего грузить, но dirty гасим
    // Слот: точное совпадение, свободный, иначе замена по хэшу.
    int slot = -1;
    for (int i = 0; i < GL_MESH_SLOTS; i++)
        if (c->meshes[i].used && c->meshes[i].cx == cx && c->meshes[i].cz == cz) { slot = i; break; }
    if (slot < 0)
        for (int i = 0; i < GL_MESH_SLOTS; i++)
            if (!c->meshes[i].used) { slot = i; break; }
    if (slot < 0) {
        unsigned h = (unsigned)(cx * 73856093 ^ cz * 19349663);
        slot = (int)(h % GL_MESH_SLOTS);
    }
    GLMesh *g = &c->meshes[slot];
    if (g->used) {
        glDeleteVertexArrays(1, &g->vao);
        glDeleteBuffers(1, &g->vbo);
    }
    glGenVertexArrays(1, &g->vao);
    glGenBuffers(1, &g->vbo);
    glBindVertexArray(g->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)(m.n * sizeof(float)), m.v, GL_STATIC_DRAW);
    // Страйд 12 float: pos3 norm3 uv2 tile ao day night (их layout 1:1).
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, 12 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, 0, 12 * sizeof(float), (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, 0, 12 * sizeof(float), (void *)(6 * sizeof(float)));
    for (int a = 3; a < 7; a++) {
        glVertexAttribPointer((unsigned)a, 1, GL_FLOAT, 0, 12 * sizeof(float), (void *)((5 + a) * sizeof(float)));
        glEnableVertexAttribArray((unsigned)a);
    }
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    g->cx = cx;
    g->cz = cz;
    g->count = (int)(m.n / 12);
    g->used = 1;
    vox_mesh_free(&m);
    return 1;
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
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
    // Глубина небу не нужна, мешам — да.
    glDisable(GL_DEPTH_TEST);
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

    // Меши их lighting-шейдером. Вершины chunk-local, model = смещение чанка.
    // Тени выкл (shadowOn=0), лампы/фонарь выкл (нули) — честный минимум дня.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(c->lightProg);
    glUniformMatrix4fv(c->u_view, 1, 0, view.m);
    glUniformMatrix4fv(c->u_proj, 1, 0, proj.m);
    { Mat4 ident = m4id(); glUniformMatrix4fv(c->u_lightSpace, 1, 0, ident.m); }
    glUniform3f(c->u_viewPos, v->camPos.x, v->camPos.y, v->camPos.z);
    glUniform3f(c->u_fogColor, 0.30f, 0.36f, 0.46f);
    glUniform2f(c->u_fogRange, 0.0f, 260.0f);
    glUniform1f(c->u_satU, 1.1f);
    glUniform1f(c->u_gammaU, 1.2f);
    glUniform1f(c->u_alphaU, 1.0f);
    glUniform1f(c->u_uTime, (float)now);
    glUniform1f(c->u_shadowOn, 0.0f);
    float amb = 0.20f * 3.0f; // их формула при dayF=1: mix(0.03..,0.20,1)*sun.amb
    glUniform3f(c->u_skyAmb, amb * 0.9f, amb * 1.0f, amb * 1.15f);
    glUniform3f(c->u_gndAmb, amb * 0.45f, amb * 0.40f, amb * 0.35f);
    glUniform3f(c->u_dirDirection, -v->sunDir.x, -v->sunDir.y, -v->sunDir.z);
    glUniform3f(c->u_dirAmbient, amb, amb, amb);
    glUniform3f(c->u_dirDiffuse, 1.7f, 1.6f, 1.45f);
    glUniform3f(c->u_dirSpecular, 0.3f, 0.28f, 0.25f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, c->tileTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, c->specTex);
    for (int i = 0; i < GL_MESH_SLOTS; i++) {
        if (!c->meshes[i].used) continue;
        Mat4 model = m4translate((float)(c->meshes[i].cx * 16), 0.0f,
                                 (float)(c->meshes[i].cz * 16));
        glUniformMatrix4fv(c->u_model, 1, 0, model.m);
        glBindVertexArray(c->meshes[i].vao);
        glDrawArrays(GL_TRIANGLES, 0, c->meshes[i].count);
    }
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_CULL_FACE);
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
    c->lightProg = gl_prog(GL_SHADER_DIR "/lighting.vs", GL_SHADER_DIR "/lighting.fs");
    if (!c->lightProg) return 0;
    // Атлас 16x16x7 из тех же тайлов + их тинты (трава #91BD59, листва #77AB2F).
    {
        static uint8_t tiles[VOX_TEXELS * 4];
        if (!vox_tex_load("../voxel-render/texture/tiles", tiles)) {
            memset(tiles, 0x80, sizeof tiles);
            printf("tiles: fallback gray\n");
        }
        for (int i = 0; i < VOX_TILE * VOX_TILE; i++) {
            uint8_t *t0 = &tiles[i * 4]; // grass_top
            t0[0] = (uint8_t)(t0[0] * 145 / 255);
            t0[1] = (uint8_t)(t0[1] * 189 / 255);
            t0[2] = (uint8_t)(t0[2] * 89 / 255);
            uint8_t *t6 = &tiles[(6 * VOX_TILE * VOX_TILE + i) * 4]; // leaves
            t6[0] = (uint8_t)(t6[0] * 119 / 255);
            t6[1] = (uint8_t)(t6[1] * 171 / 255);
            t6[2] = (uint8_t)(t6[2] * 47 / 255);
        }
        glGenTextures(1, &c->tileTex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, c->tileTex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, VOX_TILE, VOX_TILE, VOX_LAYERS,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, tiles);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &c->specTex);
        glBindTexture(GL_TEXTURE_2D, c->specTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
#define GLU(n) c->u_##n = glGetUniformLocation(c->lightProg, #n)
    GLU(model); GLU(view); GLU(lightSpace);
    GLU(viewPos); GLU(fogColor); GLU(fogRange); GLU(satU); GLU(gammaU); GLU(alphaU); GLU(uTime);
    GLU(skyAmb); GLU(gndAmb); GLU(shadowOn);
#undef GLU
    c->u_proj = glGetUniformLocation(c->lightProg, "projection");
    c->u_dirDirection = glGetUniformLocation(c->lightProg, "dirLight.direction");
    c->u_dirAmbient = glGetUniformLocation(c->lightProg, "dirLight.ambient");
    c->u_dirDiffuse = glGetUniformLocation(c->lightProg, "dirLight.diffuse");
    c->u_dirSpecular = glGetUniformLocation(c->lightProg, "dirLight.specular");
    // Сэмплеры раз и навсегда + выкл ламп/фонаря (нули, честный минимум).
    glUseProgram(c->lightProg);
    glUniform1i(glGetUniformLocation(c->lightProg, "material.diffuse"), 0);
    glUniform1i(glGetUniformLocation(c->lightProg, "material.specular"), 1);
    glUniform1f(glGetUniformLocation(c->lightProg, "material.shininess"), 32.0f);
    for (int i = 0; i < 4; i++) {
        char nm[64];
        snprintf(nm, sizeof nm, "pointLights[%d].constant", i);
        glUniform1f(glGetUniformLocation(c->lightProg, nm), 1.0f);
        snprintf(nm, sizeof nm, "pointLights[%d].linear", i);
        glUniform1f(glGetUniformLocation(c->lightProg, nm), 0.22f);
        snprintf(nm, sizeof nm, "pointLights[%d].quadratic", i);
        glUniform1f(glGetUniformLocation(c->lightProg, nm), 0.06f);
        // position/ambient/diffuse/specular — нули по умолчанию, свет не даёт
    }
    glUniform1f(glGetUniformLocation(c->lightProg, "spotLight.constant"), 1.0f);
    glUniform1f(glGetUniformLocation(c->lightProg, "spotLight.linear"), 0.09f);
    glUniform1f(glGetUniformLocation(c->lightProg, "spotLight.quadratic"), 0.032f);
    glUniform1f(glGetUniformLocation(c->lightProg, "spotLight.cutOff"), 0.976f);
    glUniform1f(glGetUniformLocation(c->lightProg, "spotLight.outerCutOff"), 0.966f);
    glUseProgram(0);
    c->ready = 1;
    c->prevT = glfwGetTime();
    printf("gl core OK: sky + meshes\n");
    return 1;
}

static void core_shutdown(RenderCore *rc) {
    GLCore *c = &((GLCoreWrap *)rc->ctx)->core;
    for (int i = 0; i < GL_MESH_SLOTS; i++) {
        if (!c->meshes[i].used) continue;
        glDeleteVertexArrays(1, &c->meshes[i].vao);
        glDeleteBuffers(1, &c->meshes[i].vbo);
        c->meshes[i].used = 0;
    }
    if (c->tileTex) glDeleteTextures(1, &c->tileTex);
    if (c->specTex) glDeleteTextures(1, &c->specTex);
    if (c->lightProg) glDeleteProgram(c->lightProg);
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
