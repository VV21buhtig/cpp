// Тест RHI: GL-контекст + компиляция шейдера, VK-девайс + свопчейн.
// Без контента: только устройство. Окна маленькие видимые (как все тесты).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include "rhi_gl.h"
#include "rhi_vk.h"

int main(void) {
    int bad = 0;
    if (!glfwInit()) { printf("RHI FAIL: glfw\n"); return 1; }
    // --- GL ---
    {
        rhi_gl_hints();
        GLFWwindow *win = glfwCreateWindow(64, 64, "rhi_gl", 0, 0);
        if (!win) { printf("RHI FAIL: gl window\n"); bad = 1; }
        else {
            if (!rhi_gl_load(win)) { printf("RHI FAIL: gl load\n"); bad = 1; }
            else {
                // Минимальные шейдеры через файлы (как ядра грузят).
                FILE *f = fopen("/tmp/opencode/rhi_t.vert", "w");
                if (f) { fputs("#version 450\nvoid main(){gl_Position=vec4(0);}\n", f); fclose(f); }
                f = fopen("/tmp/opencode/rhi_t.frag", "w");
                if (f) { fputs("#version 450\nlayout(location=0) out vec4 o;\nvoid main(){o=vec4(1);}\n", f); fclose(f); }
                unsigned p = rhi_gl_prog("/tmp/opencode/rhi_t.vert", "/tmp/opencode/rhi_t.frag");
                printf("gl prog=%u err=%s (want nonzero, empty)\n", p, rhi_gl_err());
                if (!p || rhi_gl_err()[0]) { printf("RHI FAIL: gl prog\n"); bad = 1; }
                if (p) glDeleteProgram(p);
                unsigned vao = rhi_gl_empty_vao();
                if (!vao) { printf("RHI FAIL: vao\n"); bad = 1; }
                else glDeleteVertexArrays(1, &vao);
                printf("gl: %s\n", (const char *)glGetString(GL_RENDERER));
            }
            glfwDestroyWindow(win);
        }
    }
    // --- VK ---
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow *win = glfwCreateWindow(64, 64, "rhi_vk", 0, 0);
        if (!win) { printf("RHI FAIL: vk window\n"); bad = 1; }
        else {
            RhiVk r;
            if (!rhi_vk_init(&r, win, 0, 0)) { printf("RHI FAIL: vk init\n"); bad = 1; }
            else {
                printf("vk: %s swap=%u fmt=%d\n", r.devName, r.imgN, r.imgFmt);
                uint32_t img = 0;
                int got = rhi_vk_acquire(&r, 0, &img);
                printf("acquire=%d (want >=0)\n", got);
                if (got < 0) { printf("RHI FAIL: acquire\n"); bad = 1; }
                rhi_vk_shutdown(&r);
            }
            glfwDestroyWindow(win);
        }
    }
    glfwTerminate();
    printf(bad ? "RHI FAIL\n" : "RHI OK\n");
    return bad;
}
