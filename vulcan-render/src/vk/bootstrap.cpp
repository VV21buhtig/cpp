// bootstrap: окно + instance (+валидация) + surface + GPU + device + VMA +
// swapchain + imm пул. Вырезано из main один в один, порядок тот же.
#include "vk/vk_ctx.h"

#include <cstring>
#include <vector>

std::vector<char> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> b(n);
    if (fread(b.data(), 1, n, f) != (size_t)n) { printf("read fail %s\n", path); exit(1); }
    fclose(f);
    return b;
}

VkShaderModule makeShader(VkDevice dev, const char* path) {
    auto code = readFile(path);
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &m));
    return m;
}

void imgBarrier(VkCommandBuffer cb, VkImage img, VkImageLayout oldL, VkImageLayout newL,
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
}

void VkCore::immRun(std::function<void(VkCommandBuffer)> fn) {
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
}

bool vkInitCore(VkCore& c) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    c.window = glfwCreateWindow(1280, 720, "vulcan-render demo-4 shadows", nullptr, nullptr);
    glfwSetInputMode(c.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetWindowUserPointer(c.window, &c.ctl);
    glfwSetCursorPosCallback(c.window, [](GLFWwindow* w, double x, double y) {
        auto* ctl = (CamCtl*)glfwGetWindowUserPointer(w);
        if (ctl->first) { ctl->lastX = (float)x; ctl->lastY = (float)y; ctl->first = false; }
        float dx = (float)x - ctl->lastX, dy = ctl->lastY - (float)y;
        ctl->lastX = (float)x; ctl->lastY = (float)y;
        ctl->yaw += dx * 0.12f;
        ctl->pitch += dy * 0.12f;
        if (ctl->pitch > 89.0f) ctl->pitch = 89.0f;
        if (ctl->pitch < -89.0f) ctl->pitch = -89.0f;
    });

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
        VK_CHECK(vkCreateInstance(&ci, nullptr, &c.instance));
    }
    c.del.push([self = &c]() { vkDestroyInstance(self->instance, nullptr); });

    // Debug messenger: без него слои молчат. Печатаем всё ≥ warning.
    {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(c.instance, "vkCreateDebugUtilsMessengerEXT");
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(c.instance, "vkDestroyDebugUtilsMessengerEXT");
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
            if (create(c.instance, &ci, nullptr, &c.dbgMessenger) == VK_SUCCESS)
                printf("validation messenger ON\n");
            c.del.push([self = &c, destroy]() {
                destroy(self->instance, self->dbgMessenger, nullptr);
            });
        }
    }

    VK_CHECK(glfwCreateWindowSurface(c.instance, c.window, nullptr, &c.surface));

    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
        std::vector<VkPhysicalDevice> gpus(n);
        vkEnumeratePhysicalDevices(c.instance, &n, gpus.data());
        for (auto g : gpus) {
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qs(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, qs.data());
            for (uint32_t i = 0; i < qn; i++) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(g, i, c.surface, &present);
                if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    VkPhysicalDeviceProperties p;
                    vkGetPhysicalDeviceProperties(g, &p);
                    printf("GPU: %s\n", p.deviceName);
                    c.maxAniso = p.limits.maxSamplerAnisotropy;
                    c.gpu = g; c.gfxFamily = i;
                    break;
                }
            }
            if (c.gpu) break;
        }
        if (!c.gpu) { printf("no suitable GPU\n"); return false; }
    }

    {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = c.gfxFamily;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        VkPhysicalDeviceDynamicRenderingFeatures dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dyn.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceVulkan12Features feat12{};
        feat12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        feat12.drawIndirectCount = VK_TRUE; // demo-3c: vkCmdDrawIndirectCount
        feat12.pNext = &dyn;
        VkPhysicalDeviceFeatures2 feats2{};
        feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats2.pNext = &feat12;
        feats2.features.vertexPipelineStoresAndAtomics = VK_TRUE; // DEBUG VS-store
        feats2.features.samplerAnisotropy = VK_TRUE;
        const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.pNext = &feats2; // feats2 -> feat12 -> dyn (pEnabledFeatures игнорим)
        ci.pEnabledFeatures = nullptr;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &qi;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = devExts;
        VK_CHECK(vkCreateDevice(c.gpu, &ci, nullptr, &c.device));
        vkGetDeviceQueue(c.device, c.gfxFamily, 0, &c.gfxQueue);
    }
    c.del.push([self = &c]() { vkDestroyDevice(self->device, nullptr); });

    {
        VmaAllocatorCreateInfo ai{};
        ai.physicalDevice = c.gpu;
        ai.device = c.device;
        ai.instance = c.instance;
        ai.vulkanApiVersion = VK_API_VERSION_1_3;
        VK_CHECK(vmaCreateAllocator(&ai, &c.alloc));
    }
    c.del.push([self = &c]() { vmaDestroyAllocator(self->alloc); });

    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(c.gpu, c.surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(c.gpu, c.surface, &n, fmts.data());
        VkSurfaceFormatKHR picked = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { picked = f; break; }
        c.swapFormat = picked.format;
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.gpu, c.surface, &caps);
        c.swapExtent = caps.currentExtent;
        if (c.swapExtent.width == 0xFFFFFFFF) c.swapExtent = {1280, 720};
        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = c.surface;
        ci.minImageCount = caps.minImageCount + 1;
        ci.imageFormat = c.swapFormat;
        ci.imageColorSpace = picked.colorSpace;
        ci.imageExtent = c.swapExtent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        ci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(c.device, &ci, nullptr, &c.swapchain));
        uint32_t in = 0;
        vkGetSwapchainImagesKHR(c.device, c.swapchain, &in, nullptr);
        c.swapImages.resize(in);
        vkGetSwapchainImagesKHR(c.device, c.swapchain, &in, c.swapImages.data());
        for (auto img : c.swapImages) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = c.swapFormat;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView v;
            VK_CHECK(vkCreateImageView(c.device, &vi, nullptr, &v));
            c.swapViews.push_back(v);
        }
    }
    c.del.push([self = &c]() {
        for (auto v : self->swapViews) vkDestroyImageView(self->device, v, nullptr);
        vkDestroySwapchainKHR(self->device, self->swapchain, nullptr);
        vkDestroySurfaceKHR(self->instance, self->surface, nullptr);
    });

    // Разовые команды (загрузки, переходы): свой пул + fence.
    {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                   VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // reset нужен immRun
        ci.queueFamilyIndex = c.gfxFamily;
        VK_CHECK(vkCreateCommandPool(c.device, &ci, nullptr, &c.immPool));
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = c.immPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(c.device, &ai, &c.immBuf));
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(c.device, &fi, nullptr, &c.immFence));
    }
    c.del.push([self = &c]() {
        vkDestroyFence(self->device, self->immFence, nullptr);
        vkDestroyCommandPool(self->device, self->immPool, nullptr);
    });
    return true;
}
