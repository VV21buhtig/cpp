// vulcan-render demo-2: воксели. Мир/мешер/логика — из GL-прототипа как есть,
// рендер — Vulkan 1.3: VMA, depth, UBO+дескрипторы, texture array с мипами,
// поквадратный мешер (без greedy). Управление: WASD+стрелки, Space/C вверх/вниз.
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>
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
    glm::mat4 invViewProj;
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
    GLFWwindow* window = glfwCreateWindow(1280, 720, "vulcan-render demo-4 shadows", nullptr, nullptr);
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
        VkPhysicalDeviceVulkan12Features feat12{};
        feat12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        feat12.drawIndirectCount = VK_TRUE; // demo-3c: vkCmdDrawIndirectCount
        feat12.pNext = &dyn;
        VkPhysicalDeviceFeatures feats{};
        feats.samplerAnisotropy = VK_TRUE;
        const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.pNext = &feat12;
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

    // ---- demo-5a HDR-цель (R16F, сцена+небо пишут, читают lum/tonemap) ----
    VkImage hdrImg = nullptr;
    VmaAllocation hdrAlloc = nullptr;
    VkImageView hdrView = nullptr;
    VkImageLayout hdrLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.extent = {swapExtent.width, swapExtent.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(alloc, &ci, &ai, &hdrImg, &hdrAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = hdrImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vi, nullptr, &hdrView));
    }
    del.push([&]() {
        vkDestroyImageView(device, hdrView, nullptr);
        vmaDestroyImage(alloc, hdrImg, hdrAlloc);
    });

    // ---- demo-5a lum 64x36 + exposure ping-pong 1x1 (GENERAL навсегда) ----
    VkImage lumImg = nullptr, expImg[2] = {nullptr, nullptr};
    VmaAllocation lumAlloc = nullptr, expAlloc[2] = {nullptr, nullptr};
    VkImageView lumView = nullptr, expView[2] = {nullptr, nullptr};
    VkSampler hdrSmp = nullptr, expSmp = nullptr;
    {
        auto mkTarget = [&](uint32_t w, uint32_t h, VkImage& img, VmaAllocation& al,
                            VkImageView& view, bool clear1) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R16_SFLOAT;
            ci.extent = {w, h, 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(alloc, &ci, &ai, &img, &al, nullptr));
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16_SFLOAT;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device, &vi, nullptr, &view));
            immRun([&](VkCommandBuffer cb) {
                imgBarrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                           0, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                if (clear1) {
                    VkClearColorValue cv{};
                    cv.float32[0] = 1.0f; // exposure стартует с 1.0 (рецепт книги)
                    VkImageSubresourceRange rg{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &rg);
                }
                imgBarrier(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            });
        };
        mkTarget(64, 36, lumImg, lumAlloc, lumView, false);
        mkTarget(1, 1, expImg[0], expAlloc[0], expView[0], true);
        mkTarget(1, 1, expImg[1], expAlloc[1], expView[1], true);
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(device, &si, nullptr, &hdrSmp));
        VK_CHECK(vkCreateSampler(device, &si, nullptr, &expSmp));
    }
    del.push([&]() {
        vkDestroySampler(device, hdrSmp, nullptr);
        vkDestroySampler(device, expSmp, nullptr);
        vkDestroyImageView(device, lumView, nullptr);
        vmaDestroyImage(alloc, lumImg, lumAlloc);
        for (int i = 0; i < 2; i++) {
            vkDestroyImageView(device, expView[i], nullptr);
            vmaDestroyImage(alloc, expImg[i], expAlloc[i]);
        }
    });

    // ---- demo-4 теневая карта: D16 2048 (рецепт Ch10/11) + compare-сэмплер ----
    const int SHADOW_S = 2048;
    VkImage shadowImg = nullptr;
    VmaAllocation shadowAlloc = nullptr;
    VkImageView shadowView = nullptr;
    VkSampler shadowSmp = nullptr;
    VkSampler shadowRawSmp = nullptr; // рентген без compare (F1)
    VkImageLayout shadowLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_D16_UNORM;
        ci.extent = {(uint32_t)SHADOW_S, (uint32_t)SHADOW_S, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(alloc, &ci, &ai, &shadowImg, &shadowAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = shadowImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D16_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vi, nullptr, &shadowView));
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR; // железный 2x2 PCF на тап (рецепт книги)
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.compareEnable = VK_TRUE; // sampler2DShadow: сравнение LESS_OR_EQUAL
        si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        si.minLod = 0.0f; si.maxLod = 0.0f;
        VK_CHECK(vkCreateSampler(device, &si, nullptr, &shadowSmp));
        si.compareEnable = VK_FALSE; // сырая глубина для рентгена
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        VK_CHECK(vkCreateSampler(device, &si, nullptr, &shadowRawSmp));
    }
    del.push([&]() {
        vkDestroySampler(device, shadowRawSmp, nullptr);
        vkDestroySampler(device, shadowSmp, nullptr);
        vkDestroyImageView(device, shadowView, nullptr);
        vmaDestroyImage(alloc, shadowImg, shadowAlloc);
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
        // Ванильные grass_top/листва/вода — ч/б + биомный тинт (как в MC).
        // Печём plains-тинты сразу: трава #91BD59, листва #77AB2F, вода #3F76E4.
        for (int p = 0; p < T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 145 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 189 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 89 / 255);
        }
        for (int p = 6 * T * T; p < 7 * T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 119 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 171 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 47 / 255);
        }
        for (int p = 4 * T * T; p < 5 * T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 63 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 118 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 228 / 255);
        }
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

    // ---- gigabuffer квадов (всё в одном SSBO) + meta чанков ----
    // demo-3c: compute-cull читает meta, пишет vis + indirect; VS тянет квады
    // по firstInstance+instance, чанк — по gl_DrawID из vis[].
    struct ChunkMeta { uint32_t quadOff, quadCount; float ox, oz; };
    VkBuffer gigaBuf = nullptr;
    VmaAllocation gigaAlloc = nullptr;
    VkBuffer metaBuf = nullptr;
    VmaAllocation metaAlloc = nullptr;
    VkBuffer visBuf = nullptr;
    VmaAllocation visAlloc = nullptr;
    VkBuffer indBuf = nullptr; // uint count + 64 x VkDrawIndirectCommand
    VmaAllocation indAlloc = nullptr;
    size_t totalQuads = 0;
    {
        std::vector<uint32_t> all;
        std::vector<ChunkMeta> metas;
        for (int cz = 0; cz < 8; cz++)
            for (int cx = 0; cx < 8; cx++) {
                std::vector<uint32_t> data = buildChunkVK(world, cx, cz);
                if (data.size() >= (1u << 20)) { printf("chunk too big for QUADBIAS\n"); exit(1); }
                ChunkMeta m{(uint32_t)all.size(), (uint32_t)data.size(),
                            worldOffset.x + cx * 16.0f, worldOffset.z + cz * 16.0f};
                metas.push_back(m);
                all.insert(all.end(), data.begin(), data.end());
            }
        totalQuads = all.size();
        printf("gigaquads total %zu\n", totalQuads);
        auto upload = [&](const void* src, VkDeviceSize sz, VkBufferUsageFlags use,
                          VkBuffer& out, VmaAllocation& oa) {
            VkBuffer staging;
            VmaAllocation stagingAlloc;
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = sz ? sz : 16;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
            if (sz) {
                void* dst = nullptr;
                VK_CHECK(vmaMapMemory(alloc, stagingAlloc, &dst));
                memcpy(dst, src, sz);
                vmaUnmapMemory(alloc, stagingAlloc);
            }
            bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | use;
            ai.flags = 0;
            VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &out, &oa, nullptr));
            immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = sz ? sz : 16;
                vkCmdCopyBuffer(cb, staging, out, 1, &cp);
            });
            vmaDestroyBuffer(alloc, staging, stagingAlloc);
        };
        upload(all.data(), all.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gigaBuf, gigaAlloc);
        upload(metas.data(), metas.size() * sizeof(ChunkMeta),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, metaBuf, metaAlloc);
        std::vector<uint32_t> zero(64, 0);
        upload(zero.data(), zero.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, visBuf, visAlloc);
        std::vector<uint8_t> izero(16 + 64 * sizeof(VkDrawIndirectCommand), 0);
        upload(izero.data(), izero.size(),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
               indBuf, indAlloc);
    }
    del.push([&]() {
        vmaDestroyBuffer(alloc, gigaBuf, gigaAlloc);
        vmaDestroyBuffer(alloc, metaBuf, metaAlloc);
        vmaDestroyBuffer(alloc, visBuf, visAlloc);
        vmaDestroyBuffer(alloc, indBuf, indAlloc);
    });

    // ---- demo-5w вода: свой gigabuffer/meta/indirect (путь параллельный opaque) ----
    struct WaterMeta { uint32_t quadOff, quadCount; float ox, oz; };
    VkBuffer waterGigaBuf = nullptr, waterMetaBuf = nullptr, waterIndBuf = nullptr;
    VmaAllocation waterGigaAlloc = nullptr, waterMetaAlloc = nullptr, waterIndAlloc = nullptr;
    {
        std::vector<uint32_t> all;
        std::vector<WaterMeta> metas;
        for (int cz = 0; cz < 8; cz++)
            for (int cx = 0; cx < 8; cx++) {
                std::vector<uint32_t> data = buildWaterVK(world, cx, cz);
                if (data.size() >= (1u << 20)) { printf("water chunk too big\n"); exit(1); }
                WaterMeta m{(uint32_t)all.size(), (uint32_t)data.size(),
                            worldOffset.x + cx * 16.0f, worldOffset.z + cz * 16.0f};
                metas.push_back(m);
                all.insert(all.end(), data.begin(), data.end());
            }
        printf("water quads total %zu\n", all.size());
        auto upload = [&](const void* src, VkDeviceSize sz, VkBufferUsageFlags use,
                          VkBuffer& out, VmaAllocation& oa) {
            VkBuffer staging;
            VmaAllocation stagingAlloc;
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = sz ? sz : 16;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
            if (sz) {
                void* dst = nullptr;
                VK_CHECK(vmaMapMemory(alloc, stagingAlloc, &dst));
                memcpy(dst, src, sz);
                vmaUnmapMemory(alloc, stagingAlloc);
            }
            bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | use;
            ai.flags = 0;
            VK_CHECK(vmaCreateBuffer(alloc, &bi, &ai, &out, &oa, nullptr));
            immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = sz ? sz : 16;
                vkCmdCopyBuffer(cb, staging, out, 1, &cp);
            });
            vmaDestroyBuffer(alloc, staging, stagingAlloc);
        };
        upload(all.data(), all.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, waterGigaBuf, waterGigaAlloc);
        upload(metas.data(), metas.size() * sizeof(WaterMeta),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, waterMetaBuf, waterMetaAlloc);
        std::vector<uint8_t> izero(16 + 64 * sizeof(VkDrawIndirectCommand), 0);
        upload(izero.data(), izero.size(),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
               waterIndBuf, waterIndAlloc);
    }
    del.push([&]() {
        vmaDestroyBuffer(alloc, waterGigaBuf, waterGigaAlloc);
        vmaDestroyBuffer(alloc, waterMetaBuf, waterMetaAlloc);
        vmaDestroyBuffer(alloc, waterIndBuf, waterIndAlloc);
    });

    // ---- UBO кадра x2 + дескрипторы (1 набор на кадр: UBO свой, остальное общее) ----
    VkDescriptorSetLayout setLayout;
    VkDescriptorPool descPool;
    VkDescriptorSet descSets[2];
    VkBuffer uboBuf[2];
    VmaAllocation uboAlloc[2];
    {
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
        VkDescriptorSetLayoutBinding b2{}; // 2=gigabuffer квадов
        b2.binding = 2;
        b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b2.descriptorCount = 1;
        b2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b3{}; // 3=meta чанков (origin/offset)
        b3.binding = 3;
        b3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b3.descriptorCount = 1;
        b3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b4{}; // 4=vis-список (compute пишет, VS читает)
        b4.binding = 4;
        b4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b4.descriptorCount = 1;
        b4.stageFlags = (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                              VK_SHADER_STAGE_COMPUTE_BIT);
        VkDescriptorSetLayoutBinding b5{}; // 5=теневая карта (compare-сэмплер)
        b5.binding = 5;
        b5.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b5.descriptorCount = 1;
        b5.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b6{}; // 6=тень сырьём для рентгена (F1)
        b6.binding = 6;
        b6.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b6.descriptorCount = 1;
        b6.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b7{}; // 7=вода gigabuffer (demo-5w)
        b7.binding = 7;
        b7.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b7.descriptorCount = 1;
        b7.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b8{}; // 8=вода meta (demo-5w)
        b8.binding = 8;
        b8.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b8.descriptorCount = 1;
        b8.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding bs[9] = {b0, b1, b2, b3, b4, b5, b6, b7, b8};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 9; li.pBindings = bs;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout));
        VkDescriptorPoolSize ps[3]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[0].descriptorCount = 2;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 2 * 3;
        ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps[2].descriptorCount = 2 * 5;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 2;
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
            VK_CHECK(vkAllocateDescriptorSets(device, &ai, &descSets[i]));
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = uboBuf[i]; dbi.range = sizeof(FrameUBO);
            VkDescriptorImageInfo dii{};
            dii.sampler = tileSmp; dii.imageView = tileView;
            dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorBufferInfo sbi[3]{};
            sbi[0].buffer = gigaBuf; sbi[0].range = VK_WHOLE_SIZE;
            sbi[1].buffer = metaBuf; sbi[1].range = VK_WHOLE_SIZE;
            sbi[2].buffer = visBuf; sbi[2].range = VK_WHOLE_SIZE;
            VkDescriptorBufferInfo wbi[2]{};
            wbi[0].buffer = waterGigaBuf; wbi[0].range = VK_WHOLE_SIZE;
            wbi[1].buffer = waterMetaBuf; wbi[1].range = VK_WHOLE_SIZE;
            VkDescriptorImageInfo shdi{};
            shdi.sampler = shadowSmp; shdi.imageView = shadowView;
            shdi.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet w[7]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = descSets[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[0].pBufferInfo = &dbi;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = descSets[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].pImageInfo = &dii;
            for (int b = 2; b < 5; b++) {
                w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[b].dstSet = descSets[i]; w[b].dstBinding = (uint32_t)b;
                w[b].descriptorCount = 1;
                w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[b].pBufferInfo = &sbi[b - 2];
            }
            w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[5].dstSet = descSets[i]; w[5].dstBinding = 5;
            w[5].descriptorCount = 1;
            w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[5].pImageInfo = &shdi;
            VkDescriptorImageInfo shraw{};
            shraw.sampler = shadowRawSmp; shraw.imageView = shadowView;
            shraw.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            w[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[6].dstSet = descSets[i]; w[6].dstBinding = 6;
            w[6].descriptorCount = 1;
            w[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[6].pImageInfo = &shraw;
            // дописываем воду 7,8 отдельным апдейтом (w[] было на 7 слотов):
            VkWriteDescriptorSet wx[2]{};
            wx[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wx[0].dstSet = descSets[i]; wx[0].dstBinding = 7;
            wx[0].descriptorCount = 1;
            wx[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wx[0].pBufferInfo = &wbi[0];
            wx[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wx[1].dstSet = descSets[i]; wx[1].dstBinding = 8;
            wx[1].descriptorCount = 1;
            wx[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wx[1].pBufferInfo = &wbi[1];
            vkUpdateDescriptorSets(device, 7, w, 0, nullptr);
            vkUpdateDescriptorSets(device, 2, wx, 0, nullptr);
        }
    }
    del.push([&]() {
        for (int i = 0; i < 2; i++) vmaDestroyBuffer(alloc, uboBuf[i], uboAlloc[i]);
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    });
    // ---- compute-cull: свой layout (meta/vis/indirect + вода meta/indirect) ----
    VkDescriptorSetLayout cullLayout;
    VkDescriptorSet cullSet;
    VkPipelineLayout cullPipeLayout;
    VkPipeline cullPipe;
    {
        VkDescriptorSetLayoutBinding cb[5]{};
        for (int b = 0; b < 5; b++) {
            cb[b].binding = (uint32_t)b;
            cb[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            cb[b].descriptorCount = 1;
            cb[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 5; li.pBindings = cb;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &cullLayout));
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps.descriptorCount = 5;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VkDescriptorPool cullPool;
        VK_CHECK(vkCreateDescriptorPool(device, &pi, nullptr, &cullPool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = cullPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &cullLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &ai, &cullSet));
        VkDescriptorBufferInfo bi[5]{};
        bi[0].buffer = metaBuf; bi[0].range = VK_WHOLE_SIZE;
        bi[1].buffer = visBuf; bi[1].range = VK_WHOLE_SIZE;
        bi[2].buffer = indBuf; bi[2].range = VK_WHOLE_SIZE;
        bi[3].buffer = waterMetaBuf; bi[3].range = VK_WHOLE_SIZE;
        bi[4].buffer = waterIndBuf; bi[4].range = VK_WHOLE_SIZE;
        VkWriteDescriptorSet w[5]{};
        for (int b = 0; b < 5; b++) {
            w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[b].dstSet = cullSet; w[b].dstBinding = (uint32_t)b;
            w[b].descriptorCount = 1;
            w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[b].pBufferInfo = &bi[b];
        }
        vkUpdateDescriptorSets(device, 5, w, 0, nullptr);
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.size = 6 * sizeof(glm::vec4); pc.offset = 0;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &cullLayout;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(device, &pli, nullptr, &cullPipeLayout));
        VkShaderModule cs = makeShader(device, SHADER_DIR "cull.comp.spv");
        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = cs; cpi.stage.pName = "main";
        cpi.layout = cullPipeLayout;
        VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpi, nullptr, &cullPipe));
        vkDestroyShaderModule(device, cs, nullptr);
        del.push([=, &device]() {
            vkDestroyPipeline(device, cullPipe, nullptr);
            vkDestroyPipelineLayout(device, cullPipeLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, cullLayout, nullptr);
            vkDestroyDescriptorPool(device, cullPool, nullptr);
        });
    }

    // ---- demo-5b bloom-цели (GENERAL навсегда): A половина, B четверть, A2 половина-финал ----
    VkImage bloomImg[3] = {nullptr, nullptr, nullptr};
    VmaAllocation bloomAlloc[3] = {nullptr, nullptr, nullptr};
    VkImageView bloomView[3] = {nullptr, nullptr, nullptr};
    VkSampler bloomSmp = nullptr;
    {
        uint32_t bw[3] = {(swapExtent.width + 1) / 2, (swapExtent.width + 3) / 4,
                          (swapExtent.width + 1) / 2};
        uint32_t bh[3] = {(swapExtent.height + 1) / 2, (swapExtent.height + 3) / 4,
                          (swapExtent.height + 1) / 2};
        for (int i = 0; i < 3; i++) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            ci.extent = {bw[i], bh[i], 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(alloc, &ci, &ai, &bloomImg[i], &bloomAlloc[i], nullptr));
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = bloomImg[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device, &vi, nullptr, &bloomView[i]));
            immRun([&](VkCommandBuffer cb) {
                imgBarrier(cb, bloomImg[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            });
        }
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(device, &si, nullptr, &bloomSmp));
    }
    del.push([&]() {
        vkDestroySampler(device, bloomSmp, nullptr);
        for (int i = 0; i < 3; i++) {
            vkDestroyImageView(device, bloomView[i], nullptr);
            vmaDestroyImage(alloc, bloomImg[i], bloomAlloc[i]);
        }
    });

    // ---- demo-5a пост: 2 набора (на кадр: exp ping-pong; апдейт до бинда = безопасно) ----
    VkDescriptorSetLayout postLayout;
    VkDescriptorSet postSet[2];
    VkPipelineLayout postComputeLayout; // lum+adapt делят (push dt/parity)
    VkPipeline lumPipe, adaptPipe;
    {
        VkDescriptorSetLayoutBinding pb[6]{};
        pb[0].binding = 0;
        pb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[0].descriptorCount = 1;
        pb[0].stageFlags = (VkShaderStageFlags)(VK_SHADER_STAGE_COMPUTE_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT);
        pb[1].binding = 1;
        pb[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[1].descriptorCount = 1;
        pb[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        for (int b = 2; b < 5; b++) {
            pb[b].binding = (uint32_t)b;
            pb[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            pb[b].descriptorCount = 1;
            pb[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        pb[5].binding = 5; // demo-5b: bloom для тонемэппа
        pb[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[5].descriptorCount = 1;
        pb[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding pball[6] = {pb[0], pb[1], pb[2], pb[3], pb[4], pb[5]};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 6; li.pBindings = pball;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &postLayout));
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 6;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = 6;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 2;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        VkDescriptorPool postPool;
        VK_CHECK(vkCreateDescriptorPool(device, &pi, nullptr, &postPool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = postPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &postLayout;
        for (int i = 0; i < 2; i++) {
            VK_CHECK(vkAllocateDescriptorSets(device, &ai, &postSet[i]));
            VkDescriptorImageInfo ii[2]{};
            ii[0].sampler = hdrSmp; ii[0].imageView = hdrView;
            ii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ii[1].sampler = expSmp; ii[1].imageView = expView[i];
            ii[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo si[3]{};
            VkImageView siv[3] = {lumView, expView[0], expView[1]};
            for (int b = 0; b < 3; b++) {
                si[b].sampler = VK_NULL_HANDLE; si[b].imageView = siv[b];
                si[b].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            VkDescriptorImageInfo bi5{};
            bi5.sampler = bloomSmp; bi5.imageView = bloomView[2];
            bi5.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet w[6]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = postSet[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].pImageInfo = &ii[0];
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = postSet[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].pImageInfo = &ii[1];
            for (int b = 2; b < 5; b++) {
                w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[b].dstSet = postSet[i]; w[b].dstBinding = (uint32_t)b;
                w[b].descriptorCount = 1;
                w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w[b].pImageInfo = &si[b - 2];
            }
            w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[5].dstSet = postSet[i]; w[5].dstBinding = 5;
            w[5].descriptorCount = 1;
            w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[5].pImageInfo = &bi5;
            vkUpdateDescriptorSets(device, 6, w, 0, nullptr);
        }
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.size = 16; pc.offset = 0; // dt + parity + pad
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &postLayout;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(device, &pli, nullptr, &postComputeLayout));
        auto mkCompute = [&](const char* spv, VkPipeline& out) {
            VkShaderModule cs = makeShader(device, spv);
            VkComputePipelineCreateInfo cpi{};
            cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpi.stage.module = cs; cpi.stage.pName = "main";
            cpi.layout = postComputeLayout;
            VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpi, nullptr, &out));
            vkDestroyShaderModule(device, cs, nullptr);
        };
        char lumPath[1024], adaptPath[1024];
        snprintf(lumPath, sizeof(lumPath), "%slum.comp.spv", SHADER_DIR);
        snprintf(adaptPath, sizeof(adaptPath), "%sadapt.comp.spv", SHADER_DIR);
        mkCompute(lumPath, lumPipe);
        mkCompute(adaptPath, adaptPipe);
        del.push([=, &device]() {
            vkDestroyPipeline(device, lumPipe, nullptr);
            vkDestroyPipeline(device, adaptPipe, nullptr);
            vkDestroyPipelineLayout(device, postComputeLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, postLayout, nullptr);
            vkDestroyDescriptorPool(device, postPool, nullptr);
        });
    }

    // ---- demo-5b bloom-наборы: 3 прохода (bright/down/up), картинки статичны ----
    VkDescriptorSetLayout bloomLayout;
    VkDescriptorSet bloomSet[3];
    VkPipelineLayout bloomPipeLayout;
    VkPipeline brightPipe, kdownPipe, kupPipe;
    {
        VkDescriptorSetLayoutBinding bb[3]{};
        bb[0].binding = 0;
        bb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bb[0].descriptorCount = 1;
        bb[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bb[1].binding = 1;
        bb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bb[1].descriptorCount = 1;
        bb[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bb[2].binding = 2; // только up (база); down игнорирует
        bb[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bb[2].descriptorCount = 1;
        bb[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bb;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &bloomLayout));
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 3 * 2;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = 3;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 3;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        VkDescriptorPool bloomPool;
        VK_CHECK(vkCreateDescriptorPool(device, &pi, nullptr, &bloomPool));
        // проходы: 0 bright HDR->A, 1 down A->B, 2 up B->A2 (+base A)
        VkImage srcs[3] = {hdrImg, bloomImg[0], bloomImg[1]};
        VkImageView srcv[3] = {hdrView, bloomView[0], bloomView[1]};
        VkSampler srcsmp[3] = {hdrSmp, bloomSmp, bloomSmp};
        VkImage dsts[3] = {bloomImg[0], bloomImg[1], bloomImg[2]};
        VkImageView dstv[3] = {bloomView[0], bloomView[1], bloomView[2]};
        for (int i = 0; i < 3; i++) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = bloomPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &bloomLayout;
            VK_CHECK(vkAllocateDescriptorSets(device, &ai, &bloomSet[i]));
            VkDescriptorImageInfo ii[3]{};
            ii[0].sampler = srcsmp[i]; ii[0].imageView = srcv[i];
            ii[0].imageLayout = (i == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         : VK_IMAGE_LAYOUT_GENERAL;
            ii[1].sampler = VK_NULL_HANDLE; ii[1].imageView = dstv[i];
            ii[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            ii[2].sampler = bloomSmp; ii[2].imageView = bloomView[0];
            ii[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet w[3]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = bloomSet[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].pImageInfo = &ii[0];
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = bloomSet[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w[1].pImageInfo = &ii[1];
            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = bloomSet[i]; w[2].dstBinding = 2;
            w[2].descriptorCount = 1;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[2].pImageInfo = &ii[2];
            vkUpdateDescriptorSets(device, 3, w, 0, nullptr);
        }
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &bloomLayout;
        pli.pushConstantRangeCount = 0; pli.pPushConstantRanges = nullptr;
        VK_CHECK(vkCreatePipelineLayout(device, &pli, nullptr, &bloomPipeLayout));
        auto mkBCompute = [&](const char* name, VkPipeline& out) {
            char p[1024];
            snprintf(p, sizeof(p), "%s%s.spv", SHADER_DIR, name);
            VkShaderModule cs = makeShader(device, p);
            VkComputePipelineCreateInfo cpi{};
            cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpi.stage.module = cs; cpi.stage.pName = "main";
            cpi.layout = bloomPipeLayout;
            VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpi, nullptr, &out));
            vkDestroyShaderModule(device, cs, nullptr);
        };
        mkBCompute("bright.comp", brightPipe);
        mkBCompute("kdown.comp", kdownPipe);
        mkBCompute("kup.comp", kupPipe);
        del.push([=, &device]() {
            vkDestroyPipeline(device, brightPipe, nullptr);
            vkDestroyPipeline(device, kdownPipe, nullptr);
            vkDestroyPipeline(device, kupPipe, nullptr);
            vkDestroyPipelineLayout(device, bloomPipeLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, bloomLayout, nullptr);
            vkDestroyDescriptorPool(device, bloomPool, nullptr);
        });
    }

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
        pc.size = sizeof(glm::mat4); pc.offset = 0; // demo-4: lightSpace (была model)
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = 1; li.pSetLayouts = &setLayout;
        li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(device, &li, nullptr, &pipeLayout));
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        VkFormat hdrPipeFmt = VK_FORMAT_R16G16B16A16_SFLOAT; // террейн всегда в HDR!
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &hdrPipeFmt;
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

    // ---- demo-4 shadow pipeline: те же setLayout+push (совместим!), только глубина.
    // Depth bias — драйверный (рецепт книги), включается динамикой.
    VkPipeline shadowPipe;
    {
        VkShaderModule vs = makeShader(device, SHADER_DIR "shadow.vert.spv");
        VkShaderModule fs = makeShader(device, SHADER_DIR "shadow.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
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
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        rs.depthBiasEnable = VK_TRUE; // bias задаём командой (const/slope из книги)
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 0; cb.pAttachments = nullptr; // цвета нет
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_DEPTH_BIAS};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ri.colorAttachmentCount = 0; ri.pColorAttachmentFormats = nullptr;
        ri.depthAttachmentFormat = VK_FORMAT_D16_UNORM;
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
        pi.layout = pipeLayout; // тот же layout: set с giga/meta + push lightSpace
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &shadowPipe));
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }
    del.push([&]() { vkDestroyPipeline(device, shadowPipe, nullptr); });

    // ---- demo-5w вода: тот же layout (superset), бленд ON, глубину только читаем ----
    VkPipeline waterPipe;
    {
        VkShaderModule vs = makeShader(device, SHADER_DIR "water.vert.spv");
        VkShaderModule fs = makeShader(device, SHADER_DIR "water.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
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
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.blendEnable = VK_TRUE; // прозрачная гладь поверх террейна
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.alphaBlendOp = VK_BLEND_OP_ADD;
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_FALSE; // гладь не пишет глубину
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        VkFormat whdrFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &whdrFmt;
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
        pi.layout = pipeLayout; // superset: вода берёт биндинги 0,1,7,8
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &waterPipe));
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }
    del.push([&]() { vkDestroyPipeline(device, waterPipe, nullptr); });

    // ---- demo-4b рентген: фулскрин-три в угол 256x256 (F1), тот же setLayout ----
    VkPipeline dbgPipe;
    {
        VkShaderModule vs = makeShader(device, SHADER_DIR "tri.vert.spv");
        VkShaderModule fs = makeShader(device, SHADER_DIR "dbg.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
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
        rs.cullMode = VK_CULL_MODE_NONE;
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
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &swapFormat;
        ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
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
        pi.layout = pipeLayout; // тот же set (binding 6 с сырой глубиной)
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &dbgPipe));
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }
    del.push([&]() { vkDestroyPipeline(device, dbgPipe, nullptr); });

    // ---- demo-5a небо + тонемэппинг: фулскрин-пайпы (layout = набор + свой пуш) ----
    VkPipelineLayout skyPipeLayout, tonemapPipeLayout;
    VkPipeline skyPipe, tonemapPipe;
    {
        auto mkLayout = [&](VkDescriptorSetLayout set, uint32_t pushSize,
                            VkShaderStageFlags pushStage, VkPipelineLayout& out) {
            VkPushConstantRange pc{};
            pc.stageFlags = pushStage;
            pc.size = pushSize; pc.offset = 0;
            VkPipelineLayoutCreateInfo li{};
            li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            li.setLayoutCount = 1; li.pSetLayouts = &set;
            li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pc;
            VK_CHECK(vkCreatePipelineLayout(device, &li, nullptr, &out));
        };
        mkLayout(setLayout, 16, VK_SHADER_STAGE_FRAGMENT_BIT, skyPipeLayout);
        mkLayout(postLayout, 16, VK_SHADER_STAGE_FRAGMENT_BIT, tonemapPipeLayout);
        auto mkFullPipe = [&](const char* fsName, VkFormat colorFmt,
                              VkPipelineLayout layout, VkPipeline& out) {
            VkShaderModule vs = makeShader(device, SHADER_DIR "tri.vert.spv");
            char fsPath[1024];
            snprintf(fsPath, sizeof(fsPath), "%s%s.spv", SHADER_DIR, fsName);
            VkShaderModule fs = makeShader(device, fsPath);
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs; stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs; stages[1].pName = "main";
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
            rs.cullMode = VK_CULL_MODE_NONE;
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
            ds.depthTestEnable = VK_FALSE;
            ds.depthWriteEnable = VK_FALSE;
            VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dyn{};
            dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
            VkGraphicsPipelineCreateInfo pi{};
            pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            VkPipelineRenderingCreateInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &colorFmt;
            ri.depthAttachmentFormat = VK_FORMAT_UNDEFINED; // без глубины
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
            pi.layout = layout;
            VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &out));
            vkDestroyShaderModule(device, vs, nullptr);
            vkDestroyShaderModule(device, fs, nullptr);
        };
        mkFullPipe("sky.frag", VK_FORMAT_R16G16B16A16_SFLOAT, skyPipeLayout, skyPipe);
        mkFullPipe("tonemap.frag", swapFormat, tonemapPipeLayout, tonemapPipe);
        del.push([&]() {
            vkDestroyPipeline(device, skyPipe, nullptr);
            vkDestroyPipeline(device, tonemapPipe, nullptr);
            vkDestroyPipelineLayout(device, skyPipeLayout, nullptr);
            vkDestroyPipelineLayout(device, tonemapPipeLayout, nullptr);
        });
    }

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
    float tod = 1.5707f; // полдень (1/2/3 утро/день/вечер, F1 рентген карты)
    bool dbgShadow = false, prevF1 = false;
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
            if (glfwGetKey(window, GLFW_KEY_1)) tod = 0.5f;   // утро: длинные тени
            if (glfwGetKey(window, GLFW_KEY_2)) tod = 1.5707f; // полдень
            if (glfwGetKey(window, GLFW_KEY_3)) tod = 2.6f;    // вечер: длинные тени
            bool f1 = glfwGetKey(window, GLFW_KEY_F1) != 0;
            if (f1 && !prevF1) { dbgShadow = !dbgShadow; printf("shadow xray %d\n", dbgShadow); }
            prevF1 = f1;
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
            u.invViewProj = glm::inverse(u.viewProj);
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
        // demo-3c + вода: обнулить оба счётчика (fill + барьер transfer->compute).
        vkCmdFillBuffer(cmdBufs[fi], indBuf, 0, 4, 0);
        vkCmdFillBuffer(cmdBufs[fi], waterIndBuf, 0, 4, 0);
        {
            VkBufferMemoryBarrier b[2]{};
            VkBuffer bbs[2] = {indBuf, waterIndBuf};
            for (int i = 0; i < 2; i++) {
                b[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                b[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                b[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                b[i].buffer = bbs[i]; b[i].offset = 0; b[i].size = VK_WHOLE_SIZE;
            }
            vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 2, b, 0, nullptr);
        }
        // 2) плоскости фрустума (строки viewProj, нормированные).
        glm::mat4 vp = proj * glm::lookAt(camPos, camPos + camFront, glm::vec3(0, 1, 0));
        glm::vec4 planes[6];
        {
            glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
            glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
            glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
            glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
            planes[0] = r3 + r0; planes[1] = r3 - r0;
            planes[2] = r3 + r1; planes[3] = r3 - r1;
            planes[4] = r3 + r2; planes[5] = r3 - r2;
            for (int i = 0; i < 6; i++) planes[i] /= glm::length(glm::vec3(planes[i]));
        }
        vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, cullPipe);
        vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                cullPipeLayout, 0, 1, &cullSet, 0, nullptr);
        vkCmdPushConstants(cmdBufs[fi], cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(planes), planes);
        vkCmdDispatch(cmdBufs[fi], 1, 1, 1); // 64 потока = 64 чанка
        // 3) барьер: compute-write -> indirect-read + vertex-read (оба indirect!).
        {
            VkBufferMemoryBarrier b[3]{};
            VkBuffer bbs[3] = {indBuf, visBuf, waterIndBuf};
            for (int i = 0; i < 3; i++) {
                b[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                b[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                b[i].buffer = bbs[i];
                b[i].offset = 0; b[i].size = VK_WHOLE_SIZE;
            }
            b[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            b[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b[2].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                 0, 0, nullptr, 3, b, 0, nullptr);
        }
        // demo-4: матрица солнца (снап в light-space + scale/bias fold, GL-рецепт).
        // Солнце фикс-полдень; квант не нужен (нет цикла дня), снап нужен (камера едет).
        glm::mat4 lightSpace;
        {
            glm::vec3 sun = glm::normalize(glm::vec3(cos(tod), sin(tod), 0.35f));
            const float SE = 70.0f;
            float texel = 2.0f * SE / (float)SHADOW_S;
            glm::vec3 L = sun;
            glm::vec3 up0 = fabs(L.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            glm::vec3 xx = glm::normalize(glm::cross(up0, L));
            glm::vec3 yx = glm::cross(L, xx);
            glm::vec3 center = camPos;
            float lx = glm::dot(center, xx), ly = glm::dot(center, yx), lz = glm::dot(center, L);
            lx = floor(lx / texel + 0.5f) * texel;
            ly = floor(ly / texel + 0.5f) * texel;
            center = xx * lx + yx * ly + L * lz;
            glm::mat4 sb(1.0f); // NDC->0..1 по всем осям (GLM даёт глубину [-1,1])
            sb = glm::translate(sb, glm::vec3(0.5f, 0.5f, 0.5f));
            sb = glm::scale(sb, glm::vec3(0.5f, 0.5f, 0.5f));
            lightSpace = sb * glm::ortho(-SE, SE, -SE, SE, 1.0f, 400.0f) *
                         glm::lookAt(center, center + L, yx);
        }
        // demo-4 shadow pass: та же видимость (indirect), только глубина.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = (shadowLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                  ? 0 : VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.oldLayout = shadowLayout;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            b.image = shadowImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBufs[fi],
                                 (shadowLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                     ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                     : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            shadowLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
        VkRenderingAttachmentInfo sdepth{};
        sdepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        sdepth.imageView = shadowView;
        sdepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        sdepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sdepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        sdepth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo sri{};
        sri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        sri.renderArea = {{0, 0}, {(uint32_t)SHADOW_S, (uint32_t)SHADOW_S}};
        sri.layerCount = 1;
        sri.colorAttachmentCount = 0;
        sri.pDepthAttachment = &sdepth;
        vkCmdBeginRendering(cmdBufs[fi], &sri);
        vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe);
        {
            VkViewport svp{0, 0, (float)SHADOW_S, (float)SHADOW_S, 0.0f, 1.0f};
            VkRect2D ssc{{0, 0}, {(uint32_t)SHADOW_S, (uint32_t)SHADOW_S}};
            vkCmdSetViewport(cmdBufs[fi], 0, 1, &svp);
            vkCmdSetScissor(cmdBufs[fi], 0, 1, &ssc);
        }
        vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeLayout, 0, 1, &descSets[fi], 0, nullptr);
        vkCmdPushConstants(cmdBufs[fi], pipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(lightSpace), &lightSpace);
        vkCmdSetDepthBias(cmdBufs[fi], 1.1f, 0.0f, 2.0f); // const/slope из книги
        vkCmdDrawIndirectCount(cmdBufs[fi], indBuf, sizeof(uint32_t) * 4, indBuf, 0,
                               64, sizeof(VkDrawIndirectCommand));
        vkCmdEndRendering(cmdBufs[fi]);
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = shadowImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            shadowLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        // demo-5a: HDR-цепочка. Небо и террейн пишут HDR, дальше compute + тонемэппинг.
        // HDR-переход (трекаем как своп).
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = (hdrLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                  ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.oldLayout = hdrLayout;
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = hdrImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBufs[fi],
                                 (hdrLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                     ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                     : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            hdrLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkViewport svwp{0, 0, (float)swapExtent.width, (float)swapExtent.height, 0.0f, 1.0f};
        VkRect2D ssc{{0, 0}, swapExtent};
        // Небо первым (без глубины, CLEAR поверх всего).
        {
            VkRenderingAttachmentInfo sky{};
            sky.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            sky.imageView = hdrView;
            sky.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            sky.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            sky.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            sky.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            VkRenderingInfo sri{};
            sri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            sri.renderArea = {{0, 0}, swapExtent};
            sri.layerCount = 1;
            sri.colorAttachmentCount = 1;
            sri.pColorAttachments = &sky;
            vkCmdBeginRendering(cmdBufs[fi], &sri);
            vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe);
            vkCmdSetViewport(cmdBufs[fi], 0, 1, &svwp);
            vkCmdSetScissor(cmdBufs[fi], 0, 1, &ssc);
            vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    skyPipeLayout, 0, 1, &descSets[fi], 0, nullptr);
            glm::vec4 viewSize((float)swapExtent.width, (float)swapExtent.height, 0, 0);
            vkCmdPushConstants(cmdBufs[fi], skyPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(viewSize), &viewSize);
            vkCmdDraw(cmdBufs[fi], 3, 1, 0, 0);
            vkCmdEndRendering(cmdBufs[fi]);
        }
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = hdrView; // террейн — в HDR поверх неба (LOAD!)
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
        vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeLayout, 0, 1, &descSets[fi], 0, nullptr);
        // 4) один indirect-count draw на всё видимое (команды пишет compute).
        vkCmdPushConstants(cmdBufs[fi], pipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(lightSpace), &lightSpace);
        vkCmdDrawIndirectCount(cmdBufs[fi], indBuf, sizeof(uint32_t) * 4, indBuf, 0,
                               64, sizeof(VkDrawIndirectCommand));
        // demo-5w вода поверх (тот же HDR+глубина LOAD, бленд, глубину не пишет).
        vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipe);
        vkCmdDrawIndirectCount(cmdBufs[fi], waterIndBuf, sizeof(uint32_t) * 4, waterIndBuf, 0,
                               64, sizeof(VkDrawIndirectCommand));
        vkCmdEndRendering(cmdBufs[fi]);
        // demo-5a пост: HDR -> lum -> adapt -> тонемэппинг в своп.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = hdrImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            hdrLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        // exp для тонемэппа: binding 1 -> только что записанный.
        // ДО всех биндов сета в кадре (апдейт после бинда инвалидирует запись)!
        // (parity объявлен ниже у adapt; здесь inline по frame.)
        {
            VkDescriptorImageInfo ei{};
            ei.sampler = expSmp;
            ei.imageView = (frame % 2 == 0) ? expView[1] : expView[0];
            ei.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = postSet[fi]; w.dstBinding = 1;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &ei;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
        // demo-5b bloom-цепочка: bright HDR->A, down A->B, up B->A2(+base A).
        {
            struct BloomPass { VkPipeline pipe; VkDescriptorSet set; uint32_t w, h; };
            uint32_t hw = (swapExtent.width + 1) / 2, hh = (swapExtent.height + 1) / 2;
            uint32_t qw = (swapExtent.width + 3) / 4, qh = (swapExtent.height + 3) / 4;
            BloomPass ps[3] = {{brightPipe, bloomSet[0], hw, hh},
                               {kdownPipe, bloomSet[1], qw, qh},
                               {kupPipe, bloomSet[2], hw, hh}};
            for (int i = 0; i < 3; i++) {
                vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, ps[i].pipe);
                vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                        bloomPipeLayout, 0, 1, &ps[i].set, 0, nullptr);
                vkCmdDispatch(cmdBufs[fi], (ps[i].w + 7) / 8, (ps[i].h + 7) / 8, 1);
                VkImageMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                b.image = (i == 0) ? bloomImg[0] : ((i == 1) ? bloomImg[1] : bloomImg[2]);
                b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     (i == 2) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                              : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
            }
        }
        vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, lumPipe);
        vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                postComputeLayout, 0, 1, &postSet[fi], 0, nullptr);
        vkCmdDispatch(cmdBufs[fi], 8, 5, 1); // 64x36 тайлов
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.image = lumImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        float parity = (frame % 2 == 0) ? 0.0f : 1.0f; // чёт: читаем A пишем B
        {
            struct AdaptPush { float dt, parity, p0, p1; } ap{dt, parity, 0, 0};
            vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, adaptPipe);
            vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                    postComputeLayout, 0, 1, &postSet[fi], 0, nullptr);
            vkCmdPushConstants(cmdBufs[fi], postComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(ap), &ap);
            vkCmdDispatch(cmdBufs[fi], 1, 1, 1);
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.image = (parity < 0.5f) ? expImg[1] : expImg[0];
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        // (exp binding 1 обновлён до биндов выше — см. перед lum.)
        {
            VkRenderingAttachmentInfo tm{};
            tm.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            tm.imageView = swapViews[imgIdx];
            tm.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            tm.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            tm.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            tm.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            VkRenderingInfo tri{};
            tri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            tri.renderArea = {{0, 0}, swapExtent};
            tri.layerCount = 1;
            tri.colorAttachmentCount = 1;
            tri.pColorAttachments = &tm;
            vkCmdBeginRendering(cmdBufs[fi], &tri);
            vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipe);
            vkCmdSetViewport(cmdBufs[fi], 0, 1, &vwp);
            vkCmdSetScissor(cmdBufs[fi], 0, 1, &sc);
            vkCmdBindDescriptorSets(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    tonemapPipeLayout, 0, 1, &postSet[fi], 0, nullptr);
            glm::vec4 res((float)swapExtent.width, (float)swapExtent.height, 0, 0);
            vkCmdPushConstants(cmdBufs[fi], tonemapPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(res), &res);
            vkCmdDraw(cmdBufs[fi], 3, 1, 0, 0);
            vkCmdEndRendering(cmdBufs[fi]);
        }
        // 5) рентген карты в угол (F1): второй проход по свопу (LOAD).
        if (dbgShadow) {
            VkRenderingAttachmentInfo dg{};
            dg.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            dg.imageView = swapViews[imgIdx];
            dg.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            dg.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            dg.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo dri{};
            dri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            dri.renderArea = {{0, 0}, swapExtent};
            dri.layerCount = 1;
            dri.colorAttachmentCount = 1;
            dri.pColorAttachments = &dg;
            vkCmdBeginRendering(cmdBufs[fi], &dri);
            vkCmdBindPipeline(cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, dbgPipe);
            VkViewport dvp{0, 0, 256, 256, 0.0f, 1.0f};
            VkRect2D dsc{{0, 0}, {256, 256}};
            vkCmdSetViewport(cmdBufs[fi], 0, 1, &dvp);
            vkCmdSetScissor(cmdBufs[fi], 0, 1, &dsc);
            vkCmdDraw(cmdBufs[fi], 3, 1, 0, 0);
            vkCmdEndRendering(cmdBufs[fi]);
        }
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
