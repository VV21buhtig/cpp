// RHI-GL: мясной бэкенд. C11. Ошибки — словами и со строками шейдеров.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rhi_gl.h"
#include "vfs.h"

void rhi_gl_hints(void) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

int rhi_gl_load(GLFWwindow *win) {
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) {
        fprintf(stderr, "rhi_gl: glad fail\n");
        return 0;
    }
    if (!GLAD_GL_VERSION_4_5) {
        fprintf(stderr, "rhi_gl: need GL 4.5\n");
        return 0;
    }
    return 1;
}

static char *rhi_gl_read(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
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

unsigned rhi_gl_prog_mem(const char *vsSrc, const char *fsSrc, char *log, int logcap) {
    if (log && logcap > 0) log[0] = 0;
    if (!vsSrc || !fsSrc) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "пустой исходник");
        return 0;
    }
    unsigned v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vsSrc, 0);
    glCompileShader(v);
    int ok = 0;
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[2048];
        glGetShaderInfoLog(v, sizeof l, 0, l);
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "vs: %s", l);
        glDeleteShader(v);
        return 0;
    }
    unsigned f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fsSrc, 0);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[2048];
        glGetShaderInfoLog(f, sizeof l, 0, l);
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "fs: %s", l);
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char l[2048];
        glGetProgramInfoLog(p, sizeof l, 0, l);
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "link: %s", l);
        glDeleteProgram(p);
        return 0;
    }
    if (log && logcap > 0) snprintf(log, (size_t)logcap, "ok");
    return p;
}

static const char *rhi_base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static int rhi_id_match(const char *id, const char *stored) {
    if (!id || !stored) return 0;
    if (!strcmp(id, stored)) return 1;
    return !strcmp(id, rhi_base(stored)) || !strcmp(rhi_base(id), rhi_base(stored));
}

unsigned rhi_gl_prog_load(RhiGlProg *slot, const char *vsName, const char *fsName) {
    if (!slot) return 0;
    char vs[256], fs[256];
    if (!vfs_resolve(vsName, vs, sizeof vs) || !vfs_resolve(fsName, fs, sizeof fs)) return 0;
    unsigned p = rhi_gl_prog(vs, fs);
    if (!p) return 0;
    strncpy(slot->vs, vs, sizeof slot->vs - 1);
    slot->vs[sizeof slot->vs - 1] = 0;
    strncpy(slot->fs, fs, sizeof slot->fs - 1);
    slot->fs[sizeof slot->fs - 1] = 0;
    slot->prog = p;
    return p;
}

unsigned rhi_gl_prog_reload(RhiGlProg *slot, const char *id, const char *code, char *log,
                            int logcap) {
    if (!slot || !slot->prog || !id || !code) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "нет слота/id/кода");
        return 0;
    }
    int isVs = rhi_id_match(id, slot->vs);
    int isFs = !isVs && rhi_id_match(id, slot->fs);
    if (!isVs && !isFs) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "id не нашего слота");
        return 0;
    }
    // Вторая сторона — из файла (VFS), как при загрузке.
    char *other = rhi_gl_read(isVs ? slot->fs : slot->vs);
    if (!other) {
        if (log && logcap > 0) snprintf(log, (size_t)logcap, "вторая сторона не читается");
        return 0;
    }
    char lmem[2048] = {0};
    unsigned np = rhi_gl_prog_mem(isVs ? code : other, isVs ? other : code, lmem, sizeof lmem);
    free(other);
    if (log && logcap > 0) {
        strncpy(log, lmem, (size_t)logcap - 1);
        log[logcap - 1] = 0;
    }
    if (!np) return 0; // старая жива, новую helper уже удалил
    return np;
}

unsigned rhi_gl_prog(const char *vsPath, const char *fsPath) {
    char *vsc = rhi_gl_read(vsPath), *fsc = rhi_gl_read(fsPath);
    if (!vsc || !fsc) {
        fprintf(stderr, "rhi_gl: no shader %s / %s\n",
                vsc ? "?" : vsPath, fsc ? "?" : fsPath);
        free(vsc);
        free(fsc);
        return 0;
    }
    unsigned v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, (const char **)&vsc, 0);
    glCompileShader(v);
    int ok = 0;
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[2048];
        glGetShaderInfoLog(v, sizeof l, 0, l);
        fprintf(stderr, "rhi_gl vs %s: %s\n", vsPath, l);
    }
    unsigned f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, (const char **)&fsc, 0);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[2048];
        glGetShaderInfoLog(f, sizeof l, 0, l);
        fprintf(stderr, "rhi_gl fs %s: %s\n", fsPath, l);
    }
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char l[2048];
        glGetProgramInfoLog(p, sizeof l, 0, l);
        fprintf(stderr, "rhi_gl link: %s\n", l);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    free(vsc);
    free(fsc);
    return p;
}

unsigned rhi_gl_empty_vao(void) {
    unsigned vao = 0;
    glGenVertexArrays(1, &vao);
    return vao;
}

const char *rhi_gl_err(void) {
    unsigned e = glGetError();
    if (!e) return "";
    if (e == 0x0500) return "INVALID_ENUM";
    if (e == 0x0501) return "INVALID_VALUE";
    if (e == 0x0502) return "INVALID_OPERATION";
    if (e == 0x0503) return "STACK_OVERFLOW";
    if (e == 0x0504) return "STACK_UNDERFLOW";
    if (e == 0x0505) return "OUT_OF_MEMORY";
    return "UNKNOWN";
}
