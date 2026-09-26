// Glad 2 — инклуд <glad/gl.h>, и он ДО GLFW
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "engine/mesh.h"
#include "engine/texture.h"
#include "engine/atlas.h"
#include "engine/chunk.h"
#include "engine/world.h"
#include "engine/save.h"
#include "game/player.h"
#include "game/pick.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include "shader.h"
#include "camera.h"

Camera camera(glm::vec3(8.0f, 6.0f, 14.0f));
float lastX = 640.0f, lastY = 360.0f;
bool  firstMouse = true;
float deltaTime = 0.0f, lastFrame = 0.0f;

void framebuffer_size_callback(GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); }
void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xo = (float)xpos - lastX, yo = lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    camera.ProcessMouseMovement(xo, yo);
}
void scroll_callback(GLFWwindow*, double, double y) { camera.ProcessMouseScroll((float)y); }
void processInput(GLFWwindow* w) {
    if (glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(w, true);
    // WASD — через Player::update, камера следует за игроком
}

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

    glm::vec3 pointLightPositions[] = {
        glm::vec3(24.0f, 6.0f, 26.0f),
        glm::vec3(10.0f, 5.0f, 10.0f),
        glm::vec3(38.0f, 6.0f, 38.0f),
        glm::vec3(24.0f, 4.0f, 24.0f),
    };

    // Чистая мапа: мир 3x3 чанка, пол y=0. Никаких висяков.
    // Мировые координаты блоков 0..48, рендерим со сдвигом чтобы центр был в нуле.
    const glm::vec3 worldOffset(-24.0f, 0.0f, -24.0f);
    World world;
    const char* savePath = "world.bin";
    if (loadWorld(world, savePath)) std::cout << "Loaded " << savePath << "\n";
    else std::cout << "New world (no " << savePath << ")\n";
    Player player;
    player.spawn(world, 24, 24);
    ChunkMesh meshes[World::CX][World::CZ];
    auto rebuild = [&](int cx, int cz) {
        meshes[cx][cz].upload(world.buildChunk(cx, cz));
    };
    auto rebuildAll = [&]() {
        for (int cz = 0; cz < World::CZ; cz++)
            for (int cx = 0; cx < World::CX; cx++)
                rebuild(cx, cz);
    };
    size_t totalVerts = 0;
    for (int cz = 0; cz < World::CX; cz++)
        for (int cx = 0; cx < World::CX; cx++) {
            rebuild(cx, cz);
            totalVerts += meshes[cx][cz].vertexCount;
        }
    std::cout << "World 3x3 verts: " << totalVerts << "\n";

    // Линии рёбер куба [0,1]^3 — подсветка поверх граней, depth ON (не режется stencil).
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

    unsigned int diffuseMap  = makeAtlas2("texture/container2.png", "texture/container2.png");
    unsigned int specularMap = loadTexture("texture/container2_specular.png");

    lightingShader.use();
    lightingShader.setInt("material.diffuse",  0);
    lightingShader.setInt("material.specular", 1);

    std::cout << "\nWASD ходить, Space прыжок/вверх, C вниз (fly), F fly/walk, LMB сломать, RMB поставить, F5 сейв, F9 загрузка.\n";

    bool prevL = false, prevR = false, prevF5 = false, prevF9 = false, prevF = false;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame; lastFrame = now;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        processInput(window);

        // --- PLAYER (движок): F — fly/walk, камера = глаза ---
        bool curF = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (curF && !prevF) {
            player.fly = !player.fly;
            player.vel = glm::vec3(0.0f);
            std::cout << (player.fly ? "FLY\n" : "WALK\n");
        }
        prevF = curF;
        glm::vec2 mv(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) mv.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) mv.x -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) mv.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) mv.y -= 1.0f;
        bool jump = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool down = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
        player.update(deltaTime, world, mv, glm::radians(camera.Yaw), jump, down);
        // player живёт в координатах мира (0..48), рендер сдвинут на worldOffset
        camera.Position = player.pos + worldOffset + glm::vec3(0.0f, player.eye, 0.0f);

        glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), 1280.0f/720.0f, 0.1f, 200.0f);
        glm::mat4 view = camera.GetViewMatrix();

        // ---- PICK по миру (луч в мировых координатах чанков) ----
        glm::vec3 rayO = camera.Position - worldOffset;
        int wx = -1, wy = -1, wz = -1;
        glm::vec3 hitN(0.0f);
        float hitT = world.pick(rayO, camera.Front, 100.0f, wx, wy, wz, hitN);
        bool hasHit = (hitT > 0.0f);

        // ---- BREAK / PLACE (по фронту нажатия) ----
        bool curL = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool curR = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (hasHit) {
            if (curL && !prevL) {
                if (!(wx >= 0 && wx < 48 && wy == 0 && wz >= 0 && wz < 48 && wx % 16 == 0)) {
                    // пол y=0 ломать можно, но не будем дырявить до пустоты? можно — разрешаем
                }
                world.setBlock(wx, wy, wz, 0);
                int cx = wx / 16, cz = wz / 16;
                rebuild(cx, cz);
                // шов: если на границе чанка — пересобрать соседа
                if (wx % 16 == 0 && cx > 0) rebuild(cx - 1, cz);
                if (wx % 16 == 15 && cx < 2) rebuild(cx + 1, cz);
                if (wz % 16 == 0 && cz > 0) rebuild(cx, cz - 1);
                if (wz % 16 == 15 && cz < 2) rebuild(cx, cz + 1);
            }
            if (curR && !prevR) {
                int px = wx + (int)hitN.x, py = wy + (int)hitN.y, pz = wz + (int)hitN.z;
                if (world.getBlock(px, py, pz) == 0) {
                    world.setBlock(px, py, pz, 1);
                    int cx = px / 16, cz = pz / 16;
                    if (cx >= 0 && cx < 3 && cz >= 0 && cz < 3) {
                        rebuild(cx, cz);
                        if (px % 16 == 0 && cx > 0) rebuild(cx - 1, cz);
                        if (px % 16 == 15 && cx < 2) rebuild(cx + 1, cz);
                        if (pz % 16 == 0 && cz > 0) rebuild(cx, cz - 1);
                        if (pz % 16 == 15 && cz < 2) rebuild(cx, cz + 1);
                    }
                }
            }
        }
        prevL = curL; prevR = curR;
        bool curF5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        bool curF9 = glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
        if (curF5 && !prevF5) {
            if (saveWorld(world, savePath)) std::cout << "Saved " << savePath << "\n";
            else std::cout << "Save FAILED\n";
        }
        if (curF9 && !prevF9) {
            if (loadWorld(world, savePath)) { rebuildAll(); std::cout << "Loaded " << savePath << "\n"; }
            else std::cout << "Load FAILED\n";
        }
        prevF5 = curF5; prevF9 = curF9;

        // PASS 1: opaque (пишут в stencil)
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilMask(0xFF);

        lightingShader.use();
        lightingShader.setFloat("material.shininess", 32.0f);
        lightingShader.setVec3("viewPos", camera.Position);

        lightingShader.setVec3("dirLight.direction", -0.2f, -1.0f, -0.3f);
        lightingShader.setVec3("dirLight.ambient",   0.05f, 0.05f, 0.05f);
        lightingShader.setVec3("dirLight.diffuse",   0.4f,  0.4f,  0.4f);
        lightingShader.setVec3("dirLight.specular",  0.5f,  0.5f,  0.5f);

        for (int i = 0; i < 4; i++) {
            std::string b = "pointLights[" + std::to_string(i) + "].";
            lightingShader.setVec3 (b + "position", pointLightPositions[i] + worldOffset);
            lightingShader.setVec3 (b + "ambient",   0.05f, 0.05f, 0.05f);
            lightingShader.setVec3 (b + "diffuse",   0.8f,  0.8f,  0.8f);
            lightingShader.setVec3 (b + "specular",  1.0f,  1.0f,  1.0f);
            lightingShader.setFloat(b + "constant",  1.0f);
            lightingShader.setFloat(b + "linear",    0.09f);
            lightingShader.setFloat(b + "quadratic", 0.032f);
        }

        lightingShader.setVec3 ("spotLight.position",  camera.Position);
        lightingShader.setVec3 ("spotLight.direction", camera.Front);
        lightingShader.setFloat("spotLight.cutOff",      glm::cos(glm::radians(12.5f)));
        lightingShader.setFloat("spotLight.outerCutOff", glm::cos(glm::radians(15.0f)));
        lightingShader.setVec3 ("spotLight.ambient",   0.0f, 0.0f, 0.0f);
        lightingShader.setVec3 ("spotLight.diffuse",   1.0f, 1.0f, 1.0f);
        lightingShader.setVec3 ("spotLight.specular",  1.0f, 1.0f, 1.0f);
        lightingShader.setFloat("spotLight.constant",  1.0f);
        lightingShader.setFloat("spotLight.linear",    0.09f);
        lightingShader.setFloat("spotLight.quadratic", 0.032f);

        lightingShader.setMat4("projection", projection);
        lightingShader.setMat4("view", view);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseMap);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, specularMap);

        for (int cz = 0; cz < World::CZ; cz++)
            for (int cx = 0; cx < World::CX; cx++) {
                glm::vec3 off = worldOffset + glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), off);
                lightingShader.setMat4("model", model);
                meshes[cx][cz].draw();
            }

        // PASS 2: подсветка рёбер — честный depth (прячется за стеной, стыки режет сосед)
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

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &lineVAO);
    glDeleteBuffers(1, &lineVBO);
    for (int cz = 0; cz < World::CZ; cz++)
        for (int cx = 0; cx < World::CX; cx++)
            meshes[cx][cz].destroy();
    glDeleteTextures(1, &diffuseMap);
    glDeleteTextures(1, &specularMap);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
