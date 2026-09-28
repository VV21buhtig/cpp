#ifndef RML_GL_H
#define RML_GL_H

#include <functional>

// RmlUi: собственный RenderInterface на нашем GL 4.5 (без их glad) + ввод с GLFW.
// GL-состояние на время context->Render() берём на себя, после возвращаем.
struct RmlUI {
    bool ok = false;
    bool inputActive = false; // ввод идёт в RML только когда true (пауза)
    bool init(struct GLFWwindow* window);
    void shutdown();
    void setSize(int w, int h);
    // ввод
    void mouseMove(double x, double y);
    void mouseButton(int btn, bool down); // 0=L 1=R 2=M
    void mouseWheel(double y);
    void keyEvent(int glfwKey, bool down, int mods);
    void textInput(unsigned int codepoint);
    // пауза-документ (ui/pause.rml)
    void showPause(bool show);
    void syncPauseValues(); // залить fov/gamma/fog из getSlider в инпуты
    // действия игры (назначает main)
    std::function<void()> onResume, onQuit, onDone;
    std::function<void(const char* id, float v)> onSlider; // s_fov/s_gamma/s_fog
    std::function<float(const char* id)> getSlider;
    void frame(); // Update + Render
};

extern RmlUI gRml;
// Форварды для GLFW-колбэков (no-op если RML неактивен).
void gRmlScroll(double x, double y);
void gRmlKey(int key, int action, int mods);
void gRmlChar(unsigned int codepoint);

#endif
