#ifndef ENGINE_RHI_VK_H
#define ENGINE_RHI_VK_H
// RHI-VK: сырой бэкенд Vulkan 1.1. Instance -> surface -> device/queue ->
// swapchain+views -> sync на N кадров + разовые команды + хост-буферы.
// Без валидационных слоёв, без сцены: только устройство и слова ошибок.
// RenderPass/framebuffer/pipeline — дело ядер (у каждого свои аттачменты).
#include <vulkan/vulkan.h>
#include <stdint.h>

#define RHI_VK_FIF 2

typedef struct {
    VkInstance inst;
    VkSurfaceKHR surf;
    VkPhysicalDevice pdev;
    char devName[256];
    VkDevice dev;
    VkQueue q;
    uint32_t qfam;
    VkSwapchainKHR swap;
    VkImage *imgs;        // calloc, swapN
    VkImageView *views;   // calloc, swapN
    uint32_t imgN;
    VkFormat imgFmt;
    VkExtent2D extent;
    VkCommandPool pool;
    VkCommandBuffer cmds[RHI_VK_FIF];
    VkSemaphore imgAvail[RHI_VK_FIF], renderDone[RHI_VK_FIF];
    VkFence fences[RHI_VK_FIF];
} RhiVk;

// Поднять всё вплоть до свопчейна (окно уже создано, GLFW_NO_API).
// extNames: доп. instance-расширения (0/NULL если нет). 1 ок, 0 нет.
int rhi_vk_init(RhiVk *r, void *glfwWindow, const char **extNames, uint32_t extN);
// Кадр: ждать забор, взять картинку. Возвращает индекс или -1.
int rhi_vk_acquire(RhiVk *r, int fi, uint32_t *img);
// Сабмит команд + present. 1 ок.
int rhi_vk_submit(RhiVk *r, int fi, uint32_t img, VkCommandBuffer cb);
// Разовая команда с ожиданием (аплоады на ините, не во фрейме).
int rhi_vk_once(RhiVk *r, VkCommandBuffer *cb);
int rhi_vk_once_end(RhiVk *r, VkCommandBuffer cb);
// Хост-буфер сразу замаплен (вершины/UBO/стейджинг). 1 ок.
int rhi_vk_hbuf(RhiVk *r, VkDeviceSize sz, VkBufferUsageFlags use,
                VkBuffer *b, VkDeviceMemory *m, void **ptr);
// Индекс памяти по маске+флагам, 0xFFFFFFFF если нет.
uint32_t rhi_vk_mem(RhiVk *r, uint32_t mask, VkMemoryPropertyFlags fl);
// Человеческий VkResult.
const char *rhi_vk_str(VkResult r);
void rhi_vk_shutdown(RhiVk *r);
#endif
