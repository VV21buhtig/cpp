// Glad 2 — инклуд <glad/gl.h>, и он ДО GLFW
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <stb/stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <map>
#include "shader.h"
#include "camera.h"

Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
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
    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD,  deltaTime);
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT,     deltaTime);
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT,    deltaTime);
}

unsigned int loadTexture(const char* path) {
    unsigned int tex; glGenTextures(1, &tex);
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 0);
    if (data) {
        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        std::cout << "Loaded: " << path << "\n";
    } else std::cerr << "Failed: " << path << "\n";
    stbi_image_free(data);
    return tex;
}

glm::mat4 cubeModelMatrix(glm::vec3 pos, unsigned int index) {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, pos);
    m = glm::rotate(m, glm::radians(20.0f * index), glm::vec3(1.0f, 0.3f, 0.5f));
    return m;
}

// ---------------------------------------------------------
//  Ray-AABB — выбор куба под прицелом (глава 23)
// ---------------------------------------------------------
float rayAABBLocal(glm::vec3 origin, glm::vec3 dir) {
    float tmin = 0.0f, tmax = 1e9f;
    for (int a = 0; a < 3; a++) {
        float o = origin[a], d = dir[a];
        if (fabs(d) < 1e-6f) {
            if (o < -0.5f || o > 0.5f) return -1.0f;
        } else {
            float t1 = (-0.5f - o) / d;
            float t2 = ( 0.5f - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1.0f;
        }
    }
    return tmin;
}

int pickCube(glm::vec3 rayOrigin, glm::vec3 rayDir,
             const glm::vec3* positions, int count, float maxDist) {
    int bestIdx = -1;
    float bestT = maxDist;
    for (int i = 0; i < count; i++) {
        glm::mat4 model = cubeModelMatrix(positions[i], i);
        glm::mat4 inv   = glm::inverse(model);
        glm::vec3 lo = glm::vec3(inv * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 ld = glm::vec3(inv * glm::vec4(rayDir,    0.0f));
        float t = rayAABBLocal(lo, ld);
        if (t > 0.0f && t < bestT) { bestT = t; bestIdx = i; }
    }
    return bestIdx;
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

    // =========================================================
    //  DEPTH + STENCIL + BLEND (главы 22-24)
    // =========================================================
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Shader lightingShader("shaders/lighting.vs", "shaders/lighting.fs");
    Shader lightCubeShader("shaders/lighting.vs", "shaders/light_cube.fs");
    Shader alphaShader   ("shaders/lighting.vs", "shaders/alpha.fs");
    Shader outlineShader ("shaders/lighting.vs", "shaders/outline.fs");

    glm::vec3 cubePositions[] = {
        glm::vec3( 0.0f,  0.0f,   0.0f),
        glm::vec3( 2.0f,  5.0f, -15.0f),
        glm::vec3(-1.5f, -2.2f,  -2.5f),
        glm::vec3(-3.8f, -2.0f, -12.3f),
        glm::vec3( 2.4f, -0.4f,  -3.5f),
        glm::vec3(-1.7f,  3.0f,  -7.5f),
        glm::vec3( 1.3f, -2.0f,  -2.5f),
        glm::vec3( 1.5f,  2.0f,  -2.5f),
        glm::vec3( 1.5f,  0.2f,  -1.5f),
        glm::vec3(-1.3f,  1.0f,  -1.5f),
    };
    const int NR_CUBES = 10;

    glm::vec3 pointLightPositions[] = {
        glm::vec3( 0.7f,  0.2f,   2.0f),
        glm::vec3( 2.3f, -3.3f,  -4.0f),
        glm::vec3(-4.0f,  2.0f, -12.0f),
        glm::vec3( 0.0f,  0.0f,  -3.0f),
    };

    glm::vec3 transparentPositions[] = {
        glm::vec3( 0.5f,  0.5f,   1.5f),
        glm::vec3( 1.0f,  1.0f,   0.5f),
        glm::vec3( 0.0f,  0.0f,   2.5f),
        glm::vec3(-1.0f,  0.5f,   1.0f),
        glm::vec3( 0.5f, -0.5f,   0.5f),
    };
    const int NR_TRANSPARENT = 5;

    // =========================================================
    //  КУБ — массив из главы 15 (position + normal + uv)
    // =========================================================
    float vertices[] = {
        // positions          // normals           // texture coords
        // back (-z) — CCW fix (глава 25)
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

        // right (+x) — CCW fix (глава 25)
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

        // top (+y) — CCW fix (глава 25)
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
    };

    unsigned int VBO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    unsigned int cubeVAO;
    glGenVertexArrays(1, &cubeVAO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);

    unsigned int lightCubeVAO;
    glGenVertexArrays(1, &lightCubeVAO);
    glBindVertexArray(lightCubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    unsigned int diffuseMap  = loadTexture("texture/container2.png");
    unsigned int specularMap = loadTexture("texture/container2_specular.png");

    lightingShader.use();
    lightingShader.setInt("material.diffuse",  0);
    lightingShader.setInt("material.specular", 1);

    std::cout << "\n10 контейнеров, 5 прозрачных кубов, outline на наведённом.\n";

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame; lastFrame = now;
        processInput(window);

        glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), 1280.0f/720.0f, 0.5f, 50.0f);
        glm::mat4 view = camera.GetViewMatrix();

        // ---- PICK ----
        int selectedCube = pickCube(camera.Position, camera.Front,
                                    cubePositions, NR_CUBES, 50.0f);

        // =====================================================
        //  PASS 1: непрозрачные (пишут в stencil)
        // =====================================================
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
            lightingShader.setVec3 (b + "position", pointLightPositions[i]);
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

        glBindVertexArray(cubeVAO);
        for (int i = 0; i < NR_CUBES; i++) {
            glm::mat4 model = cubeModelMatrix(cubePositions[i], i);
            lightingShader.setMat4("model", model);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        // лампы
        glStencilMask(0x00);
        lightCubeShader.use();
        lightCubeShader.setMat4("projection", projection);
        lightCubeShader.setMat4("view", view);
        glBindVertexArray(lightCubeVAO);
        for (int i = 0; i < 4; i++) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, pointLightPositions[i]);
            model = glm::scale(model, glm::vec3(0.2f));
            lightCubeShader.setMat4("model", model);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        // =====================================================
        //  PASS 2: OUTLINE — только выбранного куба
        // =====================================================
        if (selectedCube >= 0) {
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilMask(0x00);
            glDisable(GL_DEPTH_TEST);

            outlineShader.use();
            outlineShader.setMat4("projection", projection);
            outlineShader.setMat4("view", view);

            glm::mat4 model = cubeModelMatrix(cubePositions[selectedCube], selectedCube);
            model = glm::scale(model, glm::vec3(1.05f));
            outlineShader.setMat4("model", model);

            glBindVertexArray(cubeVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);

            glEnable(GL_DEPTH_TEST);
        }

        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);

        // =====================================================
        //  PASS 3: прозрачные кубы
        // =====================================================
        std::map<float, glm::vec3> sorted;
        for (int i = 0; i < NR_TRANSPARENT; i++) {
            float d = glm::length(camera.Position - transparentPositions[i]);
            sorted[d] = transparentPositions[i];
        }

        alphaShader.use();
        alphaShader.setMat4("projection", projection);
        alphaShader.setMat4("view", view);
        alphaShader.setVec3("color", 0.4f, 0.7f, 0.9f);
        alphaShader.setFloat("alpha", 0.35f);

        glDepthMask(GL_FALSE);
        glBindVertexArray(cubeVAO);
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, it->second);
            alphaShader.setMat4("model", model);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        glDepthMask(GL_TRUE);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteVertexArrays(1, &lightCubeVAO);
    glDeleteBuffers(1, &VBO);
    glDeleteTextures(1, &diffuseMap);
    glDeleteTextures(1, &specularMap);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}