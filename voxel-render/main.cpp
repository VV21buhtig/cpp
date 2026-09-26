// Glad 2 — инклуд <glad/gl.h>, и он ДО GLFW
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "engine/mesh.h"
#include "engine/texture.h"
#include "engine/chunk.h"
#include "engine/world.h"
#include "engine/save.h"
#include "engine/cvar.h"
#include "engine/frustum.h"
#include "game/player.h"
#include "game/console.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

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

Camera camera(glm::vec3(8.0f, 6.0f, 14.0f));
GameConsole console;
CVarSys* gCvar = nullptr;
float lastX = 640.0f, lastY = 360.0f;
bool  firstMouse = true;
float deltaTime = 0.0f, lastFrame = 0.0f;

void framebuffer_size_callback(GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); }
void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (console.open) { firstMouse = true; return; }
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xo = (float)xpos - lastX, yo = lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    camera.ProcessMouseMovement(xo, yo);
}
void scroll_callback(GLFWwindow*, double, double) { /* zoom only via console cam.fov */ }
void processInput(GLFWwindow* w) {
    if (!console.open && glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(w, true);
    // WASD — через Player::update, камера следует за игроком
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

static const int VIEW_R = 4; // радиус мешей вокруг чанка игрока

int main()
{
    if (!glfwInit()) { std::cerr << "GLFW fail\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Voxel", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
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
    cvar.load("gfx.cfg");
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
    // ================= MENU =================
    std::string playPath;   // worlds/<name>.bin
    int playCX = 16, playCZ = 16, playSeed = 1337;
    bool playNew = false, wantQuit = false;
    static char newName[64] = "myworld";
    int newSizeIdx = 1; // 0:8 1:16 2:24
    int newSeed = 1337;
    int menuWorldSel = 0, menuShaderSel = 0, menuPackIdx = 0;
    const int sizes[3] = {8, 16, 24};
    while (!wantQuit && playPath.empty()) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) { wantQuit = true; break; }
        int ww, hh;
        glfwGetFramebufferSize(window, &ww, &hh);
        glViewport(0, 0, ww, hh);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)ww, (float)hh));
        ImGui::Begin("menu", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::SetCursorPos(ImVec2(((float)ww - 520.0f) / 2.0f, 20.0f));
        ImGui::BeginChild("center", ImVec2(520.0f, (float)hh - 40.0f), false);
        ImGui::Text("VOXEL");
        ImGui::Separator();
        std::vector<std::string> worlds = listWorlds();
        if (menuWorldSel >= (int)worlds.size()) menuWorldSel = 0;
        ImGui::Text("Worlds:");
        for (size_t i = 0; i < worlds.size(); i++)
            if (ImGui::Selectable(worlds[i].c_str(), (int)i == menuWorldSel)) menuWorldSel = (int)i;
        if (!worlds.empty() && ImGui::Button("Play")) {
            playPath = "worlds/" + worlds[menuWorldSel] + ".bin";
            playNew = false;
        }
        ImGui::SameLine();
        if (!worlds.empty() && ImGui::Button("Delete") && menuWorldSel < (int)worlds.size()) {
            remove(("worlds/" + worlds[menuWorldSel] + ".bin").c_str());
            menuWorldSel = 0;
        }
        // legacy import
        {
            FILE* lf = fopen("world.bin", "rb");
            if (lf) {
                fclose(lf);
                if (ImGui::Button("Import legacy world.bin")) {
                    playPath = "worlds/imported.bin";
                    playNew = false;
                    playCX = 16; playCZ = 16;
                    // пометка: загрузить из world.bin вместо playPath (флаг ниже)
                    newSeed = -2;
                }
            }
        }
        ImGui::Separator();
        ImGui::Text("New world:");
        ImGui::InputText("name", newName, sizeof(newName));
        const char* sizeNames[3] = {"Small 8x8", "Normal 16x16", "Large 24x24"};
        ImGui::Combo("size", &newSizeIdx, sizeNames, 3);
        ImGui::InputInt("seed", &newSeed);
        ImGui::SameLine();
        if (ImGui::Button("random")) newSeed = rand();
        if (ImGui::Button("Create & Play") && newName[0]) {
            playPath = std::string("worlds/") + newName + ".bin";
            playCX = sizes[newSizeIdx]; playCZ = sizes[newSizeIdx];
            playSeed = newSeed;
            playNew = true;
        }
        ImGui::Separator();
        ImGui::Text("Settings:");
        {
            float v = cvar.get("cam.fov", 70.0f);
            if (ImGui::SliderFloat("fov", &v, 30.0f, 110.0f)) cvar.set("cam.fov", v);
            v = cvar.get("sun.gamma", 1.2f);
            if (ImGui::SliderFloat("gamma", &v, 0.5f, 4.0f)) cvar.set("sun.gamma", v);
            v = cvar.get("fog.far", 260.0f);
            if (ImGui::SliderFloat("fog far", &v, 50.0f, 500.0f)) cvar.set("fog.far", v);
        }
        ImGui::Text("Texture pack:");
        {
            if (menuPackIdx >= (int)packNames.size()) menuPackIdx = 0;
            ImGui::Combo("##tpack", &menuPackIdx, [](void* d, int i) { return (*(std::vector<std::string>*)d)[i].c_str(); },
                         (void*)&packNames, (int)packNames.size());
            static char addT[512] = "";
            ImGui::InputText("folder##t", addT, sizeof(addT));
            ImGui::SameLine();
            if (ImGui::Button("Add pack")) {
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
        }
        ImGui::Text("Shader pack:");
        ImGui::Combo("##spack", &menuShaderSel, [](void* d, int i) { return (*(std::vector<std::string>*)d)[i].c_str(); },
                     (void*)&shaderPacks, (int)shaderPacks.size());
        {
            static char addS[512] = "";
            ImGui::InputText("folder##s", addS, sizeof(addS));
            ImGui::SameLine();
            if (ImGui::Button("Add shaders")) {
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
        }
        if (ImGui::Button("Save settings")) cvar.exec("save");
        ImGui::SameLine();
        if (ImGui::Button("Quit")) wantQuit = true;
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    if (wantQuit || playPath.empty()) {
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
    auto sh = [&](const char* n) { return shaderDir + "/" + n; };
    Shader lightingShader(sh("lighting.vs").c_str(), sh("lighting.fs").c_str());
    Shader lineShader(sh("line.vs").c_str(), sh("outline.fs").c_str());
    Shader crosshairShader(sh("crosshair.vs").c_str(), sh("crosshair.fs").c_str());
    Shader skyShader(sh("sky.vs").c_str(), sh("sky.fs").c_str());
    auto reloadShaders = [&]() {
        lightingShader.load(sh("lighting.vs").c_str(), sh("lighting.fs").c_str());
        lineShader.load(sh("line.vs").c_str(), sh("outline.fs").c_str());
        crosshairShader.load(sh("crosshair.vs").c_str(), sh("crosshair.fs").c_str());
        skyShader.load(sh("sky.vs").c_str(), sh("sky.fs").c_str());
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
        std::cout << "No world, back to menu is unsupported — exiting\n";
        return 0;
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
    int curPCX = -1, curPCZ = -1;
    auto ensureAround = [&]() {
        int pcx = (int)player.pos.x / 16, pcz = (int)player.pos.z / 16;
        if (pcx == curPCX && pcz == curPCZ) return;
        curPCX = pcx; curPCZ = pcz;
        size_t nv = 0;
        for (int cz = 0; cz < NCZ; cz++)
            for (int cx = 0; cx < NCX; cx++) {
                int dd = std::max(abs(cx - pcx), abs(cz - pcz));
                if (dd <= VIEW_R) { if (!meshLoaded[midx(cx, cz)]) rebuild(cx, cz); nv += meshes[midx(cx, cz)].vertexCount; }
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
        packDir = nd;
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

    lightingShader.use();
    lightingShader.setInt("material.diffuse",  0);
    lightingShader.setInt("material.specular", 1);

    std::cout << "\nWASD move, Space jump/up, C down (fly), V fly/walk, F flashlight, L lamps, 1/2/3 block, LMB break, RMB place, F5 save, F9 load.\n";

    bool prevL = false, prevR = false, prevF5 = false, prevF9 = false;
    bool prevV = false, prevF = false, prevG = false, prevF1 = false, prevGrave = false, prevEsc = false;
    bool flashOn = true, followOn = true;
    bool prev1 = false, prev2 = false, prev3 = false;
    int placeId = 1;
    int shPackIdx = 0;
    float tickAcc = 0.0f, tod = 0.56f;
    glm::vec3 pointLightPositions[4];

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame; lastFrame = now;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        processInput(window);

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
        if (esc && !prevEsc && console.open) console.open = false,
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        prevEsc = esc;

        // --- PLAYER ---
        bool curV = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
        bool curF = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        bool curG = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
        bool curL = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool curR = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool curF5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        bool curF9 = glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
        if (console.open) {
            prevV = curV; prevF = curF; prevG = curG;
            prevL = curL; prevR = curR; prevF5 = curF5; prevF9 = curF9;
        }
        if (!console.open && curV && !prevV) {
            player.fly = !player.fly;
            player.vel = glm::vec3(0.0f);
            std::cout << (player.fly ? "FLY\n" : "WALK\n");
        }
        prevV = curV;
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
        if (!console.open) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) mv.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) mv.x -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) mv.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) mv.y -= 1.0f;
        jump = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        down = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
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
        camera.Position = player.pos + worldOffset + glm::vec3(0.0f, player.eye, 0.0f);
        ensureAround();

        glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(cvar.get("cam.fov", 70.0f)), 1280.0f/720.0f, 0.1f, 600.0f);
        glm::mat4 view = camera.GetViewMatrix();
        Frustum frustum = Frustum::fromVP(projection * view);
        auto chunkVisible = [&](int cx, int cz) {
            glm::vec3 mn = worldOffset + glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f);
            return frustum.visible(mn, mn + glm::vec3(16.0f));
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
        if (hasHit && !console.open) {
            if (curL && !prevL) {
                world->setBlock(wx, wy, wz, 0);
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
                    touchEdit(px, pz);
                }
            }
        }
        prevL = curL; prevR = curR;
        // выбор блока: 1 трава 2 земля 3 камень
        bool c1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
        bool c2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
        bool c3 = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
        if (!console.open && c1 && !prev1) { placeId = 1; std::cout << "block: grass\n"; }
        if (!console.open && c2 && !prev2) { placeId = 2; std::cout << "block: dirt\n"; }
        if (!console.open && c3 && !prev3) { placeId = 3; std::cout << "block: stone\n"; }
        prev1 = c1; prev2 = c2; prev3 = c3;
        if (!console.open && curF5 && !prevF5) {
            if (saveWorld(*world, playPath.c_str())) std::cout << "Saved " << playPath << "\n";
            else std::cout << "Save FAILED\n";
        }
        if (!console.open && curF9 && !prevF9) {
            if (loadWorld(*world, playPath.c_str())) { rebuildAll(); std::cout << "Loaded " << playPath << "\n"; }
            else std::cout << "Load FAILED\n";
        }
        prevF5 = curF5; prevF9 = curF9;

        // PASS 1: opaque. Только загруженные видимые меши.
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilMask(0xFF);

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
            slider("cam.fov", "cam.fov", 30.0f, 110.0f);
            slider("walk", "move.walk", 1.0f, 12.0f);
            slider("fly", "move.fly", 2.0f, 30.0f);
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
            ImGui::Checkbox("fly (V)", &player.fly);
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

    glDeleteVertexArrays(1, &lineVAO);
    glDeleteBuffers(1, &lineVBO);
    glDeleteVertexArrays(1, &triVAO);
    for (auto& m : meshes) m.destroy();
    for (auto& m : waterMeshes) m.destroy();
    for (auto& m : lavaMeshes) m.destroy();
    glDeleteTextures(1, &diffuseMap);
    glDeleteTextures(1, &specularMap);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
