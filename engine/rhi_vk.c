// RHI-VK: мясной бэкенд. C11, без слоёв. Ошибки — словами через rhi_vk_str.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rhi_vk.h"

#define RHI_CHK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    fprintf(stderr, "rhi_vk fail: %s (%s:%d)\n", rhi_vk_str(_r), __FILE__, __LINE__); \
    return 0; } } while (0)

const char *rhi_vk_str(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "OK";
    case VK_NOT_READY: return "NOT_READY";
    case VK_TIMEOUT: return "TIMEOUT";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "EXTENSION_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "INCOMPATIBLE_DRIVER";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "OUT_OF_POOL_MEMORY";
    default: return "VK_ERROR";
    }
}

uint32_t rhi_vk_mem(RhiVk *r, uint32_t mask, VkMemoryPropertyFlags fl) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(r->pdev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mask & (1u << i)) && (mp.memoryTypes[i].propertyFlags & fl) == fl)
            return i;
    return 0xFFFFFFFFu;
}

int rhi_vk_init(RhiVk *r, void *glfwWindow, const char **extNames, uint32_t extN) {
    memset(r, 0, sizeof *r);
    GLFWwindow *win = (GLFWwindow *)glfwWindow;
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "voxengine";
    ai.apiVersion = VK_API_VERSION_1_1; // 1.1: минус-высота viewport легальна
    uint32_t gextN = 0;
    const char **gexts = glfwGetRequiredInstanceExtensions(&gextN);
    const char *all[16];
    uint32_t allN = 0;
    for (uint32_t i = 0; i < gextN && allN < 16; i++) all[allN++] = gexts[i];
    for (uint32_t i = 0; i < extN && allN < 16; i++) all[allN++] = extNames[i];
    VkInstanceCreateInfo ii;
    memset(&ii, 0, sizeof ii);
    ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ii.pApplicationInfo = &ai;
    ii.enabledExtensionCount = allN;
    ii.ppEnabledExtensionNames = all;
    RHI_CHK(vkCreateInstance(&ii, 0, &r->inst));
    RHI_CHK(glfwCreateWindowSurface(r->inst, win, 0, &r->surf));
    uint32_t pdN = 0;
    RHI_CHK(vkEnumeratePhysicalDevices(r->inst, &pdN, 0));
    if (!pdN) { fprintf(stderr, "rhi_vk: no devices\n"); return 0; }
    VkPhysicalDevice pds[8];
    if (pdN > 8) pdN = 8;
    RHI_CHK(vkEnumeratePhysicalDevices(r->inst, &pdN, pds));
    uint32_t qfam = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < pdN && qfam == 0xFFFFFFFFu; i++) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &qn, 0);
        VkQueueFamilyProperties qp[16];
        if (qn > 16) qn = 16;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &qn, qp);
        for (uint32_t q = 0; q < qn; q++) {
            VkBool32 pres = 0;
            vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], q, r->surf, &pres);
            if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
                r->pdev = pds[i];
                qfam = q;
                break;
            }
        }
    }
    if (qfam == 0xFFFFFFFFu) { fprintf(stderr, "rhi_vk: no gfx+present\n"); return 0; }
    r->qfam = qfam;
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(r->pdev, &pp);
    snprintf(r->devName, sizeof r->devName, "%s", pp.deviceName);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi;
    memset(&qi, 0, sizeof qi);
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = qfam;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    const char *dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo di;
    memset(&di, 0, sizeof di);
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = dext;
    RHI_CHK(vkCreateDevice(r->pdev, &di, 0, &r->dev));
    vkGetDeviceQueue(r->dev, qfam, 0, &r->q);
    // Свопчейн FIFO, UNORM.
    VkSurfaceCapabilitiesKHR caps;
    RHI_CHK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->pdev, r->surf, &caps));
    uint32_t fn = 0;
    RHI_CHK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->pdev, r->surf, &fn, 0));
    if (!fn) { fprintf(stderr, "rhi_vk: no formats\n"); return 0; }
    VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR *)calloc(fn, sizeof *fmts);
    if (!fmts) return 0;
    VkResult fr = vkGetPhysicalDeviceSurfaceFormatsKHR(r->pdev, r->surf, &fn, fmts);
    if (fr != VK_SUCCESS && fr != VK_INCOMPLETE) {
        fprintf(stderr, "rhi_vk fail: %s\n", rhi_vk_str(fr));
        free(fmts);
        return 0;
    }
    VkSurfaceFormatKHR fmt = fmts[0];
    for (uint32_t i = 0; i < fn; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { fmt = fmts[i]; break; }
    free(fmts);
    r->imgFmt = fmt.format;
    r->extent.width = 1280;
    r->extent.height = 720;
    if (caps.currentExtent.width != 0xFFFFFFFFu) r->extent = caps.currentExtent;
    uint32_t imgN = caps.minImageCount + 1;
    if (caps.maxImageCount && imgN > caps.maxImageCount) imgN = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sw;
    memset(&sw, 0, sizeof sw);
    sw.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sw.surface = r->surf;
    sw.minImageCount = imgN;
    sw.imageFormat = r->imgFmt;
    sw.imageColorSpace = fmt.colorSpace;
    sw.imageExtent = r->extent;
    sw.imageArrayLayers = 1;
    sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sw.preTransform = caps.currentTransform;
    sw.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sw.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sw.clipped = VK_TRUE;
    RHI_CHK(vkCreateSwapchainKHR(r->dev, &sw, 0, &r->swap));
    RHI_CHK(vkGetSwapchainImagesKHR(r->dev, r->swap, &r->imgN, 0));
    r->imgs = (VkImage *)calloc(r->imgN, sizeof(VkImage));
    r->views = (VkImageView *)calloc(r->imgN, sizeof(VkImageView));
    if (!r->imgs || !r->views) return 0;
    RHI_CHK(vkGetSwapchainImagesKHR(r->dev, r->swap, &r->imgN, r->imgs));
    for (uint32_t i = 0; i < r->imgN; i++) {
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = r->imgs[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = r->imgFmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        RHI_CHK(vkCreateImageView(r->dev, &vi, 0, &r->views[i]));
    }
    VkCommandPoolCreateInfo pi;
    memset(&pi, 0, sizeof pi);
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = r->qfam;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    RHI_CHK(vkCreateCommandPool(r->dev, &pi, 0, &r->pool));
    VkCommandBufferAllocateInfo cai;
    memset(&cai, 0, sizeof cai);
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = r->pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = RHI_VK_FIF;
    RHI_CHK(vkAllocateCommandBuffers(r->dev, &cai, r->cmds));
    for (int i = 0; i < RHI_VK_FIF; i++) {
        VkSemaphoreCreateInfo si;
        memset(&si, 0, sizeof si);
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        RHI_CHK(vkCreateSemaphore(r->dev, &si, 0, &r->imgAvail[i]));
        RHI_CHK(vkCreateSemaphore(r->dev, &si, 0, &r->renderDone[i]));
        VkFenceCreateInfo fi2;
        memset(&fi2, 0, sizeof fi2);
        fi2.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi2.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        RHI_CHK(vkCreateFence(r->dev, &fi2, 0, &r->fences[i]));
    }
    return 1;
}

int rhi_vk_acquire(RhiVk *r, int fi, uint32_t *img) {
    vkWaitForFences(r->dev, 1, &r->fences[fi], 1, 1000000000ull);
    vkResetFences(r->dev, 1, &r->fences[fi]);
    VkResult ar = vkAcquireNextImageKHR(r->dev, r->swap, 1000000000ull,
                                        r->imgAvail[fi], VK_NULL_HANDLE, img);
    return (ar == VK_SUCCESS || ar == VK_SUBOPTIMAL_KHR) ? (int)*img : -1;
}

int rhi_vk_submit(RhiVk *r, int fi, uint32_t img, VkCommandBuffer cb) {
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si;
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &r->imgAvail[fi];
    si.pWaitDstStageMask = &ws;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &r->renderDone[fi];
    if (vkQueueSubmit(r->q, 1, &si, r->fences[fi]) != VK_SUCCESS) return 0;
    VkPresentInfoKHR pr;
    memset(&pr, 0, sizeof pr);
    pr.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pr.waitSemaphoreCount = 1;
    pr.pWaitSemaphores = &r->renderDone[fi];
    pr.swapchainCount = 1;
    pr.pSwapchains = &r->swap;
    pr.pImageIndices = &img;
    return vkQueuePresentKHR(r->q, &pr) == VK_SUCCESS;
}

int rhi_vk_once(RhiVk *r, VkCommandBuffer *cb) {
    VkCommandBufferAllocateInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = r->pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(r->dev, &ai, cb) != VK_SUCCESS) return 0;
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(*cb, &bi) != VK_SUCCESS) return 0;
    return 1;
}

int rhi_vk_once_end(RhiVk *r, VkCommandBuffer cb) {
    if (vkEndCommandBuffer(cb) != VK_SUCCESS) return 0;
    VkSubmitInfo si;
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence f = VK_NULL_HANDLE;
    VkFenceCreateInfo fi;
    memset(&fi, 0, sizeof fi);
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(r->dev, &fi, 0, &f) != VK_SUCCESS) return 0;
    VkResult sr = vkQueueSubmit(r->q, 1, &si, f);
    VkResult wr = VK_SUCCESS;
    if (sr == VK_SUCCESS) wr = vkWaitForFences(r->dev, 1, &f, 1, 1000000000ull);
    vkDestroyFence(r->dev, f, 0);
    vkFreeCommandBuffers(r->dev, r->pool, 1, &cb);
    return sr == VK_SUCCESS && wr == VK_SUCCESS;
}

int rhi_vk_hbuf(RhiVk *r, VkDeviceSize sz, VkBufferUsageFlags use,
                VkBuffer *b, VkDeviceMemory *m, void **ptr) {
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sz;
    bi.usage = use;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(r->dev, &bi, 0, b) != VK_SUCCESS) return 0;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(r->dev, *b, &mr);
    uint32_t mi = rhi_vk_mem(r, mr.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mi == 0xFFFFFFFFu) return 0;
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mi;
    if (vkAllocateMemory(r->dev, &ai, 0, m) != VK_SUCCESS) return 0;
    if (vkBindBufferMemory(r->dev, *b, *m, 0) != VK_SUCCESS) return 0;
    if (ptr && vkMapMemory(r->dev, *m, 0, mr.size, 0, ptr) != VK_SUCCESS) return 0;
    return 1;
}

void rhi_vk_shutdown(RhiVk *r) {
    if (!r->dev) return;
    vkDeviceWaitIdle(r->dev);
    for (int i = 0; i < RHI_VK_FIF; i++) {
        if (r->imgAvail[i]) vkDestroySemaphore(r->dev, r->imgAvail[i], 0);
        if (r->renderDone[i]) vkDestroySemaphore(r->dev, r->renderDone[i], 0);
        if (r->fences[i]) vkDestroyFence(r->dev, r->fences[i], 0);
    }
    if (r->views) {
        for (uint32_t i = 0; i < r->imgN; i++)
            if (r->views[i]) vkDestroyImageView(r->dev, r->views[i], 0);
        free(r->views);
    }
    free(r->imgs);
    if (r->pool) vkDestroyCommandPool(r->dev, r->pool, 0);
    if (r->swap) vkDestroySwapchainKHR(r->dev, r->swap, 0);
    vkDestroyDevice(r->dev, 0);
    if (r->surf) vkDestroySurfaceKHR(r->inst, r->surf, 0);
    vkDestroyInstance(r->inst, 0);
    memset(r, 0, sizeof *r);
}
