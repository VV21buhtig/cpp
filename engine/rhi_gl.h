#ifndef ENGINE_RHI_GL_H
#define ENGINE_RHI_GL_H
// RHI-GL: сырой бэкенд OpenGL 4.5. Контекст, шейдеры с логами, UBO.
// Без сцены и контента: только устройство + проверка ошибок словами.
// static-функции: включает ядро, которому нужен GL.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Хинты GL 4.5 core до glfwCreateWindow. Возвращает 1.
static int rhi_gl_hints(void) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    return 1;
}

// Загрузить glad на окне. 1 ок, 0 нет (причина в stderr).
static int rhi_gl_load(GLFWwindow *win) {
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

// Программа из файлов VS+FS. 0 при ошибке (лог в stderr, со строками).
static unsigned rhi_gl_prog(const char *vsPath, const char *fsPath) {
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

// Пустой VAO (фулскрин по gl_VertexID). 0 при ошибке.
static unsigned rhi_gl_empty_vao(void) {
    unsigned vao = 0;
    glGenVertexArrays(1, &vao);
    return vao;
}

// Человеческий код последней ошибки GL. "" если чисто.
static const char *rhi_gl_err(void) {
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
#endif
