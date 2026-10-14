#ifndef ENGINE_RHI_GL_H
#define ENGINE_RHI_GL_H

#ifdef __cplusplus
extern "C" {
#endif

// RHI-GL: сырой бэкенд OpenGL 4.5. Контекст, шейдеры с логами, UBO.
// Без сцены и контента: только устройство + проверка ошибок словами.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

// Хинты GL 4.5 core до glfwCreateWindow.
void rhi_gl_hints(void);
// Загрузить glad на окне. 1 ок, 0 нет (причина в stderr).
int rhi_gl_load(GLFWwindow *win);
// Программа из файлов VS+FS. 0 при ошибке (лог в stderr, со строками).
unsigned rhi_gl_prog(const char *vsPath, const char *fsPath);
// Программа из текстов в памяти (для eng_shader_reload).
// log/logcap: сюда человеческий лог компиляции/линковки (всегда, и при успехе
// короткое "ok"). Возвращает программу или 0 (старая у вызывателя жива).
unsigned rhi_gl_prog_mem(const char *vsSrc, const char *fsSrc, char *log, int logcap);
// Слот программы с запомненными путями (id для reload = тот же путь).
typedef struct {
    char vs[256];
    char fs[256];
    unsigned prog;
} RhiGlProg;
// Загрузить пару через VFS (vfs_resolve: проект перекрывает сток).
// 0 при ошибке. Пути запоминаются как id.
unsigned rhi_gl_prog_load(RhiGlProg *slot, const char *vsName, const char *fsName);
// Перекомпиляция: id совпал с vs или fs слота (полностью или basename) —
// та сторона берётся из code, вторая из файла. Возвращает НОВУЮ программу
// или 0 (лог всегда). Коммит (swap + refetch uniform + validate) — дело
// ядра: старая при отказе жива, новую при отказе вызыватель не видит
// (helper её уже удалил). Успешную — вызыватель ставит сам и старую удаляет.
unsigned rhi_gl_prog_reload(RhiGlProg *slot, const char *id, const char *code, char *log,
                            int logcap);
// Пустой VAO (фулскрин по gl_VertexID). 0 при ошибке.
unsigned rhi_gl_empty_vao(void);
// Человеческий код последней ошибки GL. "" если чисто.
const char *rhi_gl_err(void);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_RHI_GL_H
