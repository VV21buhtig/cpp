// Ядро Vulkan (растр): RenderCore API, caps=VOXEL. Минимал без валидационных
// слоёв: instance/surface/device/swapchain(FIFO)/renderpass, 2 кадра в полёте.
// Меши — те же greedy (vox_mesh), вершина 12 float 1:1 с GL-ядром.
// Winding как в GL: viewport с отрицательной высотой + CCW (NDC-Y зеркалит).
// Шейдеры shaders/vk/*.vert/.frag -> SPIR-V на сборке (glslc, без копий).
// Заголовки и libvulkan системные; верх — ../vulcan-render БЕЗ копий.
#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "core/render_api.h"
#include "core/mat4.h"
#include "core/vk_conv.h" // все GL/VK-противоречия — там, см. шапку
#include "sdf_math.h"
#include "vox/vox_mesh.h"
#include "vox/vox_tex.h"

#ifndef VK_SPV_DIR
#define VK_SPV_DIR "spv_vk"
#endif

#define VK_FIF 2       // кадров в полёте
#define VK_MESH_SLOTS 256

#define VKCHK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    fprintf(stderr, "vk fail %d (%s:%d)\n", _r, __FILE__, __LINE__); return 0; } } while (0)

typedef struct {
    int cx, cz, used;
    VkBuffer buf;
    VkDeviceMemory mem;
    int count;
} VKMesh;

// Frame UBO std140 — зеркало struct Frame в shaders/vk/vk_mesh.*.
typedef struct {
    float viewProj[16];
    float sunDir[4];
    float ambSky[4];
    float ambGnd[4];
    float fogColor[4]; // rgb + gamma
    float fogSat[4];   // near far sat time
    float viewPos[4];
    float sunDiff[4];
    float sunSpec[4]; // rgb + shininess
} VkFrameUbo;

typedef struct {
    GLFWwindow *win;
    VkInstance inst;
    VkDebugUtilsMessengerEXT dbgMsgr;
    VkSurfaceKHR surf;
    VkPhysicalDevice pdev;
    VkDevice dev;
    VkQueue q;
    uint32_t qfam;
    VkSwapchainKHR swap;
    VkImage *swapImgs;
    uint32_t swapN;
    VkFormat swapFmt;
    VkExtent2D swapExt;
    VkImageView *swapViews;
    VkImage depthImg;
    VkDeviceMemory depthMem;
    VkImageView depthView;
    VkRenderPass rp;
    VkFramebuffer *fbs;
    VkDescriptorSetLayout setLayout;
    VkPipelineLayout meshPipeLayout, skyPipeLayout;
    VkPipeline meshPipe, skyPipe;
    VkDescriptorPool pool;
    VkDescriptorSet sets[VK_FIF];
    VkBuffer uboBuf[VK_FIF];
    VkDeviceMemory uboMem[VK_FIF];
    void *uboPtr[VK_FIF];
    // (SDF-объём снят: см. шейдер. Бейк vox_sdf живёт для будущего DFAO.)
    VkImage tileImg;
    VkDeviceMemory tileMem;
    VkImageView tileView;
    VkSampler tileSmp;
    VkImage specImg;
    VkDeviceMemory specMem;
    VkImageView specView;
    VkSampler specSmp;
    VkCommandPool cmdPool;
    VkCommandBuffer cmds[VK_FIF];
    VkSemaphore imgAvail[VK_FIF], renderDone[VK_FIF];
    VkFence frameFence[VK_FIF];
    VKMesh meshes[VK_MESH_SLOTS];
    // Ридбэк кадра (VK_SHOT): буфер под swap-экстент, разово на свопчейн.
    VkBuffer shotBuf;
    VkDeviceMemory shotMem;
    void *shotPtr;
    int ready;
    int frame;
    double prevT;
    float fpsEma;
} VKCore;

typedef struct {
    RenderCore api;
    VKCore core;
} VKCoreWrap;

static double vk_time(void) {
    return glfwGetTime();
}

static int vk_once(VKCore *c, VkCommandBuffer *outCb);
static int vk_once_end(VKCore *c, VkCommandBuffer cb);

static VkBool32 vk_dbg_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                           VkDebugUtilsMessageTypeFlagsEXT type,
                           const VkDebugUtilsMessengerCallbackDataEXT *d, void *user) {
    (void)sev; (void)type; (void)user;
    fprintf(stderr, "VK-VALID: %s\n", d->pMessage ? d->pMessage : "?");
    return VK_FALSE;
}

static uint32_t find_mem(VkPhysicalDevice pd, uint32_t mask, VkMemoryPropertyFlags fl) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mask & (1u << i)) && (mp.memoryTypes[i].propertyFlags & fl) == fl)
            return i;
    return 0xFFFFFFFFu;
}

static char *read_bin(const char *path, size_t *outN) {    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "vk: no file %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = (char *)malloc((size_t)n);
    if (!s || fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return 0; }
    fclose(f);
    *outN = (size_t)n;
    return s;
}

static VkShaderModule vk_shader(VKCore *c, const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", VK_SPV_DIR, name);
    size_t n = 0;
    char *code = read_bin(path, &n);
    if (!code) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = n;
    ci.pCode = (const uint32_t *)code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(c->dev, &ci, 0, &m) != VK_SUCCESS)
        fprintf(stderr, "vk: bad module %s\n", path);
    free(code);
    return m;
}

// Хост-видимый буфер (вершины/UBO/стейджинг/ридбэк). Память замаплена сразу.
static int vk_hbuf(VKCore *c, VkDeviceSize sz, VkBufferUsageFlags use,
                   VkBuffer *b, VkDeviceMemory *m, void **ptr) {
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sz;
    bi.usage = use;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(c->dev, &bi, 0, b) != VK_SUCCESS) return 0;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c->dev, *b, &mr);
    uint32_t mi = find_mem(c->pdev, mr.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mi == 0xFFFFFFFFu) return 0;
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mi;
    if (vkAllocateMemory(c->dev, &ai, 0, m) != VK_SUCCESS) return 0;
    if (vkBindBufferMemory(c->dev, *b, *m, 0) != VK_SUCCESS) return 0;
    if (ptr && vkMapMemory(c->dev, *m, 0, mr.size, 0, ptr) != VK_SUCCESS) return 0;
    return 1;
}

static float core_fps(RenderCore *rc) {
    return ((VKCoreWrap *)rc->ctx)->core.fpsEma;
}

static int core_upload_chunk(RenderCore *rc, int cx, int cz, const uint8_t *vox16,
                             const VoxChunk *nb[6]) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!c->ready) return 0;
    VoxChunk tmp;
    tmp.cx = cx;
    tmp.cz = cz;
    memcpy(tmp.id, vox16, VOX_N);
    VoxMeshOut m = {0, 0, 0};
    vox_mesh_build(&tmp, nb, &m);
    int slot = -1;
    for (int i = 0; i < VK_MESH_SLOTS; i++)
        if (c->meshes[i].used && c->meshes[i].cx == cx && c->meshes[i].cz == cz) { slot = i; break; }
    if (slot < 0)
        for (int i = 0; i < VK_MESH_SLOTS; i++)
            if (!c->meshes[i].used) { slot = i; break; }
    if (slot < 0) {
        unsigned h = (unsigned)(cx * 73856093 ^ cz * 19349663);
        slot = (int)(h % VK_MESH_SLOTS);
    }
    VKMesh *g = &c->meshes[slot];
    if (g->used) {
        // Слот в полёте (2 кадра): сносим только после простоя очереди,
        // иначе VUID-vkDestroyBuffer-00922. Замены редки, простой дешёвый.
        vkQueueWaitIdle(c->q);
        vkDestroyBuffer(c->dev, g->buf, 0);
        vkFreeMemory(c->dev, g->mem, 0);
        g->used = 0;
        g->count = 0;
    }
    if (!m.n) return 1;
    void *ptr = 0;
    if (!vk_hbuf(c, (VkDeviceSize)(m.n * sizeof(float)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 &g->buf, &g->mem, &ptr)) {
        vox_mesh_free(&m);
        return 0;
    }
    memcpy(ptr, m.v, m.n * sizeof(float));
    g->cx = cx;
    g->cz = cz;
    g->count = (int)(m.n / 12);
    g->used = 1;
    vox_mesh_free(&m);
    return 1;
}

static void core_set_origin(RenderCore *rc, int ox, int oz) {
    (void)rc; (void)ox; (void)oz;
}

static void core_unload_chunk(RenderCore *rc, int cx, int cz) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    for (int i = 0; i < VK_MESH_SLOTS; i++) {
        if (!c->meshes[i].used || c->meshes[i].cx != cx || c->meshes[i].cz != cz) continue;
        vkQueueWaitIdle(c->q); // как в upload: слот может быть в полёте
        vkDestroyBuffer(c->dev, c->meshes[i].buf, 0);
        vkFreeMemory(c->dev, c->meshes[i].mem, 0);
        c->meshes[i].used = 0;
        c->meshes[i].count = 0;
        break;
    }
}

// Разовая команда (аплоад текстур на ините, не во фрейме).
static int vk_once(VKCore *c, VkCommandBuffer *outCb) {
    VkCommandBufferAllocateInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = c->cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VKCHK(vkAllocateCommandBuffers(c->dev, &ai, outCb));
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKCHK(vkBeginCommandBuffer(*outCb, &bi));
    return 1;
}

static int vk_once_end(VKCore *c, VkCommandBuffer cb) {
    VKCHK(vkEndCommandBuffer(cb));
    VkSubmitInfo si;
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence f = VK_NULL_HANDLE;
    VkFenceCreateInfo fi;
    memset(&fi, 0, sizeof fi);
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VKCHK(vkCreateFence(c->dev, &fi, 0, &f));
    VKCHK(vkQueueSubmit(c->q, 1, &si, f));
    VKCHK(vkWaitForFences(c->dev, 1, &f, 1, 1000000000ull));
    vkDestroyFence(c->dev, f, 0);
    vkFreeCommandBuffers(c->dev, c->cmdPool, 1, &cb);
    return 1;
}

static int core_init2(RenderCore *rc);
static int core_init3(RenderCore *rc);

static int core_init(RenderCore *rc, void *glfwWindow) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    memset(c, 0, sizeof *c);
    c->win = (GLFWwindow *)glfwWindow;

    // Доказательство что это правда Vulkan: VOX_VK_VALIDATE=1 цепляет
    // VK_LAYER_KHRONOS_validation + мессенджер в stderr. GL-ядро на эту
    // переменную никак не реагирует (у него нет vkCreateInstance).
    int wantValid = getenv("VOX_VK_VALIDATE") && getenv("VOX_VK_VALIDATE")[0] == '1';
    const char *wantLayer = "VK_LAYER_KHRONOS_validation";
    int haveLayer = 0;
    if (wantValid) {
        uint32_t ln = 0;
        vkEnumerateInstanceLayerProperties(&ln, 0);
        VkLayerProperties *lp = (VkLayerProperties *)calloc(ln ? ln : 1, sizeof *lp);
        if (lp) {
            vkEnumerateInstanceLayerProperties(&ln, lp);
            for (uint32_t i = 0; i < ln; i++)
                if (!strcmp(lp[i].layerName, wantLayer)) haveLayer = 1;
            free(lp);
        }
        printf("vk: validate %s (layer %s)\n",
               haveLayer ? "ON" : "wanted but MISSING", wantLayer);
    }

    VkApplicationInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "voxels";
    // 1.1: отрицательная высота viewport (наш Y-флип) легализована в
    // maintenance1; в чистом 1.0 валидация ругается VUID-VkViewport-07917.
    ai.apiVersion = VK_API_VERSION_1_1;
    uint32_t extN = 0;
    const char **exts = glfwGetRequiredInstanceExtensions(&extN);
    const char *allExt[16];
    uint32_t allN = 0;
    for (uint32_t i = 0; i < extN && allN < 16; i++) allExt[allN++] = exts[i];
    if (haveLayer && allN < 16) allExt[allN++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkInstanceCreateInfo ii;
    memset(&ii, 0, sizeof ii);
    ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ii.pApplicationInfo = &ai;
    ii.enabledExtensionCount = allN;
    ii.ppEnabledExtensionNames = allExt;
    if (haveLayer) {
        ii.enabledLayerCount = 1;
        ii.ppEnabledLayerNames = &wantLayer;
    }
    VKCHK(vkCreateInstance(&ii, 0, &c->inst));
    if (haveLayer) {
        PFN_vkCreateDebugUtilsMessengerEXT mk =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(c->inst, "vkCreateDebugUtilsMessengerEXT");
        if (mk) {
            VkDebugUtilsMessengerCreateInfoEXT mi;
            memset(&mi, 0, sizeof mi);
            mi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = vk_dbg_cb;
            mi.pUserData = 0;
            if (mk(c->inst, &mi, 0, &c->dbgMsgr) != VK_SUCCESS)
                fprintf(stderr, "vk: messenger fail\n");
        }
    }
    VKCHK(glfwCreateWindowSurface(c->inst, c->win, 0, &c->surf));

    // Физическое: дискрет first, очередь graphics+present в одном family.
    uint32_t pdN = 0;
    VKCHK(vkEnumeratePhysicalDevices(c->inst, &pdN, 0));
    if (!pdN) { fprintf(stderr, "vk: no devices\n"); return 0; }
    VkPhysicalDevice pds[8];
    if (pdN > 8) pdN = 8;
    VKCHK(vkEnumeratePhysicalDevices(c->inst, &pdN, pds));
    uint32_t qfam = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < pdN && qfam == 0xFFFFFFFFu; i++) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &qn, 0);
        VkQueueFamilyProperties qp[16];
        if (qn > 16) qn = 16;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &qn, qp);
        for (uint32_t q = 0; q < qn; q++) {
            VkBool32 pres = 0;
            vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], q, c->surf, &pres);
            if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
                c->pdev = pds[i];
                qfam = q;
                break;
            }
        }
    }
    if (qfam == 0xFFFFFFFFu) { fprintf(stderr, "vk: no gfx+present queue\n"); return 0; }
    c->qfam = qfam;
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(c->pdev, &pp);
    printf("vk: %s api=%u.%u driver=0x%x\n", pp.deviceName,
           VK_VERSION_MAJOR(pp.apiVersion), VK_VERSION_MINOR(pp.apiVersion),
           pp.driverVersion);
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
    VKCHK(vkCreateDevice(c->pdev, &di, 0, &c->dev));
    vkGetDeviceQueue(c->dev, qfam, 0, &c->q);

    // Свопчейн: FIFO (vsync 60 как везде), B8G8R8A8_UNORM.
    VkSurfaceCapabilitiesKHR caps;
    VKCHK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->pdev, c->surf, &caps));
    uint32_t fn = 0;
    VKCHK(vkGetPhysicalDeviceSurfaceFormatsKHR(c->pdev, c->surf, &fn, 0));
    if (!fn) { fprintf(stderr, "vk: no surface formats\n"); return 0; }
    VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR *)calloc(fn, sizeof *fmts);
    if (!fmts) return 0;
    VkResult fr = vkGetPhysicalDeviceSurfaceFormatsKHR(c->pdev, c->surf, &fn, fmts);
    if (fr != VK_SUCCESS && fr != VK_INCOMPLETE) {
        fprintf(stderr, "vk fail %d (%s:%d)\n", fr, __FILE__, __LINE__);
        free(fmts);
        return 0;
    }
    VkSurfaceFormatKHR fmt = fmts[0];
    for (uint32_t i = 0; i < fn; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { fmt = fmts[i]; break; }
    free(fmts);
    c->swapFmt = fmt.format;
    c->swapExt.width = 1280;
    c->swapExt.height = 720;
    if (caps.currentExtent.width != 0xFFFFFFFFu) c->swapExt = caps.currentExtent;
    uint32_t imgN = caps.minImageCount + 1;
    if (caps.maxImageCount && imgN > caps.maxImageCount) imgN = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sw;
    memset(&sw, 0, sizeof sw);
    sw.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sw.surface = c->surf;
    sw.minImageCount = imgN;
    sw.imageFormat = c->swapFmt;
    sw.imageColorSpace = fmt.colorSpace;
    sw.imageExtent = c->swapExt;
    sw.imageArrayLayers = 1;
    sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sw.preTransform = caps.currentTransform;
    sw.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sw.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sw.clipped = VK_TRUE;
    VKCHK(vkCreateSwapchainKHR(c->dev, &sw, 0, &c->swap));
    VKCHK(vkGetSwapchainImagesKHR(c->dev, c->swap, &c->swapN, 0));
    c->swapImgs = (VkImage *)calloc(c->swapN, sizeof(VkImage));
    c->swapViews = (VkImageView *)calloc(c->swapN, sizeof(VkImageView));
    c->fbs = (VkFramebuffer *)calloc(c->swapN, sizeof(VkFramebuffer));
    if (!c->swapImgs || !c->swapViews || !c->fbs) return 0;
    VKCHK(vkGetSwapchainImagesKHR(c->dev, c->swap, &c->swapN, c->swapImgs));
    for (uint32_t i = 0; i < c->swapN; i++) {
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->swapImgs[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = c->swapFmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VKCHK(vkCreateImageView(c->dev, &vi, 0, &c->swapViews[i]));
    }

    // Глубина: первый поддерживаемый.
    VkFormat dFmt = VK_FORMAT_UNDEFINED;
    VkFormat cand[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
    for (int i = 0; i < 3 && dFmt == VK_FORMAT_UNDEFINED; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(c->pdev, cand[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            dFmt = cand[i];
    }
    if (dFmt == VK_FORMAT_UNDEFINED) { fprintf(stderr, "vk: no depth format\n"); return 0; }
    {
        VkImageCreateInfo ici;
        memset(&ici, 0, sizeof ici);
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = dFmt;
        ici.extent.width = c->swapExt.width;
        ici.extent.height = c->swapExt.height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VKCHK(vkCreateImage(c->dev, &ici, 0, &c->depthImg));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->dev, c->depthImg, &mr);
        uint32_t mi = find_mem(c->pdev, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) return 0;
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        VKCHK(vkAllocateMemory(c->dev, &mai, 0, &c->depthMem));
        VKCHK(vkBindImageMemory(c->dev, c->depthImg, c->depthMem, 0));
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->depthImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = dFmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VKCHK(vkCreateImageView(c->dev, &vi, 0, &c->depthView));
    }

    // Проход: цвет CLEAR->PRESENT, глубина CLEAR.
    {
        VkAttachmentDescription at[2];
        memset(at, 0, sizeof at);
        at[0].format = c->swapFmt;
        at[0].samples = VK_SAMPLE_COUNT_1_BIT;
        at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        at[1].format = dFmt;
        at[1].samples = VK_SAMPLE_COUNT_1_BIT;
        at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ca, da;
        ca.attachment = 0;
        ca.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        da.attachment = 1;
        da.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkSubpassDescription sp;
        memset(&sp, 0, sizeof sp);
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &ca;
        sp.pDepthStencilAttachment = &da;
        VkRenderPassCreateInfo rpi;
        memset(&rpi, 0, sizeof rpi);
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 2;
        rpi.pAttachments = at;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &sp;
        VKCHK(vkCreateRenderPass(c->dev, &rpi, 0, &c->rp));
        for (uint32_t i = 0; i < c->swapN; i++) {
            VkImageView av[2] = {c->swapViews[i], c->depthView};
            VkFramebufferCreateInfo fi;
            memset(&fi, 0, sizeof fi);
            fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fi.renderPass = c->rp;
            fi.attachmentCount = 2;
            fi.pAttachments = av;
            fi.width = c->swapExt.width;
            fi.height = c->swapExt.height;
            fi.layers = 1;
            VKCHK(vkCreateFramebuffer(c->dev, &fi, 0, &c->fbs[i]));
        }
    }

    // Сет: 0=UBO, 1=атлас, 2=спек.
    {
        VkDescriptorSetLayoutBinding b[3];
        memset(b, 0, sizeof b);
        b[0].binding = 0;
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b[0].descriptorCount = 1;
        b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1;
        b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[1].descriptorCount = 1;
        b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[2].binding = 2;
        b[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[2].descriptorCount = 1;
        b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li;
        memset(&li, 0, sizeof li);
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3;
        li.pBindings = b;
        VKCHK(vkCreateDescriptorSetLayout(c->dev, &li, 0, &c->setLayout));
    }
    if (!core_init2(rc)) return 0;
    if (!core_init3(rc)) return 0;
    c->prevT = vk_time();
    c->ready = 1;
    return 1;
}

static void core_frame(RenderCore *rc, const RcView *v);
static int core_init(RenderCore *rc, void *glfwWindow);
static void core_shutdown(RenderCore *rc);

RenderCore *rc_vk_create(void) {
    VKCoreWrap *w = (VKCoreWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_VOXEL; // растр мешей по вокселям
    w->api.name = "vk";
    w->api.init = core_init;
    w->api.shutdown = core_shutdown;
    w->api.frame = core_frame;
    w->api.upload_chunk = core_upload_chunk;
    w->api.set_origin = core_set_origin;
    w->api.unload_chunk = core_unload_chunk;
    w->api.fps = core_fps;
    return &w->api;
}

// --- init, часть 2: UBO/сеты/текстуры/пайплайны/синх (вызывается из core_init) ---
static int core_init2(RenderCore *rc) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    // UBO на кадр в полёте + пул/сеты.
    for (int i = 0; i < VK_FIF; i++)
        if (!vk_hbuf(c, sizeof(VkFrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     &c->uboBuf[i], &c->uboMem[i], &c->uboPtr[i])) return 0;
    {
        VkDescriptorPoolSize ps[2];
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps[0].descriptorCount = VK_FIF;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[1].descriptorCount = VK_FIF * 2;
        VkDescriptorPoolCreateInfo pi;
        memset(&pi, 0, sizeof pi);
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = VK_FIF;
        pi.poolSizeCount = 2;
        pi.pPoolSizes = ps;
        VKCHK(vkCreateDescriptorPool(c->dev, &pi, 0, &c->pool));
        VkDescriptorSetLayout ls[VK_FIF] = {c->setLayout, c->setLayout};
        VkDescriptorSetAllocateInfo ai;
        memset(&ai, 0, sizeof ai);
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = c->pool;
        ai.descriptorSetCount = VK_FIF;
        ai.pSetLayouts = ls;
        VKCHK(vkAllocateDescriptorSets(c->dev, &ai, c->sets));
    }
    // Пул команд + буферы + семафоры/заборы.
    {
        VkCommandPoolCreateInfo pi;
        memset(&pi, 0, sizeof pi);
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.queueFamilyIndex = c->qfam;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VKCHK(vkCreateCommandPool(c->dev, &pi, 0, &c->cmdPool));
        VkCommandBufferAllocateInfo ai;
        memset(&ai, 0, sizeof ai);
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = c->cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = VK_FIF;
        VKCHK(vkAllocateCommandBuffers(c->dev, &ai, c->cmds));
        for (int i = 0; i < VK_FIF; i++) {
            VkSemaphoreCreateInfo si;
            memset(&si, 0, sizeof si);
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VKCHK(vkCreateSemaphore(c->dev, &si, 0, &c->imgAvail[i]));
            VKCHK(vkCreateSemaphore(c->dev, &si, 0, &c->renderDone[i]));
            VkFenceCreateInfo fi;
            memset(&fi, 0, sizeof fi);
            fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VKCHK(vkCreateFence(c->dev, &fi, 0, &c->frameFence[i]));
        }
    }
    // Атлас 16x16x9 + тинты (те же что в GL-ядре) и белый спек.
    static uint8_t tiles[VOX_TEXELS * 4];
    if (!vox_tex_load("../voxel-render/texture/tiles", tiles)) {
        memset(tiles, 0x80, sizeof tiles);
        printf("tiles: fallback gray\n");
    }
    for (int i = 0; i < VOX_TILE * VOX_TILE; i++) {
        uint8_t *t0 = &tiles[i * 4];
        t0[0] = (uint8_t)(t0[0] * 145 / 255);
        t0[1] = (uint8_t)(t0[1] * 189 / 255);
        t0[2] = (uint8_t)(t0[2] * 89 / 255);
        uint8_t *t6 = &tiles[(6 * VOX_TILE * VOX_TILE + i) * 4];
        t6[0] = (uint8_t)(t6[0] * 119 / 255);
        t6[1] = (uint8_t)(t6[1] * 171 / 255);
        t6[2] = (uint8_t)(t6[2] * 47 / 255);
    }
    // Картинка + стейджинг (разово, на ините).
    {
        VkImageCreateInfo ici;
        memset(&ici, 0, sizeof ici);
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent.width = VOX_TILE;
        ici.extent.height = VOX_TILE;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = VOX_LAYERS;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VKCHK(vkCreateImage(c->dev, &ici, 0, &c->tileImg));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->dev, c->tileImg, &mr);
        uint32_t mi = find_mem(c->pdev, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) return 0;
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        VKCHK(vkAllocateMemory(c->dev, &mai, 0, &c->tileMem));
        VKCHK(vkBindImageMemory(c->dev, c->tileImg, c->tileMem, 0));
        VkBuffer stg = VK_NULL_HANDLE;
        VkDeviceMemory stgM = VK_NULL_HANDLE;
        void *sp = 0;
        if (!vk_hbuf(c, sizeof tiles, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stg, &stgM, &sp)) return 0;
        memcpy(sp, tiles, sizeof tiles);
        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (!vk_once(c, &cb)) return 0;
        VkImageMemoryBarrier b0;
        memset(&b0, 0, sizeof b0);
        b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b0.image = c->tileImg;
        b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b0.subresourceRange.levelCount = 1;
        b0.subresourceRange.layerCount = VOX_LAYERS;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b0);
        VkBufferImageCopy cp;
        memset(&cp, 0, sizeof cp);
        cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cp.imageSubresource.layerCount = VOX_LAYERS;
        cp.imageExtent.width = VOX_TILE;
        cp.imageExtent.height = VOX_TILE;
        cp.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cb, stg, c->tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        VkImageMemoryBarrier b1 = b0;
        b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &b1);
        if (!vk_once_end(c, cb)) return 0;
        vkDestroyBuffer(c->dev, stg, 0);
        vkFreeMemory(c->dev, stgM, 0);
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->tileImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = VOX_LAYERS;
        VKCHK(vkCreateImageView(c->dev, &vi, 0, &c->tileView));
        VkSamplerCreateInfo sm;
        memset(&sm, 0, sizeof sm);
        sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sm.magFilter = VK_FILTER_NEAREST;
        sm.minFilter = VK_FILTER_NEAREST;
        sm.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sm.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sm.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VKCHK(vkCreateSampler(c->dev, &sm, 0, &c->tileSmp));
    }
    // Белый спек 1x1 (тот же layout-путь, короче: хост-картинка напрямую
    // не выйдет — optimal нужен; делаем через тот же стейджинг-приём).
    {
        VkImageCreateInfo ici;
        memset(&ici, 0, sizeof ici);
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent.width = 1;
        ici.extent.height = 1;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VKCHK(vkCreateImage(c->dev, &ici, 0, &c->specImg));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->dev, c->specImg, &mr);
        uint32_t mi = find_mem(c->pdev, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) return 0;
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        VKCHK(vkAllocateMemory(c->dev, &mai, 0, &c->specMem));
        VKCHK(vkBindImageMemory(c->dev, c->specImg, c->specMem, 0));
        VkBuffer stg = VK_NULL_HANDLE;
        VkDeviceMemory stgM = VK_NULL_HANDLE;
        void *sp = 0;
        if (!vk_hbuf(c, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stg, &stgM, &sp)) return 0;
        ((uint8_t *)sp)[0] = ((uint8_t *)sp)[1] = ((uint8_t *)sp)[2] = ((uint8_t *)sp)[3] = 255;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (!vk_once(c, &cb)) return 0;
        VkImageMemoryBarrier b0;
        memset(&b0, 0, sizeof b0);
        b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b0.image = c->specImg;
        b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b0.subresourceRange.levelCount = 1;
        b0.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b0);
        VkBufferImageCopy cp;
        memset(&cp, 0, sizeof cp);
        cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cp.imageSubresource.layerCount = 1;
        cp.imageExtent.width = 1;
        cp.imageExtent.height = 1;
        cp.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cb, stg, c->specImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        VkImageMemoryBarrier b1 = b0;
        b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &b1);
        if (!vk_once_end(c, cb)) return 0;
        vkDestroyBuffer(c->dev, stg, 0);
        vkFreeMemory(c->dev, stgM, 0);
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->specImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VKCHK(vkCreateImageView(c->dev, &vi, 0, &c->specView));
        VkSamplerCreateInfo sm;
        memset(&sm, 0, sizeof sm);
        sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sm.magFilter = VK_FILTER_NEAREST;
        sm.minFilter = VK_FILTER_NEAREST;
        VKCHK(vkCreateSampler(c->dev, &sm, 0, &c->specSmp));
    }
    // Сеты: UBO кадра + общие картинки.
    for (int i = 0; i < VK_FIF; i++) {
        VkDescriptorBufferInfo bi;
        bi.buffer = c->uboBuf[i];
        bi.offset = 0;
        bi.range = sizeof(VkFrameUbo);
        VkDescriptorImageInfo ii0, ii1;
        ii0.sampler = c->tileSmp;
        ii0.imageView = c->tileView;
        ii0.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ii1.sampler = c->specSmp;
        ii1.imageView = c->specView;
        ii1.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w[3];
        memset(w, 0, sizeof w);
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = c->sets[i];
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &bi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = c->sets[i];
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &ii0;
        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2].dstSet = c->sets[i];
        w[2].dstBinding = 2;
        w[2].descriptorCount = 1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].pImageInfo = &ii1;
        vkUpdateDescriptorSets(c->dev, 3, w, 0, 0);
    }
    return 1;
}

// --- init, часть 3: пайплайны меш/небо ---
static int core_init3(RenderCore *rc) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    VkShaderModule mVert = vk_shader(c, "vk_mesh.vert.spv");
    VkShaderModule mFrag = vk_shader(c, "vk_mesh.frag.spv");
    VkShaderModule sVert = vk_shader(c, "vk_sky.vert.spv");
    VkShaderModule sFrag = vk_shader(c, "vk_sky.frag.spv");
    if (!mVert || !mFrag || !sVert || !sFrag) return 0;
    // Вершина 12 float, едим 0..4 (day/night 5,6 мертвы как у них — флуд удалён).
    VkVertexInputBindingDescription bind;
    memset(&bind, 0, sizeof bind);
    bind.binding = 0;
    bind.stride = 12 * sizeof(float);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription at[5];
    memset(at, 0, sizeof at);
    uint32_t off[5] = {0, 3, 6, 8, 9};
    VkFormat fm[5] = {VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT,
                      VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SFLOAT};
    for (int i = 0; i < 5; i++) {
        at[i].location = (uint32_t)i;
        at[i].binding = 0;
        at[i].format = fm[i];
        at[i].offset = off[i] * sizeof(float);
    }
    VkPipelineVertexInputStateCreateInfo vin;
    memset(&vin, 0, sizeof vin);
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 5;
    vin.pVertexAttributeDescriptions = at;
    VkPipelineVertexInputStateCreateInfo vinEmpty;
    memset(&vinEmpty, 0, sizeof vinEmpty);
    vinEmpty.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo asmbl;
    memset(&asmbl, 0, sizeof asmbl);
    asmbl.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    asmbl.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp = vk_viewport_fill(c->swapExt.width, c->swapExt.height);
    VkRect2D sc = vk_scissor_fill(c->swapExt.width, c->swapExt.height);
    VkPipelineViewportStateCreateInfo vps;
    memset(&vps, 0, sizeof vps);
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof rs);
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof ms);
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof ds);
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineDepthStencilStateCreateInfo dsOff;
    memset(&dsOff, 0, sizeof dsOff);
    dsOff.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState ba;
    memset(&ba, 0, sizeof ba);
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof cb);
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyns;
    memset(&dyns, 0, sizeof dyns);
    dyns.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyns.dynamicStateCount = 2;
    dyns.pDynamicStates = dyn;
    // Layouts: меш — сет + push model(64); небо — только push(112).
    {
        VkPushConstantRange pr;
        pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pr.offset = 0;
        pr.size = 64;
        VkPipelineLayoutCreateInfo li;
        memset(&li, 0, sizeof li);
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = 1;
        li.pSetLayouts = &c->setLayout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pr;
        VKCHK(vkCreatePipelineLayout(c->dev, &li, 0, &c->meshPipeLayout));
    }
    {
        VkPushConstantRange pr;
        pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pr.offset = 0;
        pr.size = 112;
        VkPipelineLayoutCreateInfo li;
        memset(&li, 0, sizeof li);
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pr;
        VKCHK(vkCreatePipelineLayout(c->dev, &li, 0, &c->skyPipeLayout));
    }
    VkPipelineShaderStageCreateInfo st[2];
    memset(st, 0, sizeof st);
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = mVert;
    st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = mFrag;
    st[1].pName = "main";
    VkGraphicsPipelineCreateInfo pi;
    memset(&pi, 0, sizeof pi);
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vin;
    pi.pInputAssemblyState = &asmbl;
    pi.pViewportState = &vps;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dyns;
    pi.layout = c->meshPipeLayout;
    pi.renderPass = c->rp;
    VKCHK(vkCreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &pi, 0, &c->meshPipe));
    st[0].module = sVert;
    st[1].module = sFrag;
    pi.pVertexInputState = &vinEmpty;
    pi.pDepthStencilState = &dsOff;
    pi.layout = c->skyPipeLayout;
    VKCHK(vkCreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &pi, 0, &c->skyPipe));
    vkDestroyShaderModule(c->dev, mVert, 0);
    vkDestroyShaderModule(c->dev, mFrag, 0);
    vkDestroyShaderModule(c->dev, sVert, 0);
    vkDestroyShaderModule(c->dev, sFrag, 0);
    // Ридбэк-буфер под своп-экстент (VK_SHOT), разово.
    if (!vk_hbuf(c, (VkDeviceSize)(c->swapExt.width * c->swapExt.height * 4),
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 &c->shotBuf, &c->shotMem, &c->shotPtr)) return 0;
    printf("vk core OK: mesh+sky, %u swap, fmt=%d\n", c->swapN, c->swapFmt);
    return 1;
}

static void core_frame(RenderCore *rc, const RcView *v) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    double now = vk_time();
    float dt = (float)(now - c->prevT);
    c->prevT = now;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        c->fpsEma = c->fpsEma > 0.0f ? c->fpsEma * 0.95f + fps * 0.05f : fps;
    }
    int ww = v->resW > 0 ? v->resW : 1280;
    int hh = v->resH > 0 ? v->resH : 720;
    (void)ww; (void)hh;
    int fi = c->frame % VK_FIF;
    vkWaitForFences(c->dev, 1, &c->frameFence[fi], 1, 1000000000ull);
    vkResetFences(c->dev, 1, &c->frameFence[fi]);
    uint32_t img = 0;
    VkResult ar = vkAcquireNextImageKHR(c->dev, c->swap, 1000000000ull,
                                        c->imgAvail[fi], VK_NULL_HANDLE, &img);
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) return;

    // UBO кадра: те же числа что в GL-ядре.
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    Mat4 proj = m4persp_vk(2.0f * atanf(0.5f / (v->fov > 0.2f ? v->fov : 1.6f)),
                           (float)c->swapExt.width / (float)c->swapExt.height, 0.1f, 600.0f);
    Mat4 view = m4look(v->camPos.x, v->camPos.y, v->camPos.z,
                       v->camPos.x + f.x, v->camPos.y + f.y, v->camPos.z + f.z);
    Mat4 pv = m4mul(&proj, &view);
    Mat4 inv = m4inv(&pv);
    float dayF = v->sunDir.y > 1.0f ? 1.0f : (v->sunDir.y < -1.0f ? -1.0f : v->sunDir.y);
    float nightF = dayF < 0.02f ? (dayF < -0.12f ? 1.0f : (0.02f - dayF) / 0.14f) : 0.0f;
    float amb = 0.20f * 3.0f;
    VkFrameUbo *ub = (VkFrameUbo *)c->uboPtr[fi];
    memcpy(ub->viewProj, pv.m, sizeof pv.m);
    ub->sunDir[0] = v->sunDir.x; ub->sunDir[1] = v->sunDir.y; ub->sunDir[2] = v->sunDir.z;
    ub->sunDir[3] = 0.0f;
    ub->ambSky[0] = amb * 0.9f; ub->ambSky[1] = amb; ub->ambSky[2] = amb * 1.15f; ub->ambSky[3] = 1.0f;
    ub->ambGnd[0] = amb * 0.45f; ub->ambGnd[1] = amb * 0.40f; ub->ambGnd[2] = amb * 0.35f;
    ub->ambGnd[3] = 1.0f;
    ub->fogColor[0] = 0.30f; ub->fogColor[1] = 0.36f; ub->fogColor[2] = 0.46f;
    ub->fogColor[3] = 1.2f; // gamma
    ub->fogSat[0] = 0.0f; ub->fogSat[1] = 260.0f; ub->fogSat[2] = 1.1f;
    ub->fogSat[3] = (float)now;
    ub->viewPos[0] = v->camPos.x; ub->viewPos[1] = v->camPos.y; ub->viewPos[2] = v->camPos.z;
    ub->viewPos[3] = 1.0f;
    ub->sunDiff[0] = 1.7f; ub->sunDiff[1] = 1.6f; ub->sunDiff[2] = 1.45f; ub->sunDiff[3] = 1.0f;
    ub->sunSpec[0] = 0.3f; ub->sunSpec[1] = 0.28f; ub->sunSpec[2] = 0.25f;
    ub->sunSpec[3] = 32.0f; // shininess

    VkCommandBuffer cb = c->cmds[fi];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    VkClearValue clr[2];
    clr[0].color.float32[0] = 0.05f;
    clr[0].color.float32[1] = 0.07f;
    clr[0].color.float32[2] = 0.12f;
    clr[0].color.float32[3] = 1.0f;
    clr[1].depthStencil.depth = 1.0f;
    clr[1].depthStencil.stencil = 0;
    VkRenderPassBeginInfo rp;
    memset(&rp, 0, sizeof rp);
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = c->rp;
    rp.framebuffer = c->fbs[img];
    rp.renderArea.extent = c->swapExt;
    rp.clearValueCount = 2;
    rp.pClearValues = clr;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = vk_viewport_fill(c->swapExt.width, c->swapExt.height);
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = vk_scissor_fill(c->swapExt.width, c->swapExt.height);
    vkCmdSetScissor(cb, 0, 1, &sc);
    // Небо (push 112: invVP + sun/night + top/resH + hor/resW).
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->skyPipe);
    {
        float push[28];
        memcpy(push, inv.m, 64);
        push[16] = v->sunDir.x; push[17] = v->sunDir.y; push[18] = v->sunDir.z;
        push[19] = nightF;
        push[20] = 0.16f; push[21] = 0.32f; push[22] = 0.58f;
        push[23] = (float)c->swapExt.height;
        push[24] = 0.55f; push[25] = 0.60f; push[26] = 0.68f;
        push[27] = (float)c->swapExt.width;
        vkCmdPushConstants(cb, c->skyPipeLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 112, push);
    }
    vkCmdDraw(cb, 3, 1, 0, 0);
    // Меши.
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->meshPipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->meshPipeLayout,
                            0, 1, &c->sets[fi], 0, 0);
    for (int i = 0; i < VK_MESH_SLOTS; i++) {
        if (!c->meshes[i].used) continue;
        Mat4 model = m4translate((float)(c->meshes[i].cx * 16), 0.0f,
                                 (float)(c->meshes[i].cz * 16));
        vkCmdPushConstants(cb, c->meshPipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, 64, model.m);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &c->meshes[i].buf, &off);
        vkCmdDraw(cb, (uint32_t)c->meshes[i].count, 1, 0, 0);
    }
    // Бейдж ядра: красный угол 64x32 слева внизу экрана. Строка 0 фреймбуфера
    // сверху (vk_conv.h п.3), поэтому rect.y = H-32. GL — зелёный, см. gl_core.
    {
        VkClearAttachment ca;
        memset(&ca, 0, sizeof ca);
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca.colorAttachment = 0;
        ca.clearValue.color.float32[0] = 1.0f;
        ca.clearValue.color.float32[1] = 0.0f;
        ca.clearValue.color.float32[2] = 0.0f;
        ca.clearValue.color.float32[3] = 1.0f;
        VkClearRect cr;
        cr.rect.offset.x = 0;
        cr.rect.offset.y = (int32_t)(c->swapExt.height - 32);
        cr.rect.extent.width = 64;
        cr.rect.extent.height = 32;
        cr.baseArrayLayer = 0;
        cr.layerCount = 1;
        vkCmdClearAttachments(cb, 1, &ca, 1, &cr);
    }
    vkCmdEndRenderPass(cb);
    // Ридбэк кадра: VK_SHOT=путь.ppm VK_SHOT_AT=кадр (дефолт 60).
    const char *sv = getenv("VK_SHOT");
    int shotArmed = sv && sv[0] && c->frame ==
        (getenv("VK_SHOT_AT") && getenv("VK_SHOT_AT")[0] ? atoi(getenv("VK_SHOT_AT")) : 60);
    if (shotArmed) {
        VkImageMemoryBarrier b0;
        memset(&b0, 0, sizeof b0);
        b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b0.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b0.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b0.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b0.image = c->swapImgs[img];
        b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b0.subresourceRange.levelCount = 1;
        b0.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b0);
        VkBufferImageCopy cp;
        memset(&cp, 0, sizeof cp);
        cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cp.imageSubresource.layerCount = 1;
        cp.imageExtent.width = c->swapExt.width;
        cp.imageExtent.height = c->swapExt.height;
        cp.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(cb, c->swapImgs[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               c->shotBuf, 1, &cp);
        VkImageMemoryBarrier b1 = b0;
        b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b1.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b1.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b1.dstAccessMask = 0;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &b1);
    }
    vkEndCommandBuffer(cb);
    VkPipelineStageFlags waitSt = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si;
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &c->imgAvail[fi];
    si.pWaitDstStageMask = &waitSt;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &c->renderDone[fi];
    vkQueueSubmit(c->q, 1, &si, c->frameFence[fi]);
    VkPresentInfoKHR pr;
    memset(&pr, 0, sizeof pr);
    pr.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pr.waitSemaphoreCount = 1;
    pr.pWaitSemaphores = &c->renderDone[fi];
    pr.swapchainCount = 1;
    pr.pSwapchains = &c->swap;
    pr.pImageIndices = &img;
    vkQueuePresentKHR(c->q, &pr);
    if (shotArmed) {
        vkWaitForFences(c->dev, 1, &c->frameFence[fi], 1, 1000000000ull);
        // BGRA -> PPM RGB, строки снизу вверх.
        FILE *f = fopen(sv, "wb");
        if (f) {
            uint32_t W = c->swapExt.width, H = c->swapExt.height;
            fprintf(f, "P6\n%u %u\n255\n", W, H);
            uint8_t *px = (uint8_t *)c->shotPtr;
            // п.3 из core/vk_conv.h: строка 0 = верх кадра, пишем сверху вниз.
            for (uint32_t y = 0; y < H; y++) {
                uint8_t *row = px + (size_t)y * W * 4;
                for (uint32_t x = 0; x < W; x++) {
                    uint8_t bgr[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]};
                    fwrite(bgr, 1, 3, f);
                }
            }
            fclose(f);
            fprintf(stderr, "vkshot: %s (%ux%u)\n", sv, W, H);
        }
    }
    c->frame++;
}

static void core_shutdown(RenderCore *rc) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!c->dev) return;
    vkDeviceWaitIdle(c->dev);
    for (int i = 0; i < VK_MESH_SLOTS; i++) {
        if (!c->meshes[i].used) continue;
        vkDestroyBuffer(c->dev, c->meshes[i].buf, 0);
        vkFreeMemory(c->dev, c->meshes[i].mem, 0);
    }
    if (c->shotBuf) vkDestroyBuffer(c->dev, c->shotBuf, 0);
    if (c->shotMem) vkFreeMemory(c->dev, c->shotMem, 0);
    for (int i = 0; i < VK_FIF; i++) {
        if (c->uboBuf[i]) vkDestroyBuffer(c->dev, c->uboBuf[i], 0);
        if (c->uboMem[i]) vkFreeMemory(c->dev, c->uboMem[i], 0);
        if (c->imgAvail[i]) vkDestroySemaphore(c->dev, c->imgAvail[i], 0);
        if (c->renderDone[i]) vkDestroySemaphore(c->dev, c->renderDone[i], 0);
        if (c->frameFence[i]) vkDestroyFence(c->dev, c->frameFence[i], 0);
    }
    if (c->tileSmp) vkDestroySampler(c->dev, c->tileSmp, 0);
    if (c->tileView) vkDestroyImageView(c->dev, c->tileView, 0);
    if (c->tileImg) vkDestroyImage(c->dev, c->tileImg, 0);
    if (c->tileMem) vkFreeMemory(c->dev, c->tileMem, 0);
    if (c->specSmp) vkDestroySampler(c->dev, c->specSmp, 0);
    if (c->specView) vkDestroyImageView(c->dev, c->specView, 0);
    if (c->specImg) vkDestroyImage(c->dev, c->specImg, 0);
    if (c->specMem) vkFreeMemory(c->dev, c->specMem, 0);
    if (c->meshPipe) vkDestroyPipeline(c->dev, c->meshPipe, 0);
    if (c->skyPipe) vkDestroyPipeline(c->dev, c->skyPipe, 0);
    if (c->meshPipeLayout) vkDestroyPipelineLayout(c->dev, c->meshPipeLayout, 0);
    if (c->skyPipeLayout) vkDestroyPipelineLayout(c->dev, c->skyPipeLayout, 0);
    if (c->pool) vkDestroyDescriptorPool(c->dev, c->pool, 0);
    if (c->setLayout) vkDestroyDescriptorSetLayout(c->dev, c->setLayout, 0);
    if (c->fbs) {
        for (uint32_t i = 0; i < c->swapN; i++)
            if (c->fbs[i]) vkDestroyFramebuffer(c->dev, c->fbs[i], 0);
        free(c->fbs);
    }
    if (c->rp) vkDestroyRenderPass(c->dev, c->rp, 0);
    if (c->depthView) vkDestroyImageView(c->dev, c->depthView, 0);
    if (c->depthImg) vkDestroyImage(c->dev, c->depthImg, 0);
    if (c->depthMem) vkFreeMemory(c->dev, c->depthMem, 0);
    if (c->swapViews) {
        for (uint32_t i = 0; i < c->swapN; i++)
            if (c->swapViews[i]) vkDestroyImageView(c->dev, c->swapViews[i], 0);
        free(c->swapViews);
    }
    free(c->swapImgs);
    if (c->cmdPool) vkDestroyCommandPool(c->dev, c->cmdPool, 0);
    if (c->swap) vkDestroySwapchainKHR(c->dev, c->swap, 0);
    vkDestroyDevice(c->dev, 0);
    if (c->dbgMsgr) {
        PFN_vkDestroyDebugUtilsMessengerEXT rm =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                c->inst, "vkDestroyDebugUtilsMessengerEXT");
        if (rm) rm(c->inst, c->dbgMsgr, 0);
    }
    if (c->surf) vkDestroySurfaceKHR(c->inst, c->surf, 0);
    vkDestroyInstance(c->inst, 0);
    memset(c, 0, sizeof *c);
}
