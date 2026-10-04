// vk_ctx: общий контекст Vulkan + хелперы. Поведения нет, только хранение.
// Порядок создания ресурсов задают make*-функции (тот же что был в main),
// общий del в VkCore чистит всё в обратном порядке при выходе.
#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <stb/stb_image.h>
#include "vk_mem_alloc.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#define VK_CHECK(x) do { VkResult r = (x); \
    if (r != VK_SUCCESS) { printf("VULKAN FAIL %d @ %d\n", r, __LINE__); exit(1); } } while (0)

struct DeletionQueue {
    std::vector<std::function<void()>> fns;
    void push(std::function<void()>&& f) { fns.emplace_back(std::move(f)); }
    void flush() { for (auto it = fns.rbegin(); it != fns.rend(); ++it) (*it)(); fns.clear(); }
};

std::vector<char> readFile(const char* path);
VkShaderModule makeShader(VkDevice dev, const char* path);
// ПЕРЕХОД картинки: явный барьер (layout — состояние, менять только так).
void imgBarrier(VkCommandBuffer cb, VkImage img, VkImageLayout oldL, VkImageLayout newL,
                VkImageAspectFlags aspect, uint32_t baseMip, uint32_t mipCount,
                VkPipelineStageFlags srcS, VkAccessFlags srcA,
                VkPipelineStageFlags dstS, VkAccessFlags dstA);

// Кадр CPU-зеркало UBO (std140: всё по 16 байт, итого 368: +prevViewProj под TAA,
// +NJ-пара под MV для FSR2; добавление в КОНЕЦ старые шейдеры не ломает).
struct FrameUBO {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 sunDir, sunCol, ambSky, ambGnd, fog, misc, viewPos;
    glm::mat4 prevViewProj; // demo-7: VP прошлого кадра (с джиттером)
    glm::mat4 viewProjNJ; // FSR2/MV: текущий VP БЕЗ джиттера
    glm::mat4 invViewProjNJ; // FSR2/MV: его инверсия (считаем на CPU)
    glm::mat4 prevViewProjNJ; // FSR2/MV: прошлый VP БЕЗ джиттера
};

// Мышь: захват + look (стрелки тоже работают). ESC — выход.
struct CamCtl { float yaw, pitch, lastX, lastY; bool first; };

struct VkCore {
    GLFWwindow* window = nullptr;
    CamCtl ctl{-90.0f, -20.0f, 640.0f, 360.0f, true};
    VkInstance instance = nullptr;
    VkDebugUtilsMessengerEXT dbgMessenger = nullptr;
    VkSurfaceKHR surface = nullptr;
    VkPhysicalDevice gpu = nullptr;
    uint32_t gfxFamily = ~0u;
    float maxAniso = 1.0f;
    VkDevice device = nullptr;
    VkQueue gfxQueue = nullptr;
    VmaAllocator alloc = nullptr;
    VkSwapchainKHR swapchain = nullptr;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D swapExtent{1280, 720};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    VkCommandPool immPool = nullptr;
    VkCommandBuffer immBuf = nullptr;
    VkFence immFence = nullptr;
    DeletionQueue del;
    // Разовые команды (загрузки, переходы): свой пул + fence.
    void immRun(std::function<void(VkCommandBuffer)> fn);
};

// Всё от glfwInit до imm fence включительно. Печатает GPU. false = нет GPU.
bool vkInitCore(VkCore& c);
