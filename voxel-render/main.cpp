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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <iostream>
#include "shader.h"
#include "camera.h"
#include "game/console.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <unistd.h>
#include <fcntl.h>

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
void scroll_callback(GLFWwindow*, double, double) { /* зум только из консоли cam.fov */ }
void processInput(GLFWwindow* w) {
    if (!console.open && glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(w, true);
    // WASD — через Player::update, камера следует за игроком
}

static const int VIEW_R = 4; // радиус мешей вокруг чанка игрока

int main()
{
    if (!glfwInit()) { std::cerr << "GLFW fail\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Render", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (gladLoadGL(glfwGetProcAddress) == 0) { glfwTerminate(); return -1; }
    std::cout << "RENDERER: " << glGetString(GL_RENDERER) << "\n";

    // DEPTH + STENCIL + CULL + BLEND (главы 22-25). Порядок не менять.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Shader lightingShader("shaders/lighting.vs", "shaders/lighting.fs");
    Shader lineShader("shaders/line.vs", "shaders/outline.fs");
    Shader crosshairShader("shaders/crosshair.vs", "shaders/crosshair.fs");
    Shader skyShader("shaders/sky.vs", "shaders/sky.fs");



    unsigned int triVAO = 0;
    glGenVertexArrays(1, &triVAO);
    glBindVertexArray(triVAO);
    glBindVertexArray(0);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    // Ограниченный мир 16x16 чанков по сиду. Данные всегда в RAM, стримятся МЕШИ.
    const int W = World::CX * 16; // 256
    const glm::vec3 worldOffset(-W / 2.0f, 0.0f, -W / 2.0f);
    static World world(1337); // static: 1MB, не на стеке
    const char* savePath = "world.bin";
    if (loadWorld(world, savePath)) std::cout << "Loaded " << savePath << "\n";
    else std::cout << "New world (no " << savePath << ")\n";
    Player player;
    player.spawn(world, W / 2, W / 2);

    ChunkMesh meshes[World::CX][World::CZ];
    bool meshLoaded[World::CX][World::CZ] = {};
    auto rebuild = [&](int cx, int cz) {
        if (cx < 0 || cx >= World::CX || cz < 0 || cz >= World::CZ) return;
        meshes[cx][cz].upload(world.buildChunk(cx, cz));
        meshLoaded[cx][cz] = true;
    };
    auto unload = [&](int cx, int cz) {
        if (cx < 0 || cx >= World::CX || cz < 0 || cz >= World::CZ) return;
        meshes[cx][cz].destroy();
        meshLoaded[cx][cz] = false;
    };
    auto rebuildAll = [&]() {
        for (int cz = 0; cz < World::CZ; cz++)
            for (int cx = 0; cx < World::CX; cx++)
                rebuild(cx, cz);
    };
    int curPCX = -1, curPCZ = -1;
    auto ensureAround = [&]() {
        int pcx = (int)player.pos.x / 16, pcz = (int)player.pos.z / 16;
        if (pcx == curPCX && pcz == curPCZ) return;
        curPCX = pcx; curPCZ = pcz;
        size_t nv = 0;
        for (int cz = 0; cz < World::CZ; cz++)
            for (int cx = 0; cx < World::CX; cx++) {
                int dd = std::max(abs(cx - pcx), abs(cz - pcz));
                if (dd <= VIEW_R) { if (!meshLoaded[cx][cz]) rebuild(cx, cz); nv += meshes[cx][cz].vertexCount; }
                else if (meshLoaded[cx][cz]) unload(cx, cz);
            }
        std::cout << "stream chunk " << pcx << "," << pcz << " verts " << nv << "\n";
    };
    auto touchEdit = [&](int wx, int wz) {
        int cx = wx / 16, cz = wz / 16;
        if (cx < 0 || cx >= World::CX || cz < 0 || cz >= World::CZ) return; // наружу за границу
        if (meshLoaded[cx][cz]) rebuild(cx, cz);
        if (wx % 16 == 0 && cx > 0 && meshLoaded[cx-1][cz]) rebuild(cx - 1, cz);
        if (wx % 16 == 15 && cx < World::CX - 1 && meshLoaded[cx+1][cz]) rebuild(cx + 1, cz);
        if (wz % 16 == 0 && cz > 0 && meshLoaded[cx][cz-1]) rebuild(cx, cz - 1);
        if (wz % 16 == 15 && cz < World::CZ - 1 && meshLoaded[cx][cz+1]) rebuild(cx, cz + 1);
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

    unsigned int diffuseMap  = loadTileArray("texture/tiles");
    // земля/трава/камень матовые: спекуляр глушим чёрной 1x1 (металлик от контейнера снят)
    unsigned int specularMap = 0;
    {
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

    // Консоль света: крутишь из терминала (окно отдаёт фокус терминалу), save в gfx.cfg
    CVarSys cvar;
    cvar.reg("sun.i", 1.0f);     // прямой солнечный свет
    cvar.reg("sun.amb", 3.0f);   // ambient всего
    cvar.reg("sun.sat", 1.8f);   // насыщенность дня
    cvar.reg("sun.gamma", 1.2f); // гамма террейна
    cvar.reg("time.speed", 600.0f); // длина суток, 0 = стоп
    cvar.reg("fog.near", 0.0f);
    cvar.reg("fog.far", 260.0f);
    cvar.reg("cam.fov", 70.0f);
    cvar.reg("move.walk", 4.3f);
    cvar.reg("move.fly", 8.0f);
    cvar.reg("move.jump", 7.5f);
    cvar.reg("move.bhop", 0.0f);
    cvar.reg("tick.rate", 120.0f);
    cvar.load("gfx.cfg");
    gCvar = &cvar;
    cvar.onPrint = [](const std::string& s) { console.print(s); };
    console.print("console F1. try: set sun.i 2");
    std::cout << "console: F1 in game, or stdin+Enter. Try: set sun.i 2\n";
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    std::string conLine;

    std::cout << "\nWASD ходить, Space прыжок/вверх, C вниз (fly), V fly/walk, F фонарик, L лампы, 1/2/3 блок, LMB сломать, RMB поставить, F5 сейв, F9 загрузка.\n";

    bool prevL = false, prevR = false, prevF5 = false, prevF9 = false;
    bool prevV = false, prevF = false, prevG = false;
    bool flashOn = true, followOn = true;
    int placeId = 1;
    bool prev1 = false, prev2 = false, prev3 = false;
    bool prevF1 = false, prevGrave = false, prevEsc = false;

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
                    cvar.exec(conLine.substr(0, p));
                    conLine.erase(0, p + 1);
                }
            }
        }

        // --- F1/~ — консоль ImGui (курсор наружу/внутрь), ESC закрывает ---
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

        // --- PLAYER: V — fly/walk, F — фонарик, L — лампы, камера = глаза ---
        // консоль открыта: ввод глушим, фронты сбрасываем чтобы не выстрелило при закрытии
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
        static float tickAcc = 0.0f; // фикс. тикрейт физики
        float rate = cvar.get("tick.rate", 120.0f);
        if (rate < 30.0f) rate = 30.0f;
        if (rate > 240.0f) rate = 240.0f;
        tickAcc += deltaTime;
        float h = 1.0f / rate;
        if (tickAcc > h * 8) tickAcc = h * 8;
        while (tickAcc >= h) {
            player.update(h, world, mv, glm::radians(camera.Yaw), jump, down);
            tickAcc -= h;
        }
        // упал за мир — респаун в центр
        if (player.pos.y < -10.0f) player.spawn(world, W / 2, W / 2);
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

        // ---- DAY CYCLE: солнце крутится 240с, тянет свет/небо/туман ----
        // День 10 минут как в MC + старт утром (высота ~30°), а не на самой кромке.
        // time.speed=0 стопает время; фаза живёт в tod.
        static float tod = 0.56f;
        float spd = cvar.get("time.speed", 600.0f);
        if (spd > 0.0f) tod += deltaTime * 6.2831853f / spd;
        float sunA = tod;
        glm::vec3 sunVec = glm::normalize(glm::vec3(cos(sunA), sin(sunA), 0.35f));
        // Тайминг как у людей: сумерки раньше прямого света (буфер ниже горизонта),
        // ambient тёплый ведёт, direct догоняет. Роблокс так и делает: cutoff y>-0.3.
        float twi = glm::smoothstep(-0.14f, 0.02f, sunVec.y);   // сумерки: небо/ambient
        float morn = glm::smoothstep(-0.02f, 0.06f, sunVec.y);  // свет почти сразу с восходом
        float noonCut = 1.0f - 0.20f * glm::smoothstep(0.5f, 0.95f, sunVec.y); // полдень чуть мягче
        float sunI = morn * noonCut;
        float dayF = twi;
        float nightF = 1.0f - dayF;
        glm::vec3 topColor = glm::mix(glm::vec3(0.008f, 0.015f, 0.05f), glm::vec3(0.30f, 0.55f, 0.92f), dayF);
        glm::vec3 horizonColor = glm::mix(glm::vec3(0.04f, 0.06f, 0.11f), glm::vec3(0.74f, 0.83f, 0.93f), dayF);



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

        // ---- PICK по миру DDA (луч в координатах чанков) ----
        glm::vec3 rayO = camera.Position - worldOffset;
        int wx = -1, wy = -1, wz = -1;
        glm::vec3 hitN(0.0f);
        float hitT = world.pick(rayO, camera.Front, 100.0f, wx, wy, wz, hitN);
        bool hasHit = (hitT > 0.0f);

        // ---- BREAK / PLACE (по фронту нажатия; консоль глушит) ----
        if (hasHit && !console.open) {
            if (curL && !prevL) {
                world.setBlock(wx, wy, wz, 0); // дно тоже роется: под миром пустота, упадёшь — респаун
                touchEdit(wx, wz);
            }
            if (curR && !prevR) {
                int px = wx + (int)hitN.x, py = wy + (int)hitN.y, pz = wz + (int)hitN.z;
                // в себя ставить нельзя: клетка vs AABB игрока
                bool inPlayer = (px + 1 > player.pos.x - player.halfW && px < player.pos.x + player.halfW &&
                                 py + 1 > player.pos.y && py < player.pos.y + player.height &&
                                 pz + 1 > player.pos.z - player.halfW && pz < player.pos.z + player.halfW);
                if (world.getBlock(px, py, pz) == 0 && !inPlayer) {
                    world.setBlock(px, py, pz, (unsigned char)placeId);
                    touchEdit(px, pz);
                }
            }
        }
        prevL = curL; prevR = curR;
        // выбор блока: 1 трава 2 земля 3 камень
        bool c1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
        bool c2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
        bool c3 = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
        if (!console.open && c1 && !prev1) { placeId = 1; std::cout << "hold: grass\n"; }
        if (!console.open && c2 && !prev2) { placeId = 2; std::cout << "hold: dirt\n"; }
        if (!console.open && c3 && !prev3) { placeId = 3; std::cout << "hold: stone\n"; }
        prev1 = c1; prev2 = c2; prev3 = c3;
        if (!console.open && curF5 && !prevF5) {
            if (saveWorld(world, savePath)) std::cout << "Saved " << savePath << "\n";
            else std::cout << "Save FAILED\n";
        }
        if (!console.open && curF9 && !prevF9) {
            if (loadWorld(world, savePath)) { rebuildAll(); std::cout << "Loaded " << savePath << "\n"; }
            else std::cout << "Load FAILED\n";
        }
        prevF5 = curF5; prevF9 = curF9;

        // PASS 1: opaque (пишут в stencil). Только загруженные меши.
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilMask(0xFF);

        lightingShader.use();
        lightingShader.setFloat("material.shininess", 32.0f);
        lightingShader.setVec3("viewPos", camera.Position);
        // туман в линейном (до гаммы): инверсия чтобы сойтись с небом без гаммы
        glm::vec3 fogLin(
            pow(horizonColor.x, 2.2f), pow(horizonColor.y, 2.2f), pow(horizonColor.z, 2.2f));
        lightingShader.setVec3("fogColor", fogLin);
        lightingShader.setVec2("fogRange", cvar.get("fog.near", 90.0f), cvar.get("fog.far", 260.0f));
        lightingShader.setFloat("satU", cvar.get("sun.sat", 1.3f));
        lightingShader.setFloat("gammaU", cvar.get("sun.gamma", 2.2f));

        float duskF = glm::clamp(1.0f - glm::abs(sunVec.y) / 0.25f, 0.0f, 1.0f) * twi; // тёплые сумерки
        glm::vec3 sunCol = glm::mix(glm::vec3(1.0f, 0.55f, 0.25f), glm::vec3(1.0f, 0.97f, 0.9f),
                                    glm::smoothstep(0.0f, 0.4f, sunVec.y)); // низкое = оранжевое
        glm::vec3 ambDay = glm::mix(glm::vec3(0.03f, 0.035f, 0.07f), glm::vec3(0.42f), dayF);
        ambDay *= cvar.get("sun.amb", 1.0f);
        lightingShader.setVec3("dirLight.direction", -sunVec);
        lightingShader.setVec3("dirLight.ambient",   glm::mix(ambDay, glm::vec3(0.34f, 0.25f, 0.16f), duskF * 0.6f));
        lightingShader.setVec3("dirLight.diffuse",   glm::mix(glm::vec3(0.015f), sunCol * (1.7f * cvar.get("sun.i", 1.0f)), sunI));
        lightingShader.setVec3("dirLight.specular",  glm::mix(glm::vec3(0.01f), sunCol * 0.3f, sunI));

        // лампы следуют за игроком (мир большой, статика у центра бесполезна)
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

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, diffuseMap);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, specularMap);

        for (int cz = 0; cz < World::CZ; cz++)
            for (int cx = 0; cx < World::CX; cx++) {
                if (!meshLoaded[cx][cz] || !chunkVisible(cx, cz)) continue;
                glm::vec3 off = worldOffset + glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), off);
                lightingShader.setMat4("model", model);
                meshes[cx][cz].draw();
            }

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
            model = glm::scale(model, glm::vec3(1.004f)); // 0.002 наружу: без z-fight, кольцо у плато видно
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
            ImGui::Checkbox("flash (F)", &flashOn);
            ImGui::Checkbox("lamps (L)", &followOn);
            ImGui::Checkbox("fly (V)", &player.fly);
            bool bhop = cvar.get("move.bhop", 0.0f) > 0.5f;
            if (ImGui::Checkbox("bhop on space", &bhop)) cvar.set("move.bhop", bhop ? 1.0f : 0.0f);
            ImGui::Separator();
            ImGui::BeginChild("log", ImVec2(0, 200), true);
            for (auto& ln : console.lines) ImGui::TextUnformatted(ln.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            if (ImGui::InputText("cmd", console.inputBuf, sizeof(console.inputBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                cvar.exec(console.inputBuf);
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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    for (int cz = 0; cz < World::CZ; cz++)
        for (int cx = 0; cx < World::CX; cx++)
            meshes[cx][cz].destroy();
    glDeleteTextures(1, &diffuseMap);
    glDeleteTextures(1, &specularMap);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
