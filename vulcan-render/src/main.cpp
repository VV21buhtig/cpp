// vulcan-render demo-2: воксели. Мир/мешер/логика — из GL-прототипа как есть,
// рендер — Vulkan 1.3: VMA, depth, UBO+дескрипторы, texture array с мипами,
// поквадратный мешер (без greedy). Управление: WASD+стрелки, Space/C вверх/вниз.
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb/stb_image.h>
#include "vk_mem_alloc.h"

#include "engine/world.h"
#include "engine/blocks.h"
#include "mesh_vk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <functional>

#define VK_CHECK(x) do { VkResult r = (x); \
    if (r != VK_SUCCESS) { printf("VULKAN FAIL %d @ %d\n", r, __LINE__); exit(1); } } while (0)

struct DeletionQueue {
    std::vector<std::function<void()>> fns;
    void push(std::function<void()>&& f) { fns.emplace_back(std::move(f)); }
    void flush() { for (auto it = fns.rbegin(); it != fns.rend(); ++it) (*it)(); fns.clear(); }
};

static std::vector<char> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> b(n);
    if (fread(b.data(), 1, n, f) != (size_t)n) { printf("read fail %s\n", path); exit(1); }
    fclose(f);
    return b;
}

static VkShaderModule makeShader(VkDevice dev, const char* path) {
    auto code = readFile(path);
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &m));
    return m;
}

// Кадр CPU-зеркало UBO (std140: всё по 16 байт, итого 176).
struct FrameUBO {
    glm::mat4 viewProj;
    glm::vec4 sunDir, sunCol, ambSky, ambGnd, fog, misc, viewPos;
};

int main(int argc, char** argv) {
    int maxFrames = -1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);

    // ---- мир (та же генерация что в игре, сид 1337) ----
    World world;
    world.init(8, 8, 1337);
    const int W = world.sizeX();
    const glm::vec3 worldOffset(-W / 2.0f, 0.0f, -W / 2.0f);

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "vulcan-render demo-2 voxels", nullptr, nullptr);
    // Мышь: захват + look (стрелки тоже работают). ESC — выход.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    struct CamCtl { float yaw, pitch, lastX, lastY; bool first; };
    CamCtl ctl{-90.0f, -20.0f, 640.0f, 360.0f, true};
    glfwSetWindowUserPointer(window, &ctl);
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* c = (CamCtl*)glfwGetWindowUserPointer(w);
        if (c->first) { c->lastX = (float)x; c->lastY = (float)y; c->first = false; }
        float dx = (float)x - c->lastX, dy = c->lastY - (float)y;
        c->lastX = (float)x; c->lastY = (float)y;
        c->yaw += dx * 0.12f;
        c->pitch += dy * 0.12f;
        if (c->pitch > 89.0f) c->pitch = 89.0f;
        if (c->pitch < -89.0f) c->pitch = -89.0f;
    });

    VkInstance instance;
    {
        uint32_t extCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> exts(glfwExts, glfwExts + extCount);
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        bool haveValid = false;
        for (auto& l : layers)
            if (!strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) haveValid = true;
        const char* wantLayers[] = {"VK_LAYER_KHRONOS_validation"};
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        if (haveValid) {
            ci.enabledLayerCount = 1;
            ci.ppEnabledLayerNames = wantLayers;
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            printf("WARN: no validation layers, running blind\n");
        }
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
    }
    DeletionQueue del;
    del.push([&]() { vkDestroyInstance(instance, nullptr); });

    // Debug messenger: без него слои молчат. Печатаем всё ≥ warning.
    VkDebugUtilsMessengerEXT dbgMessenger = VK_NULL_HANDLE;
    {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (create && destroy) {
            VkDebugUtilsMessengerCreateInfoEXT ci{};
            ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            ci.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
                                    VkDebugUtilsMessageTypeFlagsEXT,
                                    const VkDebugUtilsMessengerCallbackDataEXT* d,
                                    void*) -> VkBool32 {
                printf("VALIDATION: %s\n", d->pMessage);
                return VK_FALSE;
            };
            if (create(instance, &ci, nullptr, &dbgMessenger) == VK_SUCCESS)
                printf("validation messenger ON\n");
            del.push([=]() {
                destroy(instance, dbgMessenger, nullptr);
            });
        }
    }

    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    uint32_t gfxFamily = ~0u;
    float maxAniso = 1.0f;
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance, &n, nullptr);
        std::vector<VkPhysicalDevice> gpus(n);
        vkEnumeratePhysicalDevices(instance, &n, gpus.data());
        for (auto g : gpus) {
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qs(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, qs.data());
            for (uint32_t i = 0; i < qn; i++) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(g, i, surface, &present);
                if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    VkPhysicalDeviceProperties p;
                    vkGetPhysicalDeviceProperties(g, &p);
                    printf("GPU: %s\n", p.deviceName);
                    maxAniso = p.limits.maxSamplerAnisotropy;
                    gpu = g; gfxFamily = i;
                    break;
                }
            }
            if (gpu) break;
        }
        if (!gpu) { printf("no suitable GPU\n"); return 1; }
    }

    VkDevice device;
    VkQueue gfxQueue;
    {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = gfxFamily;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        VkPhysicalDeviceDynamicRenderingFeatures dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dyn.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceFeatures feats{};
        feats.samplerAnisotropy = VK_TRUE;
        const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.pNext = &dyn;
        ci.pEnabledFeatures = &feats;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &qi;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = devExts;
        VK_CHECK(vkCreateDevice(gpu, &ci, nullptr, &device));
        vkGetDeviceQueue(device, gfxFamily, 0, &gfxQueue);
    }
    del.push([&]() { vkDestroyDevice(device, nullptr); });

    VmaAllocator alloc;
    {
        VmaAllocatorCreateInfo ai{};
        ai.physicalDevice = gpu;
        ai.device = device;
        ai.instance = instance;
        ai.vulkanApiVersion = VK_API_VERSION_1_3;
        VK_CHECK(vmaCreateAllocator(&ai, &alloc));
    }
    del.push([&]() { vmaDestroyAllocator(alloc); });

    VkSwapchainKHR swapchain;
    VkFormat swapFormat;
    VkExtent2D swapExtent{1280, 720};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, fmts.data());
        VkSurfaceFormatKHR picked = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { picked = f; break; }
        swapFormat = picked.format;
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
        swapExtent = caps.currentExtent;
        if (swapExtent.width == 0xFFFFFFFF) swapExtent = {1280, 720};
        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface;
        ci.minImageCount = caps.minImageCount + 1;
        ci.imageFormat = swapFormat;
        ci.imageColorSpace = picked.colorSpace;
        ci.imageExtent = swapExtent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        ci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain));
        uint32_t in = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &in, nullptr);
        swapImages.resize(in);
        vkGetSwapchainImagesKHR(device, swapchain, &in, swapImages.data());
        for (auto img : swapImages) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = swapFormat;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView v;
            VK_CHECK(vkCreateImageView(device, &vi, nullptr, &v));
            swapViews.push_back(v);
        }
    }
    del.push([&]() {
        for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
    });

    // Разовые команды (загрузки, переходы): свой пул + fence.
    VkCommandPool immPool;
    VkCommandBuffer immBuf;
    VkFence immFence;
    {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                   VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // reset нужен immRun
        ci.queueFamilyIndex = gfxFamily;
        VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &immPool));
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = immPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, &immBuf));
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(device, &fi, nullptr, &immFence));
    }
    del.push([&]() {
        vkDestroyFence(device, immFence, nullptr);
        vkDestroyCommandPool(device, immPool, nullptr);
    });
    auto immRun = [&](std::function<void(VkCommandBuffer)> fn) {
        VK_CHECK(vkResetFences(device, 1, &immFence));
        VK_CHECK(vkResetCommandBuffer(immBuf, 0));
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(immBuf, &bi));
        fn(immBuf);
        VK_CHECK(vkEndCommandBuffer(immBuf));
        VkSubmitInfo sub{};
        sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &immBuf;
        VK_CHECK(vkQueueSubmit(gfxQueue, 1, &sub, immFence));
        VK_CHECK(vkWaitForFences(device, 1, &immFence, VK_TRUE, 1000000000ull));
    };
    // ПЕРЕХОД картинки: явный барьер (урок: layout — состояние, менять только так).
    auto imgBarrier = [](VkCommandBuffer cb, VkImage img, VkImageLayout oldL, VkImageLayout newL,
                         VkImageAspectFlags aspect, uint32_t baseMip, uint32_t mipCount,
                         VkPipelineStageFlags srcS, VkAccessFlags srcA,
                         VkPipelineStageFlags dstS, VkAccessFlags dstA) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        b.oldLayout = oldL; b.newLayout = newL;
        b.image = img;
        b.subresourceRange = {aspect, baseMip, mipCount, 0, VK_REMAINING_ARRAY_LAYERS};
        vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // ---- depth (D32F, живёт в ATTACHMENT весь кадр, чистим loadOp) ----
    VkImage depthImg;
    VmaAllocation depthAlloc;
    VkImageView depthView;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_D32_SFLOAT;
        ci.extent = {swapExtent.width, swapExtent.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(alloc, &ci, &ai, &depthImg, &depthAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depthImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vi, nullptr, &depthView));
        immRun([&](VkCommandBuffer cb) {
            imgBarrier(cb, depthImg, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        });
    }
    del.push([&]() {
        vkDestroyImageView(device, depthView, nullptr);
        vmaDestroyImage(alloc, depthImg, depthAlloc);
    });

    // ---- texture array 13x16x16 + 5 мипов блитами ----
    VkImage tileImg;
    VmaAllocation tileAlloc;
    VkImageView tileView;
    VkSampler tileSmp;
    {
        const char* names[13] = {"grass_top.png", "grass_side.png", "dirt.png", "stone.png",
            "water.png", "lava.png", "leaves.png", "log_side.png", "log_top.png",
            "ore_coal.png", "ore_iron.png", "ore_gold.png", "ore_diamond.png"};
        const int T = 16, NL = 13, MIPS = 5;
        std::vector<unsigned char> all(T * T * 4 * NL);
        stbi_set_flip_vertically_on_load(true);
        for (int i = 0; i < NL; i++) {
            char path[1024];
            snprintf(path, sizeof(path), "assets/tiles/%s", names[i]);
            int w, h, ch;
            unsigned char* d = stbi_load(path, &w, &h, &ch, 4);
            if (!d || w != T || h != T) { printf("tile bad %s\n", path); exit(1); }
            memcpy(&all[i * T * T * 4], d, T * T * 4);
            stbi_image_free(d);
        }
        // Ваниль уже цветная (grass_top/листва зелёные) — тинты НЕ печём.
        VkDeviceSize upSize = all.size();
        VkBuffer staging;
        VmaAllocation stagingAlloc;
        {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = upSize;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
            void* dst = nullptr;
            VK_CHECK(vmaMapMemory(alloc, stagingAlloc, &dst));
            memcpy(dst, all.data(), all.size());
            vmaUnmapMemory(alloc, stagingAlloc);
        }
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {(uint32_t)T, (uint32_t)T, 1};
        ci.mipLevels = MIPS; ci.arrayLayers = NL;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(alloc, &ci, &ai, &tileImg, &tileAlloc, nullptr));
        immRun([&](VkCommandBuffer cb) {
            imgBarrier(cb, tileImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, MIPS,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy cp{};
            cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, NL};
            cp.imageExtent = {(uint32_t)T, (uint32_t)T, 1};
            vkCmdCopyBufferToImage(cb, staging, tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
            // мипы блитами 16->8->4->2->1 (каждый уровень: DST->SRC, blit, SRC->SHADER)
            for (int m = 1; m < MIPS; m++) {
                imgBarrier(cb, tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                VkImageBlit bl{};
                bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(m - 1), 0, NL};
                bl.srcOffsets[1] = {T >> (m - 1), T >> (m - 1), 1};
                bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)m, 0, NL};
                bl.dstOffsets[1] = {T >> m, T >> m, 1};
                vkCmdBlitImage(cb, tileImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
                imgBarrier(cb, tileImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            }
            imgBarrier(cb, tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, MIPS - 1, 1,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        });
        vmaDestroyBuffer(alloc, staging, stagingAlloc);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = tileImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, MIPS, 0, NL};
        VK_CHECK(vkCreateImageView(device, &vi, nullptr, &tileView));
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = maxAniso < 8.0f ? maxAniso : 8.0f;
        si.maxLod = (float)(MIPS - 1);
        VK_CHECK(vkCreateSampler(device, &si, nullptr, &tileSmp));
    }
    del.push([&]() {
        vkDestroySampler(device, tileSmp, nullptr);
        vkDestroyImageView(device, tileView, nullptr);
        vmaDestroyImage(alloc, tileImg, tileAlloc);
    });

    // ---- вершины чанков (поквадратный мешер, глобальные координаты) ----
    struct ChunkVB { VkBuffer buf; VmaAllocation alloc; int verts; };
    std::vector<ChunkVB> chunkVBs;
    glm::vec3 modelOff = worldOffset;
    {
        for (int cz = 0; cz < 8; cz++)
            for (int cx = 0; cx < 8; cx++) {
                std::vector<float> data = buildChunkVK(world, cx, cz);
                ChunkVB c{nullptr, nullptr, (int)(data.size() / 10)};
                if (data.empty()) { chunkVBs.push_back(c); continue; }
                VkDeviceSize sz = data.size() * sizeof(float);
                VkBuffer staging;
                VmaAllocation stagingAlloc;
                VkBufferCreateInfo bi{};
                bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bi.size = sz;
                bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                VmaAllocationCreateInfo ai{};
                ai.usage = VMA_MEMORY_USAGE_AUTO;
                ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
                void* dst = nullptr;
                VK_CHECK(vmaMapMemory(alloc, stagingAlloc, &dst));
                memcpy(dst, data.data(), sz);
                vmaUnmapMemory(alloc, stagingAlloc);
                bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                ai.flags = 0;
                VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &c.buf, &c.alloc, nullptr));
                immRun([&](VkCommandBuffer cb) {
                    VkBufferCopy cp{};
                    cp.size = sz;
                    vkCmdCopyBuffer(cb, staging, c.buf, 1, &cp);
                });
                vmaDestroyBuffer(alloc, staging, stagingAlloc);
                // барьер не нужен: fence ждёт очередь целиком перед рисованием
                chunkVBs.push_back(c);
            }
        size_t tv = 0;
        for (auto& c : chunkVBs) tv += c.verts;
        printf("chunks verts total %zu\n", tv);
    }
    del.push([&]() {
        for (auto& c : chunkVBs)
            if (c.buf) vmaDestroyBuffer(alloc, c.buf, c.alloc);
    });

    // ---- UBO кадра x2 + дескрипторы (набор на кадр x чанк: SSBO свой у каждого) ----
    VkDescriptorSetLayout setLayout;
    VkDescriptorPool descPool;
    VkDescriptorSet descSets[2][64];
    VkBuffer uboBuf[2];
    VmaAllocation uboAlloc[2];
    VkBuffer dummySSBO;
    VmaAllocation dummyAlloc;
    {
        VkBufferCreateInfo dbi0{};
        dbi0.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dbi0.size = 16;
        dbi0.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo daci{};
        daci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateBuffer(alloc, &dbi0, &daci, &dummySSBO, &dummyAlloc, nullptr));
        VkDescriptorSetLayoutBinding b0{};
        b0.binding = 0;
        b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b0.descriptorCount = 1;
        b0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b1{};
        b1.binding = 1;
        b1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b1.descriptorCount = 1;
        b1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b2{}; // demo-3a: вершины тянутся из SSBO
        b2.binding = 2;
        b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b2.descriptorCount = 1;
        b2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding bs[3] = {b0, b1, b2};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bs;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout));
        VkDescriptorPoolSize ps[3]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[0].descriptorCount = 128;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 128;
        ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps[2].descriptorCount = 128;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 128;
        pi.poolSizeCount = 3; pi.pPoolSizes = ps;
        VK_CHECK(vkCreateDescriptorPool(device, &pi, nullptr, &descPool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout;
        for (int i = 0; i < 2; i++) {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = sizeof(FrameUBO);
            bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            VmaAllocationCreateInfo aci{};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(alloc, &bi, &aci, &uboBuf[i], &uboAlloc[i], nullptr));
            for (int c = 0; c < 64; c++) {
                VK_CHECK(vkAllocateDescriptorSets(device, &ai, &descSets[i][c]));
                VkDescriptorBufferInfo dbi{};
                dbi.buffer = uboBuf[i]; dbi.range = sizeof(FrameUBO);
                VkDescriptorImageInfo dii{};
                dii.sampler = tileSmp; dii.imageView = tileView;
                dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkBuffer ssbo = chunkVBs[c].buf ? chunkVBs[c].buf : dummySSBO;
                VkDescriptorBufferInfo sbi{};
                sbi.buffer = ssbo; sbi.range = VK_WHOLE_SIZE;
                VkWriteDescriptorSet w[3]{};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[0].dstSet = descSets[i][c]; w[0].dstBinding = 0;
                w[0].descriptorCount = 1;
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w[0].pBufferInfo = &dbi;
                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[1].dstSet = descSets[i][c]; w[1].dstBinding = 1;
                w[1].descriptorCount = 1;
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[1].pImageInfo = &dii;
                w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[2].dstSet = descSets[i][c]; w[2].dstBinding = 2;
                w[2].descriptorCount = 1;
                w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[2].pBufferInfo = &sbi;
                vkUpdateDescriptorSets(device, 3, w, 0, nullptr);
            }
        }
    }
    del.push([&]() {
        for (int i = 0; i < 2; i++) vmaDestroyBuffer(alloc, uboBuf[i], uboAlloc[i]);
        vmaDestroyBuffer(alloc, dummySSBO, dummyAlloc);
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    });

    // ---- пайплайн террейна (vertex-input 12 floats, depth, cull NONE на demo-2) ----
    VkPipelineLayout pipeLayout;
    VkPipeline pipeline;
    {
        VkShaderModule vs = makeShader(device, SHADER_DIR "vk_terrain.vert.spv");
        VkShaderModule fs = makeShader(device, SHADER_DIR "vk_terrain.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
        // demo-3a pulling (Ch05): vertex-input ПУСТОЙ, вершины тянет шейдер
        // из SSBO (binding 2) по gl_VertexIndex. Индекс-буфер — следующим шагом.
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE; // demo-2: winding проверим глазами, каллинг позже
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pc.size = sizeof(glm::mat4); pc.offset = 0;
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = 1; li.pSetLayouts = &setLayout;
        li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(device, &li, nullptr, &pipeLayout));
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &swapFormat;
        ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.pNext = &ri;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDepthStencilState = &ds;
        pi.pDynamicState = &dyn;
        pi.layout = pipeLayout;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline));
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }
    del.push([&]() {
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipeLayout, nullptr);
    });

    // ---- камера: старт у холма, WASD+мышь+стрелки, Space/C, ESC выход ----
    glm::vec3 camPos, camFront;
    {
        int hx = 64, hz = 64, top = 20;
        for (int z = 20; z < 108; z++)
            for (int x = 20; x < 108; x++) {
                int t = -1;
                for (int y = 63; y >= 0; y--)
                    if (World::isSolid(world.getBlock(x, y, z))) { t = y; break; }
                if (t > top) { top = t; hx = x; hz = z; }
            }
        glm::vec3 target = worldOffset + glm::vec3(hx + 0.5f, top, hz + 0.5f);
        camPos = target + glm::vec3(20.0f, 12.0f, 28.0f);
        camFront = glm::normalize(target - camPos);
        ctl.yaw = glm::degrees(atan2(camFront.z, camFront.x));
        ctl.pitch = glm::degrees(asin(camFront.y));
        printf("hill (%d,%d,%d)\n", hx, top, hz);
    }
    auto updFront = [&]() {
        camFront.x = cos(glm::radians(ctl.yaw)) * cos(glm::radians(ctl.pitch));
        camFront.y = sin(glm::radians(ctl.pitch));
        camFront.z = sin(glm::radians(ctl.yaw)) * cos(glm::radians(ctl.pitch));
        camFront = glm::normalize(camFront);
    };

    // Синхра: acquire-семафор по кадру, render-семафор + layout по картинке,
    // fence кадра ждётся ПЕРЕД acquire (сабмит позапрошлого кадра выполнен —
    // cmdbuf свободен, acquire-семафор потреблён). acquire ПЕРЕД fence картинки
    // не нужен: acquire сам ждёт present. Так велят слои (3 бага найдено ими).
    const int FRAMES = 2;
    const int NIMGS = (int)swapImages.size();
    if (NIMGS > 8) { printf("too many swap images %d\n", NIMGS); return 1; }
    VkSemaphore acquireSem[8], renderSem[8];
    VkFence frameFence[2];
    VkImageLayout imgLayout[8];
    for (int i = 0; i < NIMGS; i++) {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &acquireSem[i]));
        VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &renderSem[i]));
        imgLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
        int j = i;
        del.push([=, &device]() {
            vkDestroySemaphore(device, acquireSem[j], nullptr);
            vkDestroySemaphore(device, renderSem[j], nullptr);
        });
    }
    {
        // Кадровые заборы отдельно: их ровно FRAMES, не путать с картинками.
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < FRAMES; i++)
            VK_CHECK(vkCreateFence(device, &fi, nullptr, &frameFence[i]));
        del.push([&]() {
            for (int i = 0; i < FRAMES; i++) vkDestroyFence(device, frameFence[i], nullptr);
        });
    }
    // ---- кадровые комманд-буферы (2 в полёте, синхра — по картинкам выше) ----
    VkCommandPool cmdPool;
    VK_CHECK([&]() {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = gfxFamily;
        return vkCreateCommandPool(device, &ci, nullptr, &cmdPool);
    }());
    del.push([&]() { vkDestroyCommandPool(device, cmdPool, nullptr); });
    VkCommandBuffer cmdBufs[FRAMES];
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = FRAMES;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, cmdBufs));
    }
    glm::mat4 proj = glm::perspective(glm::radians(70.0f),
        (float)swapExtent.width / (float)swapExtent.height, 0.1f, 600.0f);
    proj[1][1] *= -1.0f; // Y-flip под Vulkan (идиома vkguide)
    float tod = 1.5707f; // полдень
    double prevT = glfwGetTime();
    int frame = 0, drawn = 0;
    double fpsT = prevT;
    int fpsN = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)(now - prevT);
        prevT = now;
        if (dt > 0.05f) dt = 0.05f;
        // ввод
        {
            float sp = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) ? 30.0f : 12.0f) * dt;
            glm::vec3 right = glm::normalize(glm::cross(camFront, glm::vec3(0, 1, 0)));
            if (glfwGetKey(window, GLFW_KEY_W)) camPos += camFront * sp;
            if (glfwGetKey(window, GLFW_KEY_S)) camPos -= camFront * sp;
            if (glfwGetKey(window, GLFW_KEY_A)) camPos -= right * sp;
            if (glfwGetKey(window, GLFW_KEY_D)) camPos += right * sp;
            if (glfwGetKey(window, GLFW_KEY_SPACE)) camPos.y += sp;
            if (glfwGetKey(window, GLFW_KEY_C)) camPos.y -= sp;
            float rs = 60.0f * dt;
            if (glfwGetKey(window, GLFW_KEY_LEFT)) ctl.yaw -= rs;
            if (glfwGetKey(window, GLFW_KEY_RIGHT)) ctl.yaw += rs;
            if (glfwGetKey(window, GLFW_KEY_UP)) ctl.pitch += rs;
            if (glfwGetKey(window, GLFW_KEY_DOWN)) ctl.pitch -= rs;
            if (ctl.pitch > 89.0f) ctl.pitch = 89.0f;
            if (ctl.pitch < -89.0f) ctl.pitch = -89.0f;
            updFront();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(window, 1);
        }
        int fi = frame % FRAMES;
        uint32_t imgIdx = 0;
        // Кадровый fence ПЕРЕД acquire: сабмит двухкадровой давности точно
        // выполнен → cmdbuf свободен, acquire-семафор потреблён. Без этого
        // слои орут про pending semaphore/commandbuffer (проверено).
        VK_CHECK(vkWaitForFences(device, 1, &frameFence[fi], VK_TRUE, 1000000000ull));
        VK_CHECK(vkResetFences(device, 1, &frameFence[fi]));
        // acquire ПЕРЕД записью: картинка вернётся только после своего present,
        // layout трекаем сами. Кадровый fence выше уже гарантирует свободный cmdbuf.
        VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000ull,
                                       acquireSem[fi], VK_NULL_HANDLE, &imgIdx));
        // UBO кадра
        {
            FrameUBO u{};
            u.viewProj = proj * glm::lookAt(camPos, camPos + camFront, glm::vec3(0, 1, 0));
            glm::vec3 sun = glm::normalize(glm::vec3(cos(tod), sin(tod), 0.35f));
            u.sunDir = glm::vec4(sun, 0.0f);
            u.sunCol = glm::vec4(1.25f, 1.21f, 1.12f, 0.0f);
            u.ambSky = glm::vec4(0.54f, 0.60f, 0.69f, 0.0f);
            u.ambGnd = glm::vec4(0.27f, 0.24f, 0.21f, 0.0f);
            u.fog = glm::vec4(0.55f, 0.65f, 0.80f, 260.0f);
            u.misc = glm::vec4(40.0f, 1.1f, 1.2f, (float)now);
            u.viewPos = glm::vec4(camPos, 0.0f);
            void* dst = nullptr;
            VK_CHECK(vmaMapMemory(alloc, uboAlloc[fi], &dst));
            memcpy(dst, &u, sizeof(u));
            vmaUnmapMemory(alloc, uboAlloc[fi]);
        }
        VK_CHECK(vkResetCommandBuffer(cmdBufs[fi], 0));
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmdBufs[fi], &bi));
        VkImageMemoryBarrier toDraw{};
        toDraw.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDraw.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toDraw.oldLayout = imgLayout[imgIdx]; // трекаем: первый раз UNDEFINED
        toDraw.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toDraw.image = swapImages[imgIdx];
        toDraw.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDraw);
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = swapViews[imgIdx];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.45f, 0.62f, 0.85f, 1.0f}};
        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = depthView;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea = {{0, 0}, swapExtent};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        ri.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmdBufs[fi], &ri);
        vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport vwp{0, 0, (float)swapExtent.width, (float)swapExtent.height, 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, swapExtent};
        vkCmdSetViewport(cmdBufs[fi], 0, 1, &vwp);
        vkCmdSetScissor(cmdBufs[fi], 0, 1, &sc);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), modelOff);
        vkCmdPushConstants(cmdBufs[fi], pipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(model), &model);
        for (int c = 0; c < 64; c++) {
            if (!chunkVBs[c].buf) continue;
            vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeLayout, 0, 1, &descSets[fi][c], 0, nullptr);
            vkCmdDraw(cmdBufs[fi], (uint32_t)chunkVBs[c].verts, 1, 0, 0);
        }
        vkCmdEndRendering(cmdBufs[fi]);
        VkImageMemoryBarrier toPresent = toDraw;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toPresent);
        VK_CHECK(vkEndCommandBuffer(cmdBufs[fi]));
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo sub{};
        sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &acquireSem[fi];
        sub.pWaitDstStageMask = &waitStage;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &cmdBufs[fi];
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &renderSem[imgIdx];
        VK_CHECK(vkQueueSubmit(gfxQueue, 1, &sub, frameFence[fi]));
        imgLayout[imgIdx] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkPresentInfoKHR pr{};
        pr.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pr.waitSemaphoreCount = 1;
        pr.pWaitSemaphores = &renderSem[imgIdx];
        pr.swapchainCount = 1;
        pr.pSwapchains = &swapchain;
        pr.pImageIndices = &imgIdx;
        VK_CHECK(vkQueuePresentKHR(gfxQueue, &pr));
        frame++; drawn++; fpsN++;
        if (now - fpsT >= 2.0) {
            printf("fps %.0f (%.2f ms)\n", fpsN / (now - fpsT), (now - fpsT) * 1000.0 / fpsN);
            fpsT = now; fpsN = 0;
        }
        if (maxFrames > 0 && drawn >= maxFrames) break;
    }
    printf("demo-2 OK: %d frames\n", drawn);
    vkDeviceWaitIdle(device);
    del.flush();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
