// Glad 2 — инклуд <glad/gl.h>, и он ДО GLFW
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "engine/texture.h"
#include "engine/chunk.h"
#include "engine/world.h"
#include "engine/save.h"
#include "engine/cvar.h"
#include "engine/frustum.h"
#include "engine/audio.h"
#include "game/player.h"
#include "game/console.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ui/rml_gl.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <iostream>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include "shader.h"
#include "camera.h"
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cfloat>

// ---- MC-style меню: тень текста, кнопки, фон, логотип ----
static void MCShadowText(ImDrawList* d, ImFont* f, float size, ImVec2 p, ImU32 col, const char* txt) {
    d->AddText(f, size, ImVec2(p.x + 2, p.y + 2), IM_COL32(63, 63, 63, 255), txt);
    d->AddText(f, size, p, col, txt);
}
static void MCTitle(ImDrawList* d, ImFont* f, float size, const char* txt, float ww, float y) {
    if (!f) return;
    ImVec2 ts = f->CalcTextSizeA(size, 10000.0f, 0, txt);
    MCShadowText(d, f, size, ImVec2((ww - ts.x) * 0.5f, y), IM_COL32(200, 200, 200, 255), txt);
}
static ImU32 MCBoost(ImU32 c, int b) {
    if (b <= 0) return c;
    int r = (c & 0xFF) + b; if (r > 255) r = 255;
    int g = ((c >> 8) & 0xFF) + b; if (g > 255) g = 255;
    int bl = ((c >> 16) & 0xFF) + b; if (bl > 255) bl = 255;
    return IM_COL32(r, g, bl, 255);
}
// Кнопка в духе MC Beta: серая с градиентом, чёрная рамка, жёлтый текст при наведении.
// boost осветляет (кнопки паузы поверх затемнённого мира).
static bool MCButton(const char* id, const char* label, ImVec2 size, ImFont* f, float fsize, bool enabled = true, int boost = 0) {
    ImGui::PushID(id);
    ImGui::InvisibleButton("##mc", size);
    bool clicked = enabled && ImGui::IsItemClicked();
    bool hov = enabled && ImGui::IsItemHovered();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImU32 cTop = MCBoost(!enabled ? IM_COL32(85, 85, 85, 255) : (hov ? IM_COL32(160, 168, 225, 255) : IM_COL32(150, 150, 150, 255)), boost);
    ImU32 cBot = MCBoost(!enabled ? IM_COL32(65, 65, 65, 255) : (hov ? IM_COL32(110, 118, 175, 255) : IM_COL32(105, 105, 105, 255)), boost);
    d->AddRectFilledMultiColor(a, b, cTop, cTop, cBot, cBot);
    d->AddRect(a, b, IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);
    d->AddLine(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, a.y + 2), IM_COL32(255, 255, 255, 70));
    if (f) {
        ImVec2 ts = f->CalcTextSizeA(fsize, 10000.0f, 0, label);
        ImVec2 tp(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f);
        ImU32 tc = !enabled ? IM_COL32(160, 160, 160, 255) : (hov ? IM_COL32(255, 255, 160, 255) : IM_COL32(255, 255, 255, 255));
        MCShadowText(d, f, fsize, tp, tc, label);
    }
    ImGui::PopID();
    return clicked;
}
// Слайдеры без «читерской» синевы: тёмный трек, серый ползунок.
static void MCPushSliderStyle() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.04f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.10f, 0.10f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.10f, 0.10f, 0.10f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
}
static void MCPopSliderStyle() { ImGui::PopStyleColor(5); }
// Фон: тайленная земля + затемнение.
static void MCDirtBG(unsigned int tex, int ww, int hh) {
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    const float T = 64.0f;
    for (float y = 0; y < (float)hh; y += T)
        for (float x = 0; x < (float)ww; x += T)
            d->AddImage((ImTextureID)(intptr_t)tex, ImVec2(x, y), ImVec2(x + T, y + T));
    d->AddRectFilled(ImVec2(0, 0), ImVec2((float)ww, (float)hh), IM_COL32(0, 0, 0, 140));
}
// Логотип: изометрический куб травы + RENDOR.
static void MCLogo(ImDrawList* d, ImFont* f, float fsize, unsigned int tTop, unsigned int tSide, ImVec2 origin, float width) {
    const char* txt = "RENDOR";
    ImVec2 ts = f->CalcTextSizeA(fsize, 10000.0f, 0, txt);
    float u = fsize * 0.72f;
    float gap = 24.0f;
    float x0 = origin.x + (width - (u * 2 + gap + ts.x)) * 0.5f;
    float cy = origin.y;
    ImVec2 T(x0 + u, cy), L(x0, cy + u * 0.5f), C(x0 + u, cy + u), R(x0 + 2 * u, cy + u * 0.5f);
    ImVec2 BL(x0, cy + u * 1.5f), BC(x0 + u, cy + 2 * u), BR(x0 + 2 * u, cy + u * 1.5f);
    d->AddImageQuad((ImTextureID)(intptr_t)tTop, T, R, C, L,
                    ImVec2(0.5f, 1), ImVec2(1, 0.5f), ImVec2(0.5f, 0), ImVec2(0, 0.5f),
                    IM_COL32(145, 189, 89, 255)); // grass_top ч/б — plains-tint #91BD59 как в атласе
    d->AddImageQuad((ImTextureID)(intptr_t)tSide, L, C, BC, BL,
                    ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0), IM_COL32(150, 150, 150, 255));
    d->AddImageQuad((ImTextureID)(intptr_t)tSide, C, R, BR, BC,
                    ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0), IM_COL32(205, 205, 205, 255));
    ImVec2 tp(x0 + u * 2 + gap, cy + u - ts.y * 0.5f);
    ImU32 out = IM_COL32(40, 40, 40, 255);
    d->AddText(f, fsize, ImVec2(tp.x - 3, tp.y), out, txt);
    d->AddText(f, fsize, ImVec2(tp.x + 3, tp.y), out, txt);
    d->AddText(f, fsize, ImVec2(tp.x, tp.y - 3), out, txt);
    d->AddText(f, fsize, ImVec2(tp.x, tp.y + 3), out, txt);
    MCShadowText(d, f, fsize, tp, IM_COL32(255, 255, 255, 255), txt);
}

Camera camera(glm::vec3(8.0f, 6.0f, 14.0f));
GameConsole console;
bool gPaused = false; // пауза игры (ESC): тик стоит, поверх — MC-меню
CVarSys* gCvar = nullptr;
float lastX = 640.0f, lastY = 360.0f;
bool  firstMouse = true;
float deltaTime = 0.0f, lastFrame = 0.0f;

void framebuffer_size_callback(GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); }
void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (console.open || gPaused) { firstMouse = true; return; }
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xo = (float)xpos - lastX, yo = lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    camera.ProcessMouseMovement(xo, yo);
}
void scroll_callback(GLFWwindow*, double x, double y) {
    gRmlScroll(x, y); // ImGui уже получил событие раньше нас по цепочке GLFW->ImGui->мы
}
void key_callback(GLFWwindow*, int key, int, int action, int mods) {
    gRmlKey(key, action, mods);
}
void char_callback(GLFWwindow*, unsigned int c) {
    gRmlChar(c);
}

static std::vector<std::string> listDirs(const char* path) {
    std::vector<std::string> out;
    DIR* dp = opendir(path);
    if (dp) {
        struct dirent* e;
        while ((e = readdir(dp))) {
            if (e->d_name[0] == '.') continue;
            bool isDir = (e->d_type == DT_DIR || e->d_type == DT_LNK);
            if (e->d_type == DT_UNKNOWN) {
                char full[1024];
                snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
                DIR* t = opendir(full);
                isDir = (t != nullptr);
                if (t) closedir(t);
            }
            if (isDir) out.push_back(e->d_name);
        }
        closedir(dp);
    }
    return out;
}

static bool hasFiles(const std::string& dir, std::initializer_list<const char*> rel, std::string& miss) {
    for (auto r : rel) {
        FILE* f = fopen((dir + "/" + r).c_str(), "rb");
        if (!f) { miss = r; return false; }
        fclose(f);
    }
    return true;
}

// Папка как пак: symlink в targetDir/<basename>. Возвращает имя или "".
static std::string linkPack(const char* targetDir, const char* srcPath) {
    std::string src = srcPath;
    while (!src.empty() && src.back() == '/') src.pop_back();
    size_t p = src.find_last_of('/');
    std::string name = (p == std::string::npos) ? src : src.substr(p + 1);
    if (name.empty()) return "";
    std::string dst = std::string(targetDir) + "/" + name;
    FILE* probe = fopen(dst.c_str(), "rb");
    if (probe) fclose(probe);
    DIR* dd = opendir(dst.c_str());
    if (probe || dd) { if (dd) closedir(dd); return ""; } // занято
    if (symlink(src.c_str(), dst.c_str()) != 0) return "";
    return name;
}

static std::vector<std::string> listWorlds() {    std::vector<std::string> out;
    DIR* dp = opendir("worlds");
    if (dp) {
        struct dirent* e;
        while ((e = readdir(dp))) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.substr(n.size() - 4) == ".bin")
                out.push_back(n.substr(0, n.size() - 4));
        }
        closedir(dp);
    }
    return out;
}

int main()
{
    if (!glfwInit()) { std::cerr << "GLFW fail\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Rendor", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // в меню курсор свободный

    if (gladLoadGL(glfwGetProcAddress) == 0) { glfwTerminate(); return -1; }
    std::cout << "RENDERER: " << glGetString(GL_RENDERER) << "\n";

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    // ---- cvars ----
    CVarSys cvar;
    gCvar = &cvar;
    cvar.reg("sun.i", 1.0f);
    cvar.reg("sun.amb", 3.0f);
    cvar.reg("sun.sat", 1.8f);
    cvar.reg("sun.gamma", 1.2f);
    cvar.reg("time.speed", 600.0f);
    cvar.reg("fog.near", 0.0f);
    cvar.reg("fog.far", 260.0f);
    cvar.reg("cam.fov", 70.0f);
    cvar.reg("move.walk", 4.3f);
    cvar.reg("move.fly", 8.0f);
    cvar.reg("move.jump", 7.5f);
    cvar.reg("move.bhop", 0.0f);
    cvar.reg("move.step", 1.0f);
    cvar.reg("move.step_h", 1.0f);
    cvar.reg("tick.rate", 120.0f);
    cvar.reg("vid.w", 1280.0f);
    cvar.reg("vid.h", 720.0f);
    cvar.reg("vid.fullscreen", 0.0f);
    cvar.reg("vid.vsync", 1.0f);
    cvar.reg("view.dist", 4.0f);
    cvar.reg("gfx.filter", 0.0f);
    cvar.reg("gfx.fxaa", 0.0f);
    cvar.reg("ui.rml", 1.0f); // пауза через RmlUi (0 = старый MC-оверлей)
    cvar.reg("snd.vol", 0.8f);
    cvar.reg("snd.on", 1.0f);
    cvar.load("gfx.cfg");
    // ---- video: размер/фулскрин/vsync из cvar, применяется живо ----
    auto applyVideo = [&]() {
        int vw = (int)cvar.get("vid.w", 1280.0f);
        int vh = (int)cvar.get("vid.h", 720.0f);
        if (vw < 640) vw = 640; if (vh < 360) vh = 360;
        if (vw > 3840) vw = 3840; if (vh > 2160) vh = 2160;
        if (cvar.get("vid.fullscreen", 0.0f) > 0.5f) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            glfwSetWindowMonitor(window, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        } else {
            glfwSetWindowMonitor(window, nullptr, 100, 100, vw, vh, GLFW_DONT_CARE);
        }
        glfwSwapInterval(cvar.get("vid.vsync", 1.0f) > 0.5f ? 1 : 0);
    };
    applyVideo();
    cvar.onPrint = [](const std::string& s) { console.print(s); };
    console.print("console F1. try: set sun.i 2");
    std::cout << "console: F1 in game, or stdin+Enter. Try: set sun.i 2\n";
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    std::string conLine;
    std::function<void(const std::string&)> runLine = [&](const std::string& s) { cvar.exec(s); };

    // ---- packs ----
    std::vector<std::string> packNames = {"default"};
    for (auto& d : listDirs("texture/packs")) packNames.push_back(d);
    std::vector<std::string> shaderPacks = {"default"};
    for (auto& d : listDirs("shaders/packs")) shaderPacks.push_back(d);

    mkdir("worlds", 0755); // сейвы должны куда-то писаться (CWD=build/)
    AudioSys audio;
    if (!audio.init()) std::cout << "audio: no device, muted\n";
    audio.setMaster(cvar.get("snd.vol", 0.8f));
    if (!gBlocks.load("blocks.json")) console.print("blocks.json missing/invalid, defaults\n");
    if (!gRml.init(window)) console.print("rml init failed, old pause\n");

    // ================= MENU (MC-style) =================
    enum MenuScr { M_MAIN, M_SINGLE, M_CREATE, M_OPTIONS, M_PACKS };
    MenuScr scr = M_MAIN;
    std::string playPath;   // worlds/<name>.bin
    int playCX = 16, playCZ = 16, playSeed = 1337;
    bool playNew = false, wantQuit = false;
    static char newName[64] = "myworld";
    int newSizeIdx = 1; // 0:8 1:16 2:24
    int newSeed = 1337;
    int menuWorldSel = 0, menuShaderSel = 0, menuPackIdx = 0;
    bool delArm = false; // удаление мира: первое нажатие ставит на взвод
    const int sizes[3] = {8, 16, 24};
    const char* sizeNames[3] = {"Small 8x8", "Normal 16x16", "Large 24x24"};

    // Шрифт меню (Monocraft, OFL) + текстуры меню. CWD=build/, fonts/ копируется пост-билдом.
    ImFont* fontUI = nullptr, *fontLogo = nullptr;
    {
        ImGuiIO& mio = ImGui::GetIO();
        fontUI = mio.Fonts->AddFontFromFileTTF("fonts/Monocraft.ttf", 20.0f, nullptr, mio.Fonts->GetGlyphRangesCyrillic());
        fontLogo = mio.Fonts->AddFontFromFileTTF("fonts/Monocraft.ttf", 64.0f, nullptr, mio.Fonts->GetGlyphRangesCyrillic());
        if (!fontUI) {
            mio.Fonts->AddFontDefault();
            std::cout << "menu: fonts/Monocraft.ttf missing, fallback\n";
        }
    }
    unsigned int texDirt = loadTexture("texture/tiles/dirt.png");
    unsigned int texGTop = loadTexture("texture/tiles/grass_top.png");
    unsigned int texGSide = loadTexture("texture/tiles/grass_side.png");
    for (unsigned int t : {texDirt, texGTop, texGSide}) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
title_screen:
    // Возврат из игры (Save and Quit to Title): сброс выбора мира.
    playPath.clear();
    playNew = false;
    scr = M_MAIN;
    delArm = false;
    menuWorldSel = 0;
    audio.wind(false);
    while (!wantQuit && playPath.empty()) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) { wantQuit = true; break; }
        int ww, hh;
        glfwGetFramebufferSize(window, &ww, &hh);
        glViewport(0, 0, ww, hh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        MCDirtBG(texDirt, ww, hh);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)ww, (float)hh));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("menu", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        if (fontUI) ImGui::PushFont(fontUI);
        ImDrawList* md = ImGui::GetWindowDrawList();
        const float bw = 400.0f, bh = 36.0f, gap = 8.0f;
        const float fs = fontUI ? 20.0f : 13.0f;
        const float cx = ((float)ww - bw) * 0.5f;

        if (scr == M_MAIN) {
            MCLogo(md, fontLogo ? fontLogo : ImGui::GetFont(), fontLogo ? 64.0f : 40.0f,
                   texGTop, texGSide, ImVec2(0, (float)hh * 0.10f), (float)ww);
            float y = (float)hh * 0.10f + 2 * (fontLogo ? 64.0f : 40.0f) * 0.72f + 48.0f;
            ImGui::SetCursorPos(ImVec2(cx, y));
            if (MCButton("m_single", "Singleplayer", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); scr = M_SINGLE; delArm = false; }
            y += bh + gap;
            float bw2 = (bw - gap) * 0.5f;
            ImGui::SetCursorPos(ImVec2(cx, y));
            if (MCButton("m_opt", "Options...", ImVec2(bw2, bh), fontUI, fs)) { audio.playUI(); scr = M_OPTIONS; }
            ImGui::SetCursorPos(ImVec2(cx + bw2 + gap, y));
            if (MCButton("m_quit", "Quit Game", ImVec2(bw2, bh), fontUI, fs)) { wantQuit = true; }
        } else if (scr == M_SINGLE) {
            MCTitle(md, fontUI, fs + 4.0f, "Select World", (float)ww, 24.0f);
            std::vector<std::string> worlds = listWorlds();
            if (menuWorldSel >= (int)worlds.size()) { menuWorldSel = 0; delArm = false; }
            float listW = (float)ww < 660.0f ? (float)ww - 40.0f : 620.0f;
            float listH = (float)hh * 0.42f;
            ImGui::SetCursorPos(ImVec2(((float)ww - listW) * 0.5f, 64.0f));
            ImGui::BeginChild("worlds", ImVec2(listW, listH), true);
            for (size_t i = 0; i < worlds.size(); i++)
                if (ImGui::Selectable(worlds[i].c_str(), (int)i == menuWorldSel)) { menuWorldSel = (int)i; delArm = false; }
            ImGui::EndChild();
            float y = 64.0f + listH + 16.0f;
            bool has = !worlds.empty();
            ImGui::SetCursorPos(ImVec2(cx, y));
            if (MCButton("s_play", "Play Selected World", ImVec2(bw, bh), fontUI, fs, has)) {
                audio.playUI();
                playPath = "worlds/" + worlds[menuWorldSel] + ".bin";
                playNew = false;
            }
            y += bh + gap;
            ImGui::SetCursorPos(ImVec2(cx, y));
            if (MCButton("s_create", "Create New World", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); scr = M_CREATE; }
            y += bh + gap;
            ImGui::SetCursorPos(ImVec2(cx, y));
            if (MCButton("s_del", delArm ? "Really delete?" : "Delete", ImVec2(bw, bh), fontUI, fs, has)) {
                if (!delArm) { delArm = true; audio.playUI(); }
                else {
                    remove(("worlds/" + worlds[menuWorldSel] + ".bin").c_str());
                    menuWorldSel = 0; delArm = false;
                }
            }
            y += bh + gap;
            if (has) {
                int icx = 0, icz = 0, iseed = -1;
                std::string ipath = "worlds/" + worlds[menuWorldSel] + ".bin";
                if (readWorldInfo(ipath.c_str(), icx, icz, iseed)) {
                    std::string info = "Size: " + std::to_string(icx) + "x" + std::to_string(icz) +
                                       "  Seed: " + (iseed < 0 ? std::string("?") : std::to_string(iseed));
                    ImGui::SetCursorPos(ImVec2(cx, y));
                    ImGui::TextUnformatted(info.c_str());
                    y += 28.0f;
                    static char renName[64] = "";
                    ImGui::SetCursorPos(ImVec2(cx, y)); ImGui::SetNextItemWidth(bw - 170.0f);
                    ImGui::InputText("##wrename", renName, sizeof(renName));
                    ImGui::SetCursorPos(ImVec2(cx + bw - 160.0f, y - 2.0f));
                    if (MCButton("s_rename", "Rename", ImVec2(160, 32), fontUI, fs, renName[0] != 0)) {
                        std::string np = std::string("worlds/") + renName + ".bin";
                        FILE* ex = fopen(np.c_str(), "rb");
                        if (ex) { fclose(ex); console.print("rename: name taken\n"); }
                        else if (rename(ipath.c_str(), np.c_str()) == 0) {
                            audio.playUI(); worlds[menuWorldSel] = renName; renName[0] = 0;
                        } else console.print("rename FAILED\n");
                    }
                    y += 42.0f;
                    ImGui::SetCursorPos(ImVec2(cx, y));
                    if (MCButton("s_seed", "New World with This Seed", ImVec2(bw, bh), fontUI, fs, iseed >= 0)) {
                        audio.playUI(); newSeed = iseed; scr = M_CREATE;
                    }
                    y += bh + gap;
                }
            }
            FILE* lf = fopen("world.bin", "rb");
            if (lf) {
                fclose(lf);
                ImGui::SetCursorPos(ImVec2(cx, y));
                if (MCButton("s_import", "Import legacy world.bin", ImVec2(bw, bh), fontUI, fs)) {
                    audio.playUI();
                    playPath = "worlds/imported.bin";
                    playNew = false;
                    playCX = 16; playCZ = 16;
                    newSeed = -2; // флаг: грузить из world.bin
                }
                y += bh + gap;
            }
            ImGui::SetCursorPos(ImVec2(cx, y));
            if (MCButton("s_cancel", "Cancel", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); scr = M_MAIN; delArm = false; }
        } else if (scr == M_CREATE) {
            MCTitle(md, fontUI, fs + 4.0f, "Create New World", (float)ww, 24.0f);
            float fx = cx, fy = 84.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::Text("World Name:");
            fy += 28.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw);
            ImGui::InputText("##wname", newName, sizeof(newName));
            fy += 44.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::Text("World Size:");
            fy += 28.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("c_size", sizeNames[newSizeIdx], ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); newSizeIdx = (newSizeIdx + 1) % 3; }
            fy += bh + 16.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::Text("Seed:");
            fy += 28.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw - 170.0f);
            ImGui::InputInt("##wseed", &newSeed);
            ImGui::SetCursorPos(ImVec2(fx + bw - 160.0f, fy - 2.0f));
            if (MCButton("c_rand", "Random", ImVec2(160, 32), fontUI, fs)) newSeed = rand();
            fy += 52.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("c_go", "Create New World", ImVec2(bw, bh), fontUI, fs, newName[0] != 0)) {
                audio.playUI();
                playPath = std::string("worlds/") + newName + ".bin";
                playCX = sizes[newSizeIdx]; playCZ = sizes[newSizeIdx];
                playSeed = newSeed;
                playNew = true;
            }
            fy += bh + gap;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("c_cancel", "Cancel", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); scr = M_SINGLE; }
        } else if (scr == M_OPTIONS) {
            MCTitle(md, fontUI, fs + 4.0f, "Options...", (float)ww, 24.0f);
            float fx = cx, fy = 84.0f;
            MCPushSliderStyle();
            float v = cvar.get("cam.fov", 70.0f);
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw);
            if (ImGui::SliderFloat("FOV", &v, 30.0f, 110.0f)) cvar.set("cam.fov", v);
            fy += 40.0f;
            v = cvar.get("sun.gamma", 1.2f);
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw);
            if (ImGui::SliderFloat("Gamma", &v, 0.5f, 4.0f)) cvar.set("sun.gamma", v);
            fy += 40.0f;
            v = cvar.get("fog.far", 260.0f);
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw);
            if (ImGui::SliderFloat("Fog distance", &v, 50.0f, 500.0f)) cvar.set("fog.far", v);
            MCPopSliderStyle();
            fy += 52.0f;
            // --- video: дистанция/фильтрация/vsync/разрешение/фулскрин, всё живо ---
            static const int dists[] = {2, 4, 6, 8, 12};
            int vd = (int)cvar.get("view.dist", 4.0f);
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_dist", ("Render Distance: " + std::to_string(vd)).c_str(), ImVec2(bw, bh), fontUI, fs)) {
                audio.playUI();
                int ni = 0;
                for (int i = 0; i < 5; i++) if (dists[i] == vd) ni = (i + 1) % 5;
                cvar.set("view.dist", (float)dists[ni]);
            }
            fy += bh + gap;
            static const char* fnames[] = {"Nearest", "Bilinear", "Trilinear", "Aniso 8x"};
            int fm = (int)cvar.get("gfx.filter", 0.0f); if (fm < 0) fm = 0; if (fm > 3) fm = 3;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_filter", ("Filtering: " + std::string(fnames[fm])).c_str(), ImVec2(bw, bh), fontUI, fs)) {
                audio.playUI(); cvar.set("gfx.filter", (float)((fm + 1) % 4));
            }
            fy += bh + gap;
            bool vs = cvar.get("vid.vsync", 1.0f) > 0.5f;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_vsync", (std::string("VSync: ") + (vs ? "ON" : "OFF")).c_str(), ImVec2(bw, bh), fontUI, fs)) {
                audio.playUI(); cvar.set("vid.vsync", vs ? 0.0f : 1.0f); applyVideo();
            }
            fy += bh + gap;
            int vw = (int)cvar.get("vid.w", 1280.0f), vh = (int)cvar.get("vid.h", 720.0f);
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_res", ("Resolution: " + std::to_string(vw) + "x" + std::to_string(vh)).c_str(), ImVec2(bw, bh), fontUI, fs)) {
                audio.playUI();
                static const int presets[][2] = {{960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
                int ni = 0;
                for (int i = 0; i < 5; i++) if (presets[i][0] == vw && presets[i][1] == vh) ni = (i + 1) % 5;
                cvar.set("vid.w", (float)presets[ni][0]); cvar.set("vid.h", (float)presets[ni][1]);
                applyVideo();
            }
            fy += bh + gap;
            bool fsm = cvar.get("vid.fullscreen", 0.0f) > 0.5f;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_fs", (std::string("Fullscreen: ") + (fsm ? "ON" : "OFF")).c_str(), ImVec2(bw, bh), fontUI, fs)) {
                audio.playUI(); cvar.set("vid.fullscreen", fsm ? 0.0f : 1.0f); applyVideo();
            }
            fy += bh + gap;
            bool faa = cvar.get("gfx.fxaa", 0.0f) > 0.5f;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_aa", (std::string("AA: ") + (faa ? "FXAA" : "OFF")).c_str(), ImVec2(bw, bh), fontUI, fs)) {
                audio.playUI(); cvar.set("gfx.fxaa", faa ? 0.0f : 1.0f);
            }
            fy += bh + gap;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_packs", "Texture Packs...", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); scr = M_PACKS; }
            fy += bh + gap;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("o_done", "Done", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); cvar.exec("save"); scr = M_MAIN; }
        } else if (scr == M_PACKS) {
            MCTitle(md, fontUI, fs + 4.0f, "Texture Packs...", (float)ww, 24.0f);
            float fx = cx, fy = 84.0f;
            if (menuPackIdx >= (int)packNames.size()) menuPackIdx = 0;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw);
            ImGui::Combo("##tpack", &menuPackIdx, [](void* d, int i) { return (*(std::vector<std::string>*)d)[i].c_str(); },
                         (void*)&packNames, (int)packNames.size());
            fy += 40.0f;
            static char addT[512] = "";
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw - 170.0f);
            ImGui::InputText("##tfolder", addT, sizeof(addT));
            ImGui::SetCursorPos(ImVec2(fx + bw - 160.0f, fy - 2.0f));
            if (MCButton("p_add", "Add pack", ImVec2(160, 32), fontUI, fs)) {
                std::string miss;
                bool ok = hasFiles(addT, {"grass_top.png", "grass_side.png", "dirt.png", "stone.png"}, miss) ||
                          hasFiles(addT, {"assets/minecraft/textures/block/grass_block_top.png",
                                          "assets/minecraft/textures/block/grass_block_side.png",
                                          "assets/minecraft/textures/block/dirt.png",
                                          "assets/minecraft/textures/block/stone.png"}, miss);
                if (!ok) console.print(std::string("pack rejected, missing: ") + miss + "\n");
                else {
                    std::string nm = linkPack("texture/packs", addT);
                    if (nm.empty()) console.print("pack add failed (exists?)\n");
                    else {
                        packNames = {"default"};
                        for (auto& d : listDirs("texture/packs")) packNames.push_back(d);
                        console.print("pack added: " + nm + "\n");
                    }
                }
            }
            fy += 52.0f;
            if (menuShaderSel >= (int)shaderPacks.size()) menuShaderSel = 0;
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw);
            ImGui::Combo("##spack", &menuShaderSel, [](void* d, int i) { return (*(std::vector<std::string>*)d)[i].c_str(); },
                         (void*)&shaderPacks, (int)shaderPacks.size());
            fy += 40.0f;
            static char addS[512] = "";
            ImGui::SetCursorPos(ImVec2(fx, fy)); ImGui::SetNextItemWidth(bw - 170.0f);
            ImGui::InputText("##sfolder", addS, sizeof(addS));
            ImGui::SetCursorPos(ImVec2(fx + bw - 160.0f, fy - 2.0f));
            if (MCButton("p_adds", "Add shaders", ImVec2(160, 32), fontUI, fs)) {
                std::string miss;
                bool ok = hasFiles(addS, {"lighting.vs", "lighting.fs", "line.vs", "outline.fs",
                                          "sky.vs", "sky.fs", "crosshair.vs", "crosshair.fs"}, miss);
                if (!ok) console.print(std::string("shader pack rejected, missing: ") + miss + "\n");
                else {
                    std::string nm = linkPack("shaders/packs", addS);
                    if (nm.empty()) console.print("shader add failed (exists?)\n");
                    else {
                        shaderPacks = {"default"};
                        for (auto& d : listDirs("shaders/packs")) shaderPacks.push_back(d);
                        console.print("shader pack added: " + nm + "\n");
                    }
                }
            }
            fy += 52.0f;
            ImGui::SetCursorPos(ImVec2(fx, fy));
            if (MCButton("p_done", "Done", ImVec2(bw, bh), fontUI, fs)) { audio.playUI(); scr = M_OPTIONS; }
        }

        // Футер как в MC: версия слева, дисклеймер справа.
        ImDrawList* fgd = ImGui::GetForegroundDrawList();
        if (fontUI) {
            fgd->AddText(fontUI, 16.0f, ImVec2(10, (float)hh - 28), IM_COL32(170, 170, 170, 255), "Rendor 0.1");
            const char* cr = "Not affiliated with Mojang AB";
            ImVec2 csz = fontUI->CalcTextSizeA(16.0f, 10000.0f, 0, cr);
            fgd->AddText(fontUI, 16.0f, ImVec2((float)ww - csz.x - 10, (float)hh - 28), IM_COL32(170, 170, 170, 255), cr);
        }
        if (fontUI) ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    if (wantQuit || playPath.empty()) {
        audio.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }
    std::string shaderDir = (shaderPacks[menuShaderSel] == "default") ? "shaders"
                          : "shaders/packs/" + shaderPacks[menuShaderSel];
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // в игре захват
    firstMouse = true;
    (void)0; // пак текстур выбирается в игре (комбо) либо из меню ниже

    // ================= GAME =================
    gPaused = false;
    audio.wind(true); // эмбиент только в игре, не в меню
    auto sh = [&](const char* n) { return shaderDir + "/" + n; };
    Shader lightingShader(sh("lighting.vs").c_str(), sh("lighting.fs").c_str());
    Shader lineShader(sh("line.vs").c_str(), sh("outline.fs").c_str());
    Shader crosshairShader(sh("crosshair.vs").c_str(), sh("crosshair.fs").c_str());
    Shader skyShader(sh("sky.vs").c_str(), sh("sky.fs").c_str());
    // fxaa есть только в дефолтном паке: у кастомных — fallback на shaders/
    auto fxaaFile = [&](const char* n) {
        std::string p = shaderDir + "/" + n;
        FILE* f = fopen(p.c_str(), "rb");
        if (f) { fclose(f); return p; }
        return std::string("shaders/") + n;
    };
    Shader fxaaShader(fxaaFile("fxaa.vs").c_str(), fxaaFile("fxaa.fs").c_str());
    auto reloadShaders = [&]() {
        lightingShader.load(sh("lighting.vs").c_str(), sh("lighting.fs").c_str());
        lineShader.load(sh("line.vs").c_str(), sh("outline.fs").c_str());
        crosshairShader.load(sh("crosshair.vs").c_str(), sh("crosshair.fs").c_str());
        skyShader.load(sh("sky.vs").c_str(), sh("sky.fs").c_str());
        fxaaShader.load(fxaaFile("fxaa.vs").c_str(), fxaaFile("fxaa.fs").c_str());
        console.print("shaders reloaded: " + shaderDir + "\n");
    };
    unsigned int triVAO = 0;
    glGenVertexArrays(1, &triVAO);
    glBindVertexArray(triVAO);
    glBindVertexArray(0);

    const int W = playCX * 16;
    const glm::vec3 worldOffset(-W / 2.0f, 0.0f, -W / 2.0f);
    auto world = std::make_unique<World>();
    bool haveWorld = false;
    if (playNew) {
        world->init(playCX, playCZ, playSeed);
        haveWorld = true;
        std::cout << "New world " << playCX << "x" << playCZ << " seed " << playSeed << "\n";
    } else if (newSeed == -2) {
        world->init(playCX, playCZ, 1337);
        if (loadWorld(*world, "world.bin")) { haveWorld = true; std::cout << "Imported world.bin\n"; }
    } else {
        if (loadWorld(*world, playPath.c_str())) {
            haveWorld = true;
            std::cout << "Loaded " << playPath << "\n";
        }
    }
    if (!haveWorld) {
        std::cout << "No world, back to title\n";
        glDeleteVertexArrays(1, &triVAO);
        goto title_screen;
    }
    int NCX = world->ncx(), NCZ = world->ncz();
    int WB = NCX * 16;
    Player player;
    player.spawn(*world, WB / 2, WB / 2);

    std::vector<ChunkMesh> meshes(NCX * NCZ);
    std::vector<ChunkMesh> waterMeshes(NCX * NCZ);
    std::vector<ChunkMesh> lavaMeshes(NCX * NCZ);
    std::vector<char> meshLoaded(NCX * NCZ, 0);
    auto midx = [&](int cx, int cz) { return cz * NCX + cx; };
    auto rebuild = [&](int cx, int cz) {
        if (cx < 0 || cx >= NCX || cz < 0 || cz >= NCZ) return;
        meshes[midx(cx, cz)].upload(world->buildChunk(cx, cz));
        std::vector<float> wv, lv;
        world->buildFluids(cx, cz, wv, lv);
        waterMeshes[midx(cx, cz)].upload(wv);
        lavaMeshes[midx(cx, cz)].upload(lv);
        meshLoaded[midx(cx, cz)] = 1;
    };
    auto unload = [&](int cx, int cz) {
        if (cx < 0 || cx >= NCX || cz < 0 || cz >= NCZ) return;
        meshes[midx(cx, cz)].destroy();
        waterMeshes[midx(cx, cz)].destroy();
        lavaMeshes[midx(cx, cz)].destroy();
        meshLoaded[midx(cx, cz)] = 0;
    };
    auto rebuildAll = [&]() {
        for (int cz = 0; cz < NCZ; cz++)
            for (int cx = 0; cx < NCX; cx++)
                rebuild(cx, cz);
    };
    int curPCX = -1, curPCZ = -1, curVR = -1;
    auto ensureAround = [&]() {
        int vr = (int)cvar.get("view.dist", 4.0f); // render distance, чанки O(R^2)
        if (vr < 2) vr = 2; if (vr > 12) vr = 12;
        int pcx = (int)player.pos.x / 16, pcz = (int)player.pos.z / 16;
        if (pcx == curPCX && pcz == curPCZ && vr == curVR) return;
        curPCX = pcx; curPCZ = pcz; curVR = vr;
        size_t nv = 0;
        for (int cz = 0; cz < NCZ; cz++)
            for (int cx = 0; cx < NCX; cx++) {
                int dd = std::max(abs(cx - pcx), abs(cz - pcz));
                if (dd <= vr) { if (!meshLoaded[midx(cx, cz)]) rebuild(cx, cz); nv += meshes[midx(cx, cz)].vertexCount; }
                else if (meshLoaded[midx(cx, cz)]) unload(cx, cz);
            }
        std::cout << "stream chunk " << pcx << "," << pcz << " verts " << nv << "\n";
    };
    auto touchEdit = [&](int wx, int wz) {
        int cx = wx / 16, cz = wz / 16;
        if (cx < 0 || cx >= NCX || cz < 0 || cz >= NCZ) return; // наружу за границу
        if (meshLoaded[midx(cx, cz)]) rebuild(cx, cz);
        if (wx % 16 == 0 && cx > 0 && meshLoaded[midx(cx-1, cz)]) rebuild(cx - 1, cz);
        if (wx % 16 == 15 && cx < NCX - 1 && meshLoaded[midx(cx+1, cz)]) rebuild(cx + 1, cz);
        if (wz % 16 == 0 && cz > 0 && meshLoaded[midx(cx, cz-1)]) rebuild(cx, cz - 1);
        if (wz % 16 == 15 && cz < NCZ - 1 && meshLoaded[midx(cx, cz+1)]) rebuild(cx, cz + 1);
    };
    ensureAround();

    // Линии рёбер куба [0,1]^3 — подсветка, depth честный.
    unsigned int lineVAO = 0, lineVBO = 0;
    {
        float e[] = {
            0,0,0, 1,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,1, 0,0,0,
            0,1,0, 1,1,0, 1,1,0, 1,1,1, 1,1,1, 0,1,1, 0,1,1, 0,1,0,
            0,0,0, 0,1,0, 1,0,0, 1,1,0, 1,0,1, 1,1,1, 0,0,1, 0,1,1,
        };
        glGenVertexArrays(1, &lineVAO);
        glGenBuffers(1, &lineVBO);
        glBindVertexArray(lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(e), e, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    std::string packDir = "texture/tiles";
    int packIdx = menuPackIdx;
    if (packIdx < 0 || packIdx >= (int)packNames.size()) packIdx = 0;
    unsigned int diffuseMap  = loadTileArray(packDir.c_str());
    setTileArrayFilter(diffuseMap, (int)cvar.get("gfx.filter", 0.0f));
    auto applyPack = [&](const std::string& pn) {
        for (size_t i = 0; i < packNames.size(); i++)
            if (packNames[i] == pn) packIdx = (int)i;
        std::string nd;
        if (pn == "default" || pn == "texture/tiles") nd = "texture/tiles";
        else if (!pn.empty() && pn[0] == '/') nd = pn; // абсолютный путь (MC-пак целиком)
        else nd = "texture/packs/" + pn;
        unsigned int nt = loadTileArray(nd.c_str());
        glDeleteTextures(1, &diffuseMap);
        diffuseMap = nt;
        setTileArrayFilter(diffuseMap, (int)cvar.get("gfx.filter", 0.0f));
        packDir = nd;
        audio.setSoundDir(nd + "/sounds");
        audio.reload();
        console.print("pack: " + nd + "\n");
    };
    if (packNames[packIdx] != "default") applyPack(packNames[packIdx]);
    runLine = [&](const std::string& s) {
        if (s.rfind("pack ", 0) == 0) applyPack(s.substr(5));
        else cvar.exec(s);
    };
    unsigned int specularMap = 0;
    {
        // земля матовая: спекуляр глушим чёрной 1x1
        unsigned char black[4] = {0, 0, 0, 255};
        glGenTextures(1, &specularMap);
        glBindTexture(GL_TEXTURE_2D, specularMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    // Пост-FBO для FXAA: сцена в текстуру, затем один пасс на экран.
    // Без FXAA рендерим как раньше сразу в backbuffer (нулевая цена).
    unsigned int postFBO = 0, postTex = 0, postDepth = 0;
    int postW = 0, postH = 0;
    auto ensurePost = [&](int w, int h) {
        if (postFBO && w == postW && h == postH) return;
        if (postFBO) {
            glDeleteFramebuffers(1, &postFBO);
            glDeleteTextures(1, &postTex);
            glDeleteRenderbuffers(1, &postDepth);
            postFBO = 0;
        }
        postW = w; postH = h;
        glGenFramebuffers(1, &postFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, postFBO);
        glGenTextures(1, &postTex);
        glBindTexture(GL_TEXTURE_2D, postTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postTex, 0);
        glGenRenderbuffers(1, &postDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, postDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, postDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "post FBO incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    auto dropPost = [&]() {
        if (postFBO) {
            glDeleteFramebuffers(1, &postFBO);
            glDeleteTextures(1, &postTex);
            glDeleteRenderbuffers(1, &postDepth);
            postFBO = 0; postTex = 0; postDepth = 0;
        }
        postW = postH = 0;
    };

    lightingShader.use();
    lightingShader.setInt("material.diffuse",  0);
    lightingShader.setInt("material.specular", 1);

    std::cout << "\nWASD move, Space jump/up, Shift sneak/down, 2xSpace or Ctrl+Space fly, F flashlight, L lamps, 1/2/3 block, LMB break, RMB place, F5 save, F9 load.\n";

    bool prevL = false, prevR = false, prevF5 = false, prevF9 = false;
    bool pauseOpt = false, toTitle = false; // подэкран опций паузы, выход в титул
    // RML-пауза: действия игры (старый оверлей остаётся при ui.rml=0)
    gRml.onResume = [&]() {
        audio.playUI();
        gPaused = false;
        glfwSetInputMode(window, GLFW_CURSOR, console.open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        firstMouse = true;
    };
    gRml.onQuit = [&]() {
        audio.playUI();
        if (saveWorld(*world, playPath.c_str())) std::cout << "Saved " << playPath << "\n";
        else std::cout << "Save FAILED\n";
        toTitle = true;
    };
    gRml.onSlider = [&](const char* id, float v) {
        std::string s = id ? id : "";
        if (s == "s_fov") cvar.set("cam.fov", v);
        else if (s == "s_gamma") cvar.set("sun.gamma", v);
        else if (s == "s_fog") cvar.set("fog.far", v);
    };
    gRml.getSlider = [&](const char* id) -> float {
        std::string s = id ? id : "";
        if (s == "s_fov") return cvar.get("cam.fov", 70.0f);
        if (s == "s_gamma") return cvar.get("sun.gamma", 1.2f);
        return cvar.get("fog.far", 260.0f);
    };
    gRml.onDone = [&]() { audio.playUI(); cvar.exec("save"); };
    bool prevF = false, prevG = false, prevF1 = false, prevGrave = false, prevEsc = false;
    bool flashOn = true, followOn = true;
    bool prev1 = false, prev2 = false, prev3 = false;
    int placeId = B_GRASS;
    int shPackIdx = 0;
    float tickAcc = 0.0f, tod = 0.56f;
    int lastFilter = -1; // смена gfx.filter из консоли применяется живо
    glm::vec3 pointLightPositions[4];

    while (!glfwWindowShouldClose(window) && !toTitle)
    {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame; lastFrame = now;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        if (gPaused) deltaTime = 0.0f; // пауза: тики игрока, флюиды и tod стоят
        { int rw, rh; glfwGetFramebufferSize(window, &rw, &rh); gRml.setSize(rw, rh); }

        // консоль: строки из stdin по Enter
        {
            char buf[1024];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                conLine += buf;
                size_t p;
                while ((p = conLine.find('\n')) != std::string::npos) {
                    std::string ln = conLine.substr(0, p);
                    if (ln.rfind("pack ", 0) == 0) {
                        std::string pn = ln.substr(5);
                        std::string nd = (pn == "default") ? "texture/tiles"
                            : (!pn.empty() && pn[0] == '/' ? pn : "texture/packs/" + pn);
                        unsigned int nt = loadTileArray(nd.c_str());
                        glDeleteTextures(1, &diffuseMap);
                        diffuseMap = nt;
                        packDir = nd;
                        console.print("pack: " + nd + "\n");
                    } else cvar.exec(ln);
                    conLine.erase(0, p + 1);
                }
            }
        }

        // --- F1/~ — консоль ImGui ---
        bool f1 = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
        bool grv = glfwGetKey(window, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
        if ((f1 && !prevF1) || (grv && !prevGrave)) {
            console.open = !console.open;
            glfwSetInputMode(window, GLFW_CURSOR, console.open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            firstMouse = true;
            std::cout << (console.open ? "console OPEN\n" : "console closed\n");
        }
        prevF1 = f1; prevGrave = grv;
        bool esc = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (esc && !prevEsc) {
            if (console.open) {
                console.open = false;
                if (!gPaused) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            } else {
                gPaused = !gPaused;
                pauseOpt = false;
                glfwSetInputMode(window, GLFW_CURSOR, gPaused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                firstMouse = true;
                audio.playUI();
                if (gPaused) { audio.lavaLoop(camera.Position, false); audio.underLoop(false); }
                std::cout << (gPaused ? "paused\n" : "resumed\n");
            }
        }
        prevEsc = esc;

        // --- PLAYER ---
        bool curF = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        bool curG = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
        bool curL = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool curR = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool curF5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        bool curF9 = glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
        if (console.open) {
            prevF = curF; prevG = curG;
            prevL = curL; prevR = curR; prevF5 = curF5; prevF9 = curF9;
        }
        if (!console.open && curF && !prevF) { flashOn = !flashOn; std::cout << (flashOn ? "flash ON\n" : "flash OFF\n"); }
        prevF = curF;
        if (!console.open && curG && !prevG) { followOn = !followOn; std::cout << (followOn ? "lamps ON\n" : "lamps OFF\n"); }
        prevG = curG;
        glm::vec2 mv(0.0f);
        if (!console.open) {
        player.walkSpeed = cvar.get("move.walk", 4.3f);
        player.flySpeed = cvar.get("move.fly", 8.0f);
        player.jumpVel = cvar.get("move.jump", 7.5f);
        player.stepOn = cvar.get("move.step", 1.0f) > 0.5f;
        player.stepH = cvar.get("move.step_h", 1.0f);
        player.autoJump = cvar.get("move.bhop", 0.0f) > 0.5f;
        }
        bool jump = false, down = false;
        static bool prevSpace = false;
        bool sneakNow = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        player.sneak = sneakNow && !console.open;
        if (!console.open) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) mv.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) mv.x -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) mv.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) mv.y -= 1.0f;
        bool spaceNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool ctrlNow = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        if (spaceNow && !prevSpace && ctrlNow) { // только Ctrl+Space — дабл-тап убран
            player.fly = !player.fly;
            player.vel = glm::vec3(0.0f);
            std::cout << (player.fly ? "FLY\n" : "WALK\n");
        }
        prevSpace = spaceNow;
        jump = spaceNow;
        down = sneakNow; // C убран: вниз на шифте
        }
        audio.setMaster(cvar.get("snd.vol", 0.8f) * (cvar.get("snd.on", 1.0f) > 0.5f ? 1.0f : 0.0f));
        {
            int fm = (int)cvar.get("gfx.filter", 0.0f);
            if (fm != lastFilter) { setTileArrayFilter(diffuseMap, fm); lastFilter = fm; }
        }
        audio.listener(camera.Position, camera.Front);
        static float stepAcc = 0.0f;
        {
            float hs = sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z);
            if (!player.fly && !player.sneak && player.onGround && hs > 1.0f) { // sneak бесшумен
                stepAcc += hs * deltaTime;
                if (stepAcc > 2.2f) {
                    stepAcc = 0.0f;
                    unsigned char gb = world->getBlock((int)player.pos.x, (int)player.pos.y - 1, (int)player.pos.z);
                    int surf = gBlocks.get(gb).stepSurf;
                    audio.playStep(player.pos + worldOffset, surf);
                }
            } else stepAcc = 0.0f;
        }
        // всплеск при входе в воду
        {
            static bool wasWet = false;
            unsigned char fb = world->getBlock((int)player.pos.x, (int)(player.pos.y + 0.3f), (int)player.pos.z);
            bool wet = (fb == B_WATER);
            if (wet && !wasWet) audio.playSplash(player.pos + worldOffset);
            if (fb == B_LAVA && !wasWet) audio.playThunk(player.pos + worldOffset);
            wasWet = wet || fb == B_LAVA;
        }
        // гребки в движении + петли лавы рядом и подводья
        {
            unsigned char fb = world->getBlock((int)player.pos.x, (int)(player.pos.y + 0.3f), (int)player.pos.z);
            float hs = sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z);
            static float swimAcc = 0.0f;
            if ((fb == B_WATER || fb == B_LAVA) && hs > 0.5f) {
                swimAcc += hs * deltaTime;
                if (swimAcc > 2.5f) {
                    swimAcc = 0.0f;
                    audio.playSwim(player.pos + worldOffset, fb == B_LAVA);
                }
            } else swimAcc = 0.0f;
            bool nearLava = false;
            for (int a = -4; a <= 4 && !nearLava; a++)
                for (int b = -2; b <= 2 && !nearLava; b++)
                    for (int c = -4; c <= 4 && !nearLava; c++)
                        if (world->getBlock((int)player.pos.x + a, (int)player.pos.y + b, (int)player.pos.z + c) == B_LAVA)
                            nearLava = true;
            audio.lavaLoop(camera.Position, nearLava && !gPaused);
            unsigned char eye = world->getBlock((int)player.pos.x, (int)(player.pos.y + player.eye), (int)player.pos.z);
            audio.underLoop(eye == B_WATER && !gPaused);
        }
        float rate = cvar.get("tick.rate", 120.0f);
        if (rate < 30.0f) rate = 30.0f;
        if (rate > 240.0f) rate = 240.0f;
        tickAcc += deltaTime;
        float h = 1.0f / rate;
        if (tickAcc > h * 8) tickAcc = h * 8;
        while (tickAcc >= h) {
            player.update(h, *world, mv, glm::radians(camera.Yaw), jump, down);
            tickAcc -= h;
        }
        // упал за мир — респаун в центр
        if (player.pos.y < -10.0f) player.spawn(*world, WB / 2, WB / 2);
        camera.Position = player.pos + worldOffset + glm::vec3(0.0f, player.eyeH(), 0.0f);
        ensureAround();

        // тики флюидов: вода каждый 2-й, лава каждый 8-й; перестройка грязных чанков
        {
            static float fluidAcc = 0.0f;
            static int fluidTick = 0;
            fluidAcc += deltaTime;
            if (fluidAcc >= 0.25f && world->fluidsDirty()) {
                fluidAcc = 0.0f;
                fluidTick++;
                std::vector<int> dirty;
                int n = world->tickFluids(fluidTick % 4 == 0);
                world->takeFluidDirty(dirty);
                for (int id : dirty) rebuild(id % NCX, id / NCX);
                if (n > 0) std::cout << "fluid tick: " << n << " changes\n";
            }
        }

        // FXAA on: сцена в FBO (чистим именно его), resolve-пасс ниже;
        // off: сразу в backbuffer. Порядок важен: clear ПОСЛЕ бинда цели.
        bool fxaaOn = cvar.get("gfx.fxaa", 0.0f) > 0.5f;
        int fww = 0, fhh = 0;
        if (fxaaOn) {
            glfwGetFramebufferSize(window, &fww, &fhh);
            ensurePost(fww, fhh);
            glBindFramebuffer(GL_FRAMEBUFFER, postFBO);
            glViewport(0, 0, fww, fhh);
        }
        glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int vbw, vbh;
        glfwGetFramebufferSize(window, &vbw, &vbh);
        if (vbh <= 0) vbh = 1;
        glm::mat4 projection = glm::perspective(glm::radians(cvar.get("cam.fov", 70.0f)), (float)vbw / (float)vbh, 0.1f, 600.0f);
        glm::mat4 view = camera.GetViewMatrix();
        Frustum frustum = Frustum::fromVP(projection * view);
        auto chunkVisible = [&](int cx, int cz) {
            glm::vec3 mn = worldOffset + glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f);
            return frustum.visible(mn, mn + glm::vec3(16.0f, (float)Chunk::SY, 16.0f));
        };

        // ---- DAY CYCLE ----
        float spd = cvar.get("time.speed", 600.0f);
        if (spd > 0.0f) tod += deltaTime * 6.2831853f / spd;
        float sunA = tod;
        glm::vec3 sunVec = glm::normalize(glm::vec3(cos(sunA), sin(sunA), 0.35f));
        float twi = glm::smoothstep(-0.14f, 0.02f, sunVec.y);
        float morn = glm::smoothstep(-0.02f, 0.06f, sunVec.y);
        float noonCut = 1.0f - 0.20f * glm::smoothstep(0.5f, 0.95f, sunVec.y);
        float sunI = morn * noonCut;
        float dayF = twi;
        float nightF = 1.0f - dayF;
        glm::vec3 topColor = glm::mix(glm::vec3(0.008f, 0.015f, 0.05f), glm::vec3(0.30f, 0.55f, 0.92f), dayF);
        glm::vec3 horizonColor = glm::mix(glm::vec3(0.04f, 0.06f, 0.11f), glm::vec3(0.74f, 0.83f, 0.93f), dayF);
        float duskF = glm::clamp(1.0f - glm::abs(sunVec.y) / 0.25f, 0.0f, 1.0f) * twi;
        glm::vec3 sunCol = glm::mix(glm::vec3(1.0f, 0.55f, 0.25f), glm::vec3(1.0f, 0.97f, 0.9f),
                                    glm::smoothstep(0.0f, 0.4f, sunVec.y));

        // небо первым (без глубины)
        {
            int ww, hh;
            glfwGetFramebufferSize(window, &ww, &hh);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            skyShader.use();
            skyShader.setMat4("invVP", glm::inverse(projection * view));
            skyShader.setVec3("topColor", topColor);
            skyShader.setVec3("horizonColor", horizonColor);
            skyShader.setVec3("sunDir", sunVec);
            skyShader.setFloat("nightF", nightF);
            skyShader.setVec2("res", (float)ww, (float)hh);
            glBindVertexArray(triVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }

        // ---- PICK по миру DDA ----
        glm::vec3 rayO = camera.Position - worldOffset;
        int wx = -1, wy = -1, wz = -1;
        glm::vec3 hitN(0.0f);
        float hitT = world->pick(rayO, camera.Front, 100.0f, wx, wy, wz, hitN);
        bool hasHit = (hitT > 0.0f);

        // ---- BREAK / PLACE ----
        if (hasHit && !console.open && !gPaused) {
            if (curL && !prevL) {
                unsigned char broken = world->getBlock(wx, wy, wz);
                world->setBlock(wx, wy, wz, 0);
                glm::vec3 bp = worldOffset + glm::vec3(wx + 0.5f, wy + 0.5f, wz + 0.5f);
                audio.playBreakId(bp, broken);
                touchEdit(wx, wz);
            }
            if (curR && !prevR) {
                int px = wx + (int)hitN.x, py = wy + (int)hitN.y, pz = wz + (int)hitN.z;
                bool inPlayer = (px + 1 > player.pos.x - player.halfW && px < player.pos.x + player.halfW &&
                                 py + 1 > player.pos.y && py < player.pos.y + player.height &&
                                 pz + 1 > player.pos.z - player.halfW && pz < player.pos.z + player.halfW);
                // ставить можно в воздух и во флюид (замена воды/лавы блоком)
                if (!World::isSolid(world->getBlock(px, py, pz)) && !inPlayer) {
                    world->setBlock(px, py, pz, (unsigned char)placeId);
                    audio.playPlaceId(worldOffset + glm::vec3(px + 0.5f, py + 0.5f, pz + 0.5f), placeId);
                    touchEdit(px, pz);
                }
            }
        }
        prevL = curL; prevR = curR;
        // выбор блока: 1 трава 2 земля 3 камень
        bool c1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
        bool c2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
        bool c3 = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
        if (!console.open && !gPaused && c1 && !prev1) { placeId = B_GRASS; std::cout << "block: grass\n"; }
        if (!console.open && !gPaused && c2 && !prev2) { placeId = B_DIRT; std::cout << "block: dirt\n"; }
        if (!console.open && !gPaused && c3 && !prev3) { placeId = B_STONE; std::cout << "block: stone\n"; }
        prev1 = c1; prev2 = c2; prev3 = c3;
        if (!console.open && !gPaused && curF5 && !prevF5) {
            if (saveWorld(*world, playPath.c_str())) std::cout << "Saved " << playPath << "\n";
            else std::cout << "Save FAILED\n";
        }
        if (!console.open && !gPaused && curF9 && !prevF9) {
            if (loadWorld(*world, playPath.c_str())) { rebuildAll(); std::cout << "Loaded " << playPath << "\n"; }
            else std::cout << "Load FAILED\n";
        }
        prevF5 = curF5; prevF9 = curF9;

        // PASS 1: opaque. Только загруженные видимые меши.
        lightingShader.use();
        lightingShader.setFloat("material.shininess", 32.0f);
        lightingShader.setVec3("viewPos", camera.Position);
        glm::vec3 fogLin(
            pow(horizonColor.x, 2.2f), pow(horizonColor.y, 2.2f), pow(horizonColor.z, 2.2f));
        lightingShader.setVec3("fogColor", fogLin);
        lightingShader.setVec2("fogRange", cvar.get("fog.near", 0.0f), cvar.get("fog.far", 260.0f));
        lightingShader.setFloat("satU", cvar.get("sun.sat", 1.8f));
        lightingShader.setFloat("gammaU", cvar.get("sun.gamma", 1.2f));

        glm::vec3 ambDay = glm::mix(glm::vec3(0.03f, 0.035f, 0.07f), glm::vec3(0.20f), dayF);
        ambDay *= cvar.get("sun.amb", 3.0f);
        lightingShader.setVec3("dirLight.direction", -sunVec);
        lightingShader.setVec3("dirLight.ambient",   glm::mix(ambDay, glm::vec3(0.34f, 0.25f, 0.16f), duskF * 0.6f));
        lightingShader.setVec3("dirLight.diffuse",   glm::mix(glm::vec3(0.015f), sunCol * (1.7f * cvar.get("sun.i", 1.0f)), sunI));
        lightingShader.setVec3("dirLight.specular",  glm::mix(glm::vec3(0.01f), sunCol * 0.3f, sunI));

        glm::vec3 pp = player.pos;
        glm::vec3 lampOff[4] = {
            glm::vec3(3.0f, 4.0f, 2.0f), glm::vec3(-4.0f, 3.0f, -3.0f),
            glm::vec3(5.0f, 2.0f, -4.0f), glm::vec3(0.0f, 5.0f, 0.0f),
        };
        for (int i = 0; i < 4; i++) {
            std::string b = "pointLights[" + std::to_string(i) + "].";
            float lon = followOn ? 1.0f : 0.0f;
            lightingShader.setVec3 (b + "position", pp + lampOff[i] + worldOffset);
            lightingShader.setVec3 (b + "ambient",   0.02f * lon, 0.02f * lon, 0.02f * lon);
            lightingShader.setVec3 (b + "diffuse",   0.3f * lon, 0.3f * lon, 0.3f * lon);
            lightingShader.setVec3 (b + "specular",  0.3f * lon, 0.3f * lon, 0.3f * lon);
            lightingShader.setFloat(b + "constant",  1.0f);
            lightingShader.setFloat(b + "linear",    0.22f);
            lightingShader.setFloat(b + "quadratic", 0.06f);
        }

        float fon = flashOn ? 1.0f : 0.0f;
        lightingShader.setVec3 ("spotLight.position",  camera.Position);
        lightingShader.setVec3 ("spotLight.direction", camera.Front);
        lightingShader.setFloat("spotLight.cutOff",      glm::cos(glm::radians(12.5f)));
        lightingShader.setFloat("spotLight.outerCutOff", glm::cos(glm::radians(15.0f)));
        lightingShader.setVec3 ("spotLight.ambient",   0.0f, 0.0f, 0.0f);
        lightingShader.setVec3 ("spotLight.diffuse",   fon, fon, fon);
        lightingShader.setVec3 ("spotLight.specular",  fon, fon, fon);
        lightingShader.setFloat("spotLight.constant",  1.0f);
        lightingShader.setFloat("spotLight.linear",    0.09f);
        lightingShader.setFloat("spotLight.quadratic", 0.032f);

        lightingShader.setMat4("projection", projection);
        lightingShader.setMat4("view", view);
        lightingShader.setFloat("alphaU", 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, diffuseMap);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, specularMap);

        for (int cz = 0; cz < NCZ; cz++)
            for (int cx = 0; cx < NCX; cx++) {
                int mi = cz * NCX + cx;
                if (!meshLoaded[mi] || !chunkVisible(cx, cz)) continue;
                glm::vec3 off = worldOffset + glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), off);
                lightingShader.setMat4("model", model);
                meshes[mi].draw();
                lavaMeshes[mi].draw();
            }

        // PASS 1b: вода прозрачная (без записи глубины, после opaque)
        glDepthMask(GL_FALSE);
        lightingShader.setFloat("alphaU", 0.75f);
        for (int cz = 0; cz < NCZ; cz++)
            for (int cx = 0; cx < NCX; cx++) {
                int mi = cz * NCX + cx;
                if (!meshLoaded[mi] || !chunkVisible(cx, cz)) continue;
                glm::vec3 off = worldOffset + glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f);
                lightingShader.setMat4("model", glm::translate(glm::mat4(1.0f), off));
                waterMeshes[mi].draw();
            }
        glDepthMask(GL_TRUE);
        lightingShader.setFloat("alphaU", 1.0f);

        // PASS 2: подсветка рёбер честным depth
        if (hasHit) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glLineWidth(3.0f);

            lineShader.use();
            lineShader.setMat4("projection", projection);
            lineShader.setMat4("view", view);
            lineShader.setVec3("outlineColor", 1.0f, 0.55f, 0.1f);

            glm::vec3 mn = worldOffset + glm::vec3(wx - 0.002f, wy - 0.002f, wz - 0.002f);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), mn);
            model = glm::scale(model, glm::vec3(1.004f));
            lineShader.setMat4("model", model);

            glBindVertexArray(lineVAO);
            glDrawArrays(GL_LINES, 0, 24);
            glBindVertexArray(0);
            glLineWidth(1.0f);
            glDepthFunc(GL_LESS);
        }

        // FXAA-resolve: сцена из FBO на экран одним пассом (прицел и UI — после, без AA)
        if (fxaaOn) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fww, fhh);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            fxaaShader.use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, postTex);
            fxaaShader.setInt("sceneTex", 0);
            fxaaShader.setVec2("rcpFrame", 1.0f / (float)fww, 1.0f / (float)fhh);
            fxaaShader.setFloat("subpix", 0.75f);
            fxaaShader.setFloat("edgeThr", 0.125f);
            fxaaShader.setFloat("edgeThrMin", 0.0625f);
            glBindVertexArray(triVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }

        // прицел поверх всего
        {
            int ww, hh;
            glfwGetFramebufferSize(window, &ww, &hh);
            glDisable(GL_DEPTH_TEST);
            crosshairShader.use();
            crosshairShader.setVec2("res", (float)ww, (float)hh);
            crosshairShader.setFloat("t", now);
            glBindVertexArray(triVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }

        // ImGui-консоль поверх всего
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // ---- PAUSE (ESC): RmlUi-документ или старый MC-оверлей (ui.rml) ----
        bool useRml = gRml.ok && cvar.get("ui.rml", 1.0f) > 0.5f;
        gRml.inputActive = gPaused && useRml;
        gRml.showPause(gPaused && useRml);
        if (gPaused && useRml) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            gRml.mouseMove(mx, my);
            static bool rmlL = false, rmlR = false;
            if (curL != rmlL) { gRml.mouseButton(0, curL); rmlL = curL; }
            if (curR != rmlR) { gRml.mouseButton(1, curR); rmlR = curR; }
            gRml.frame();
        } else if (gPaused) {
            int pww, phh;
            glfwGetFramebufferSize(window, &pww, &phh);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)pww, (float)phh));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.6f));
            ImGui::Begin("pause", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse);
            if (fontUI) ImGui::PushFont(fontUI);
            ImDrawList* pd = ImGui::GetWindowDrawList();
            const float pbw = 400.0f, pbh = 36.0f, pgap = 8.0f;
            const float pfs = fontUI ? 20.0f : 13.0f;
            const float pcx = ((float)pww - pbw) * 0.5f;
            if (!pauseOpt) {
                MCTitle(pd, fontUI, pfs + 4.0f, "Game menu", (float)pww, (float)phh * 0.18f);
                float py = (float)phh * 0.18f + 64.0f;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_back", "Back to Game", ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI(); gPaused = false; pauseOpt = false;
                    glfwSetInputMode(window, GLFW_CURSOR, console.open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                    firstMouse = true;
                }
                py += pbh + pgap;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_opt", "Options...", ImVec2(pbw, pbh), fontUI, pfs, true, 45)) { audio.playUI(); pauseOpt = true; }
                py += pbh + pgap;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_quit", "Save and Quit to Title", ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI();
                    if (saveWorld(*world, playPath.c_str())) std::cout << "Saved " << playPath << "\n";
                    else std::cout << "Save FAILED\n";
                    toTitle = true;
                }
            } else {
                MCTitle(pd, fontUI, pfs + 4.0f, "Options...", (float)pww, (float)phh * 0.18f);
                float py = (float)phh * 0.18f + 64.0f;
                MCPushSliderStyle();
                float v = cvar.get("cam.fov", 70.0f);
                ImGui::SetCursorPos(ImVec2(pcx, py)); ImGui::SetNextItemWidth(pbw);
                if (ImGui::SliderFloat("FOV", &v, 30.0f, 110.0f)) cvar.set("cam.fov", v);
                py += 40.0f;
                v = cvar.get("sun.gamma", 1.2f);
                ImGui::SetCursorPos(ImVec2(pcx, py)); ImGui::SetNextItemWidth(pbw);
                if (ImGui::SliderFloat("Gamma", &v, 0.5f, 4.0f)) cvar.set("sun.gamma", v);
                py += 40.0f;
                v = cvar.get("fog.far", 260.0f);
                ImGui::SetCursorPos(ImVec2(pcx, py)); ImGui::SetNextItemWidth(pbw);
                if (ImGui::SliderFloat("Fog distance", &v, 50.0f, 500.0f)) cvar.set("fog.far", v);
                MCPopSliderStyle();
                py += 52.0f;
                // --- video живьём (дистанция подхватывается стримингом, фильтр — кадром) ---
                static const int pds[] = {2, 4, 6, 8, 12};
                int pvd = (int)cvar.get("view.dist", 4.0f);
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_dist", ("Render Distance: " + std::to_string(pvd)).c_str(), ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI();
                    int ni = 0;
                    for (int i = 0; i < 5; i++) if (pds[i] == pvd) ni = (i + 1) % 5;
                    cvar.set("view.dist", (float)pds[ni]);
                }
                py += pbh + pgap;
                static const char* pfn[] = {"Nearest", "Bilinear", "Trilinear", "Aniso 8x"};
                int pfm = (int)cvar.get("gfx.filter", 0.0f); if (pfm < 0) pfm = 0; if (pfm > 3) pfm = 3;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_filter", ("Filtering: " + std::string(pfn[pfm])).c_str(), ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI(); cvar.set("gfx.filter", (float)((pfm + 1) % 4));
                }
                py += pbh + pgap;
                bool pvs = cvar.get("vid.vsync", 1.0f) > 0.5f;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_vsync", (std::string("VSync: ") + (pvs ? "ON" : "OFF")).c_str(), ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI(); cvar.set("vid.vsync", pvs ? 0.0f : 1.0f); applyVideo();
                }
                py += pbh + pgap;
                int pvw = (int)cvar.get("vid.w", 1280.0f), pvh = (int)cvar.get("vid.h", 720.0f);
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_res", ("Resolution: " + std::to_string(pvw) + "x" + std::to_string(pvh)).c_str(), ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI();
                    static const int pp[][2] = {{960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
                    int ni = 0;
                    for (int i = 0; i < 5; i++) if (pp[i][0] == pvw && pp[i][1] == pvh) ni = (i + 1) % 5;
                    cvar.set("vid.w", (float)pp[ni][0]); cvar.set("vid.h", (float)pp[ni][1]);
                    applyVideo();
                }
                py += pbh + pgap;
                bool pfs2 = cvar.get("vid.fullscreen", 0.0f) > 0.5f;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_fs", (std::string("Fullscreen: ") + (pfs2 ? "ON" : "OFF")).c_str(), ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI(); cvar.set("vid.fullscreen", pfs2 ? 0.0f : 1.0f); applyVideo();
                }
                py += pbh + pgap;
                bool pfaa = cvar.get("gfx.fxaa", 0.0f) > 0.5f;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_aa", (std::string("AA: ") + (pfaa ? "FXAA" : "OFF")).c_str(), ImVec2(pbw, pbh), fontUI, pfs, true, 45)) {
                    audio.playUI(); cvar.set("gfx.fxaa", pfaa ? 0.0f : 1.0f);
                }
                py += pbh + pgap;
                ImGui::SetCursorPos(ImVec2(pcx, py));
                if (MCButton("p_odone", "Done", ImVec2(pbw, pbh), fontUI, pfs, true, 45)) { audio.playUI(); cvar.exec("save"); pauseOpt = false; }
            }
            if (fontUI) ImGui::PopFont();
            ImGui::End();
            ImGui::PopStyleColor();
        }

        if (console.open) {
            ImGui::Begin("Console", &console.open);
            auto slider = [&](const char* label, const char* name, float lo, float hi) {
                float v = cvar.get(name);
                if (ImGui::SliderFloat(label, &v, lo, hi)) cvar.set(name, v);
            };
            slider("sun.i", "sun.i", 0.0f, 4.0f);
            slider("sun.amb", "sun.amb", 0.0f, 5.0f);
            slider("sun.sat", "sun.sat", 0.0f, 2.5f);
            slider("sun.gamma", "sun.gamma", 0.5f, 4.0f);
            slider("fog.near", "fog.near", 0.0f, 200.0f);
            slider("fog.far", "fog.far", 50.0f, 500.0f);
            slider("time.speed", "time.speed", 0.0f, 1200.0f);
            {
                float dayT = tod / 6.2831853f;
                dayT -= floor(dayT);
                if (ImGui::SliderFloat("time of day", &dayT, 0.0f, 1.0f)) tod = dayT * 6.2831853f;
            }
            slider("cam.fov", "cam.fov", 30.0f, 110.0f);
            slider("walk", "move.walk", 1.0f, 12.0f);
            slider("fly speed", "move.fly", 2.0f, 30.0f);
            slider("jump", "move.jump", 2.0f, 12.0f);
            slider("tick", "tick.rate", 30.0f, 240.0f);
            {
                bool st = cvar.get("move.step", 1.0f) > 0.5f;
                if (ImGui::Checkbox("auto-step", &st)) cvar.set("move.step", st ? 1.0f : 0.0f);
                float sh = cvar.get("move.step_h", 1.0f);
                if (ImGui::SliderFloat("step_h", &sh, 0.5f, 2.0f)) cvar.set("move.step_h", sh);
            }
            ImGui::Checkbox("flash (F)", &flashOn);
            ImGui::Checkbox("lamps (L)", &followOn);
            ImGui::Checkbox("fly mode", &player.fly);
            bool bhop = cvar.get("move.bhop", 0.0f) > 0.5f;
            if (ImGui::Checkbox("bhop on space", &bhop)) cvar.set("move.bhop", bhop ? 1.0f : 0.0f);
            ImGui::Separator();
            {
                if (packIdx >= (int)packNames.size()) packIdx = 0;
                if (ImGui::BeginCombo("pack", packNames[packIdx].c_str())) {
                    for (size_t i = 0; i < packNames.size(); i++)
                        if (ImGui::Selectable(packNames[i].c_str(), (int)i == packIdx)) {
                            packIdx = (int)i;
                            applyPack(packNames[i]);
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("shaders", shaderPacks[shPackIdx].c_str())) {
                    for (size_t i = 0; i < shaderPacks.size(); i++)
                        if (ImGui::Selectable(shaderPacks[i].c_str(), (int)i == shPackIdx)) {
                            shPackIdx = (int)i;
                            shaderDir = (shaderPacks[i] == "default") ? "shaders" : "shaders/packs/" + shaderPacks[i];
                            reloadShaders();
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("reload shaders")) reloadShaders();
            }
            ImGui::Separator();
            ImGui::BeginChild("log", ImVec2(0, 200), true);
            for (auto& ln : console.lines) ImGui::TextUnformatted(ln.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            if (ImGui::InputText("cmd", console.inputBuf, sizeof(console.inputBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                runLine(console.inputBuf);
                console.inputBuf[0] = 0;
                ImGui::SetKeyboardFocusHere(-1);
            }
            if (ImGui::Button("save")) cvar.exec("save");
            ImGui::SameLine();
            if (ImGui::Button("load")) { cvar.exec("load"); rebuildAll(); }
            ImGui::End();
        }
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Чистим ресурсы игры — общее для выхода и возврата в титул.
    audio.wind(false);
    audio.lavaLoop(camera.Position, false);
    audio.underLoop(false);
    glDeleteVertexArrays(1, &lineVAO);
    glDeleteBuffers(1, &lineVBO);
    glDeleteVertexArrays(1, &triVAO);
    for (auto& m : meshes) m.destroy();
    for (auto& m : waterMeshes) m.destroy();
    for (auto& m : lavaMeshes) m.destroy();
    glDeleteTextures(1, &diffuseMap);
    glDeleteTextures(1, &specularMap);
    dropPost();
    world.reset();
    gPaused = false;
    if (toTitle && !glfwWindowShouldClose(window)) goto title_screen;
    gRml.shutdown();
    // Меню-текстуры живут пока возможен возврат в титул — только полный выход.
    glDeleteTextures(1, &texDirt);
    glDeleteTextures(1, &texGTop);
    glDeleteTextures(1, &texGSide);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
