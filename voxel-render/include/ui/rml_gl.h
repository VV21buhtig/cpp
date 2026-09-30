#ifndef RML_GL_H
#define RML_GL_H

#include <functional>
#include <string>

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
    // пауза-документ (ui/pause.rml) и титульные опции (ui/options.rml)
    void showPause(bool show);
    void showOptions(bool show);
    void syncPauseValues(); // залить значения в открытый RML-документ
    void setScale(float s); // GUI Scale: dp->px (MC: мелкий/обычный/крупный)
    void dump(); // диагностика в stdout: размеры, видимость, геометрия #menu
    // действия игры (назначает main)
    std::function<void()> onResume, onQuit, onDone, onDoneTitle;
    std::function<void(const char* id, float v)> onSlider; // s_fov/s_gamma/s_fog
    std::function<float(const char* id)> getSlider;
    std::function<void(const char* id)> onCycle; // b_dist/b_filter/... кнопки-циклы
    std::function<std::string(const char* id)> getLabel; // текст для кнопок-циклов
    void frame(); // Update + Render
};

extern RmlUI gRml;
// Форварды для GLFW-колбэков (no-op если RML неактивен).
void gRmlScroll(double x, double y);
void gRmlKey(int key, int action, int mods);
void gRmlChar(unsigned int codepoint);

#endif
