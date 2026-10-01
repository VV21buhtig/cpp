#ifndef RML_GL_H
#define RML_GL_H

#include <functional>
#include <string>
#include <vector>

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
    // документы: пауза, титульные опции, титул, одиночка
    void showPause(bool show);
    void showOptions(bool show);
    void showTitle(bool show);
    void showSingle(bool show); // ui/single.rml
    void showCreate(bool show); // ui/create.rml
    void showPacks(bool show); // ui/packs.rml
    void showHud(bool show); // ui/hud.rml (игровой хотбар)
    void setHudSlot(int i); // рамка выбора 0..8
    void showInv(bool show); // ui/inv.rml (креатив-инвентарь)
    void setHudIcon(int slot, const char* src); // иконка слота (src перезагружается)
    void refreshSingle(const std::vector<std::string>& worlds, int sel);
    void selectSingleRow(int oldN, int newN);
    void setWInfo(const std::string& t);
    void setImportVisible(bool show);
    std::string getSingleText(const char* id);
    void setSingleText(const char* id, const std::string& t);
    void setSingleInner(const char* id, const std::string& t);
    std::string getCreateText(const char* id);
    void setCreateText(const char* id, const std::string& t);
    void setCreateInner(const char* id, const std::string& t);
    // Survival-инвентарь: вид слота для syncInv.
    struct InvView { unsigned char id = 0; int n = 0; const char* src = ""; };
    void syncInv(const InvView store[27], const InvView hot[9], int lifted);
    std::string getPacksText(const char* id);
    void setPacksText(const char* id, const std::string& t);
    void setPacksInner(const char* id, const std::string& t);
    void syncPauseValues(); // залить значения в открытый RML-документ
    void setScale(float s); // GUI Scale: dp->px (MC: мелкий/обычный/крупный)
    void dump(); // диагностика в stdout: размеры, видимость, геометрия #menu
    // действия игры (назначает main)
    std::function<void()> onResume, onQuit, onDone, onDoneTitle;
    std::function<void(const char* id, float v)> onSlider; // s_fov/s_gamma/s_fog
    std::function<float(const char* id)> getSlider;
    std::function<void(const char* id)> onCycle; // b_dist/b_filter/... кнопки-циклы
    std::function<std::string(const char* id)> getLabel; // текст для кнопок-циклов
    std::function<void(const char* id)> onAction; // прочие клики титульных доков (t_*)
    std::function<void(int g)> onInvDown; // mousedown по слоту (поднять)
    std::function<void()> onInvUp; // mouseup (бросить; слот резолвится по координатам)
    int pickInvSlot(double x, double y); // хит-тест слотов inv-doc, -1 мимо
    void setCarryIcon(const char* src); // иконка-призрак (null = спрятать)
    void moveCarry(double x, double y); // позиция призрака (px экрана)
    void frame(); // Update + Render
};

extern RmlUI gRml;
// Форварды для GLFW-колбэков (no-op если RML неактивен).
void gRmlScroll(double x, double y);
void gRmlKey(int key, int action, int mods);
void gRmlChar(unsigned int codepoint);

#endif
