// Ядро Vulkan (растр): RenderCore API, caps=VOXEL. Четвёртый живой домен.
// Устройство/очередь/свопчейн — из RHI (rhi_vk); своё: renderpass, пайплайны,
// меши greedy, UBO. Без валидационных слоёв (RHI их не поднимает).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "rhi_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "render_api.h"
#include "rtarget.h"
#include "mat4.h"
#include "vk_conv.h" // все GL/VK-противоречия — там, см. шапку
#include "vec.h"
#include "vox/vox_tex.h"

#ifndef VK_SPV_DIR
#define VK_SPV_DIR "spv_vk"
#endif

#define VK_FIF 2       // кадров в полёте
#define VK_MESH_SLOTS 256

#define VKCHK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    fprintf(stderr, "vk fail %d (%s:%d)\n", _r, __FILE__, __LINE__); return 0; } } while (0)

typedef struct {
    int used;
    VkBuffer buf;
    VkDeviceMemory mem;
    int count;
    float model[16]; // матрица от фидера (чанков ядро не знает)
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
    RhiVk rhi; // устройство/очередь/свопчейн/синхронизация — отсюда
    VkImage depthImg;
    VkDeviceMemory depthMem;
    VkImageView depthView;
    VkFormat depthFmt; // запомненный формат глубины (для таргетов тоже он)
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
    int tileReady; // атлас залит (иначе белая заглушка из init)
    VkImage specImg;
    VkDeviceMemory specMem;
    VkImageView specView;
    VkSampler specSmp;
    VKMesh meshes[VK_MESH_SLOTS];
    // Ридбэк кадра (VK_SHOT): буфер под swap-экстент, разово на свопчейн.
    VkBuffer shotBuf;
    VkDeviceMemory shotMem;
    void *shotPtr;
    // Scratch-цель для frame_cpu (размер заказа, не свопа).
    // Нативный offscreen-таргет композитора (создаёт ядро, живёт в цели):
    // цвет + глубина + 2 прохода (clear/load) + фреймбуфер. Стейджинг чтения
    // свой (размер цели ≠ размер свопа), realloc по мере роста.
    VkBuffer grabBuf;
    VkDeviceMemory grabMem;
    void *grabPtr;
    VkDeviceSize grabCap;
    void *liveT[4]; // живые VKTarget* (смерть — в shutdown)
    int liveN;
    RcTarget scratch; // цель frame_cpu (натив в nobj[0], трекается в liveT)
    int ready;
    int frame;
    double prevT;
    float fpsEma;
} VKCore;

// Нативная цель Vulkan (лежит в RcTarget.nobj[0], владеет ядро).
typedef struct {
    int w, h;
    VkImage img;
    VkDeviceMemory mem;
    VkImageView view;
    VkImage dimg;
    VkDeviceMemory dmem;
    VkImageView dview;
    VkRenderPass rpClear; // цвет+глубина CLEAR (первый проход)
    VkRenderPass rpLoad;  // оба LOAD (поздние; глубина STORE — цепочка)
    VkFramebuffer fb;
} VKTarget;

typedef struct {
    RenderCore api;
    VKCore core;
} VKCoreWrap;

static double vk_time(void) {
    return glfwGetTime();
}

static int vk_once(VKCore *c, VkCommandBuffer *outCb);
static int vk_once_end(VKCore *c, VkCommandBuffer cb);

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
    if (vkCreateShaderModule(c->rhi.dev, &ci, 0, &m) != VK_SUCCESS)
        fprintf(stderr, "vk: bad module %s\n", path);
    free(code);
    return m;
}

// Хост-видимый буфер (вершины/UBO/стейджинг/ридбэк). Память замаплена сразу.
static int vk_hbuf(VKCore *c, VkDeviceSize sz, VkBufferUsageFlags use,
                   VkBuffer *b, VkDeviceMemory *m, void **ptr) {
    return rhi_vk_hbuf(&c->rhi, sz, use, b, m, ptr);
}

static float core_fps(RenderCore *rc) {
    return ((VKCoreWrap *)rc->ctx)->core.fpsEma;
}

static int core_upload_voxmesh(RenderCore *rc, int slot, const float *v12,
                               int nverts, const float *model) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!c->ready || slot < 0 || slot >= VK_MESH_SLOTS) return 0;
    VKMesh *g = &c->meshes[slot];
    if (g->used) {
        // Слот в полёте (2 кадра): сносим только после простоя очереди,
        // иначе VUID-vkDestroyBuffer-00922. Замены редки, простой дешёвый.
        vkQueueWaitIdle(c->rhi.q);
        vkDestroyBuffer(c->rhi.dev, g->buf, 0);
        vkFreeMemory(c->rhi.dev, g->mem, 0);
        g->used = 0;
        g->count = 0;
    }
    if (!v12 || nverts <= 0) return 1; // пусто — слот зачищен
    void *ptr = 0;
    size_t n = (size_t)nverts * 12 * sizeof(float);
    if (!vk_hbuf(c, (VkDeviceSize)n, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 &g->buf, &g->mem, &ptr))
        return 0;
    memcpy(ptr, v12, n);
    if (model) memcpy(g->model, model, sizeof g->model);
    else { Mat4 id = m4id(); memcpy(g->model, id.m, sizeof g->model); }
    g->count = nverts;
    g->used = 1;
    return 1;
}

static void core_unload_voxmesh(RenderCore *rc, int slot) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (slot < 0 || slot >= VK_MESH_SLOTS || !c->meshes[slot].used) return;
    vkQueueWaitIdle(c->rhi.q); // как в upload: слот может быть в полёте
    vkDestroyBuffer(c->rhi.dev, c->meshes[slot].buf, 0);
    vkFreeMemory(c->rhi.dev, c->meshes[slot].mem, 0);
    c->meshes[slot].used = 0;
    c->meshes[slot].count = 0;
}

// Разовая команда (аплоад текстур на ините, не во фрейме).
static int vk_once(VKCore *c, VkCommandBuffer *outCb) {
    VkCommandBufferAllocateInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = c->rhi.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VKCHK(vkAllocateCommandBuffers(c->rhi.dev, &ai, outCb));
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
    VKCHK(vkCreateFence(c->rhi.dev, &fi, 0, &f));
    VKCHK(vkQueueSubmit(c->rhi.q, 1, &si, f));
    VKCHK(vkWaitForFences(c->rhi.dev, 1, &f, 1, 1000000000ull));
    vkDestroyFence(c->rhi.dev, f, 0);
    vkFreeCommandBuffers(c->rhi.dev, c->rhi.pool, 1, &cb);
    return 1;
}

static int core_init2(RenderCore *rc);
static int core_init3(RenderCore *rc);

static int core_init(RenderCore *rc, void *glfwWindow) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    memset(c, 0, sizeof *c);
    c->win = (GLFWwindow *)glfwWindow;
    // Устройство/очередь/свопчейн/синхронизация — из RHI целиком.
    // Без валидационных слоёв (доказательство было в игре, здесь чистота RHI).
    if (!rhi_vk_init(&c->rhi, c->win, 0, 0)) return 0;
    printf("vk: %s\n", c->rhi.devName);
    c->fbs = (VkFramebuffer *)calloc(c->rhi.imgN, sizeof(VkFramebuffer));
    if (!c->fbs) return 0;

    // Глубина: первый поддерживаемый.
    VkFormat dFmt = VK_FORMAT_UNDEFINED;
    VkFormat cand[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
    for (int i = 0; i < 3 && dFmt == VK_FORMAT_UNDEFINED; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(c->rhi.pdev, cand[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            dFmt = cand[i];
    }
    if (dFmt == VK_FORMAT_UNDEFINED) { fprintf(stderr, "vk: no depth format\n"); return 0; }
    c->depthFmt = dFmt; // тот же формат — в offscreen-таргеты (совместимость проходов)
    {
        VkImageCreateInfo ici;
        memset(&ici, 0, sizeof ici);
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = dFmt;
        ici.extent.width = c->rhi.extent.width;
        ici.extent.height = c->rhi.extent.height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VKCHK(vkCreateImage(c->rhi.dev, &ici, 0, &c->depthImg));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->rhi.dev, c->depthImg, &mr);
        uint32_t mi = rhi_vk_mem(&c->rhi, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) return 0;
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        VKCHK(vkAllocateMemory(c->rhi.dev, &mai, 0, &c->depthMem));
        VKCHK(vkBindImageMemory(c->rhi.dev, c->depthImg, c->depthMem, 0));
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->depthImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = dFmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VKCHK(vkCreateImageView(c->rhi.dev, &vi, 0, &c->depthView));
    }

    // Проход: цвет CLEAR->PRESENT, глубина CLEAR.
    {
        VkAttachmentDescription at[2];
        memset(at, 0, sizeof at);
        at[0].format = c->rhi.imgFmt;
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
        VKCHK(vkCreateRenderPass(c->rhi.dev, &rpi, 0, &c->rp));
        for (uint32_t i = 0; i < c->rhi.imgN; i++) {
            VkImageView av[2] = {c->rhi.views[i], c->depthView};
            VkFramebufferCreateInfo fi;
            memset(&fi, 0, sizeof fi);
            fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fi.renderPass = c->rp;
            fi.attachmentCount = 2;
            fi.pAttachments = av;
            fi.width = c->rhi.extent.width;
            fi.height = c->rhi.extent.height;
            fi.layers = 1;
            VKCHK(vkCreateFramebuffer(c->rhi.dev, &fi, 0, &c->fbs[i]));
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
        VKCHK(vkCreateDescriptorSetLayout(c->rhi.dev, &li, 0, &c->setLayout));
    }
    if (!core_init2(rc)) return 0;
    if (!core_init3(rc)) return 0;
    c->prevT = vk_time();
    c->ready = 1;
    return 1;
}

static void core_frame(RenderCore *rc, const RcView *v);
static int core_frame_to(RenderCore *rc, const RcView *v, RcTarget *t);
static int core_download(RenderCore *rc, RcTarget *t, uint8_t *rgba, int w, int h);
static int core_frame_cpu(RenderCore *rc, const RcView *v, uint8_t *rgba, int w, int h);
static int core_upload_tiles(RenderCore *rc, const uint8_t *rgba, int tile, int layers);
static int core_init(RenderCore *rc, void *glfwWindow);
static void core_shutdown(RenderCore *rc);


// Саморегистрация в реестре corelib (выбор по имени/домену безifndef).
#ifdef __GNUC__
__attribute__((constructor))
#endif
static void rc_reg_self(void) {
    rc_register("vk", RC_CAP_VOXEL, RC_BACKEND_VK, rc_vk_create);
}

RenderCore *rc_vk_create(void) {
    VKCoreWrap *w = (VKCoreWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_VOXEL; // растр мешей по вокселям
    w->api.name = "vk";
    w->api.backend = RC_BACKEND_VK;
    w->api.init = core_init;
    w->api.shutdown = core_shutdown;
    w->api.frame = core_frame;
    w->api.frame_to = core_frame_to; // нативный offscreen-таргет (композитор)
    w->api.download = core_download;
    w->api.upload_chunk = 0; // сырые воксели ест только DDA (webgpu)
    w->api.set_origin = 0;
    w->api.unload_chunk = 0;
    w->api.upload_voxmesh = core_upload_voxmesh;
    w->api.unload_voxmesh = core_unload_voxmesh;
    w->api.upload_tiles = core_upload_tiles;
    w->api.frame_cpu = core_frame_cpu;
    w->api.fps = core_fps;
    return &w->api;
}

// --- init, часть 2: UBO/сеты/текстуры/пайплайны/синх (вызывается из core_init) ---
// Заливка пикселей в tile-картинку (стейджинг + барьеры).
// Первая — из UNDEFINED, повторные — из SHADER_READ (флаг tileReady).
static int vk_tile_pixels(VKCore *c, const uint8_t *px) {
    VkBuffer stg = VK_NULL_HANDLE;
    VkDeviceMemory stgM = VK_NULL_HANDLE;
    void *sp = 0;
    if (!vk_hbuf(c, (VkDeviceSize)VOX_TILE * VOX_TILE * VOX_LAYERS * 4,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stg, &stgM, &sp)) return 0;
    memcpy(sp, px, (size_t)VOX_TILE * VOX_TILE * VOX_LAYERS * 4);
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (!vk_once(c, &cb)) goto fail;
    VkImageMemoryBarrier b0;
    memset(&b0, 0, sizeof b0);
    b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b0.oldLayout = c->tileReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                : VK_IMAGE_LAYOUT_UNDEFINED;
    b0.srcAccessMask = c->tileReady ? VK_ACCESS_SHADER_READ_BIT : 0;
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
    if (!vk_once_end(c, cb)) goto fail;
    vkDestroyBuffer(c->rhi.dev, stg, 0);
    vkFreeMemory(c->rhi.dev, stgM, 0);
    c->tileReady = 1;
    return 1;
fail:
    if (stg) vkDestroyBuffer(c->rhi.dev, stg, 0);
    if (stgM) vkFreeMemory(c->rhi.dev, stgM, 0);
    return 0;
}

// Атлас из API (фидер): пиксели + тинты материала (те же что в GL-ядре).
// Тинты — константы воксельного материала ядра, не контент.
static int core_upload_tiles(RenderCore *rc, const uint8_t *rgba, int tile, int layers) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!rgba || tile != VOX_TILE || layers != VOX_LAYERS) return 0;
    if (!c->tileImg) return 0;
    static uint8_t tiles[VOX_TEXELS * 4]; // заливка редкая, не цикл кадра
    memcpy(tiles, rgba, sizeof tiles);
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
    return vk_tile_pixels(c, tiles);
}

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
        VKCHK(vkCreateDescriptorPool(c->rhi.dev, &pi, 0, &c->pool));
        VkDescriptorSetLayout ls[VK_FIF] = {c->setLayout, c->setLayout};
        VkDescriptorSetAllocateInfo ai;
        memset(&ai, 0, sizeof ai);
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = c->pool;
        ai.descriptorSetCount = VK_FIF;
        ai.pSetLayouts = ls;
        VKCHK(vkAllocateDescriptorSets(c->rhi.dev, &ai, c->sets));
    }
    // Пул команд/синхронизация — из RHI (rhi_vk_init уже всё поднял).
    // Атлас 16x16x9 + тинты (те же что в GL-ядре) и белый спек.
        // Белая заглушка: настоящие пиксели прилетят через upload_tiles.
        static uint8_t white16[(size_t)VOX_TILE * VOX_TILE * VOX_LAYERS * 4];
        memset(white16, 0xFF, sizeof white16);
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
        VKCHK(vkCreateImage(c->rhi.dev, &ici, 0, &c->tileImg));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->rhi.dev, c->tileImg, &mr);
        uint32_t mi = rhi_vk_mem(&c->rhi, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) return 0;
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        VKCHK(vkAllocateMemory(c->rhi.dev, &mai, 0, &c->tileMem));
        VKCHK(vkBindImageMemory(c->rhi.dev, c->tileImg, c->tileMem, 0));
        if (!vk_tile_pixels(c, white16)) return 0;
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->tileImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = VOX_LAYERS;
        VKCHK(vkCreateImageView(c->rhi.dev, &vi, 0, &c->tileView));
        VkSamplerCreateInfo sm;
        memset(&sm, 0, sizeof sm);
        sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sm.magFilter = VK_FILTER_NEAREST;
        sm.minFilter = VK_FILTER_NEAREST;
        sm.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sm.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sm.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VKCHK(vkCreateSampler(c->rhi.dev, &sm, 0, &c->tileSmp));
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
        VKCHK(vkCreateImage(c->rhi.dev, &ici, 0, &c->specImg));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->rhi.dev, c->specImg, &mr);
        uint32_t mi = rhi_vk_mem(&c->rhi, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) return 0;
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        VKCHK(vkAllocateMemory(c->rhi.dev, &mai, 0, &c->specMem));
        VKCHK(vkBindImageMemory(c->rhi.dev, c->specImg, c->specMem, 0));
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
        vkDestroyBuffer(c->rhi.dev, stg, 0);
        vkFreeMemory(c->rhi.dev, stgM, 0);
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = c->specImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VKCHK(vkCreateImageView(c->rhi.dev, &vi, 0, &c->specView));
        VkSamplerCreateInfo sm;
        memset(&sm, 0, sizeof sm);
        sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sm.magFilter = VK_FILTER_NEAREST;
        sm.minFilter = VK_FILTER_NEAREST;
        VKCHK(vkCreateSampler(c->rhi.dev, &sm, 0, &c->specSmp));
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
        vkUpdateDescriptorSets(c->rhi.dev, 3, w, 0, 0);
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
    VkViewport vp = vk_viewport_fill(c->rhi.extent.width, c->rhi.extent.height);
    VkRect2D sc = vk_scissor_fill(c->rhi.extent.width, c->rhi.extent.height);
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
        VKCHK(vkCreatePipelineLayout(c->rhi.dev, &li, 0, &c->meshPipeLayout));
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
        VKCHK(vkCreatePipelineLayout(c->rhi.dev, &li, 0, &c->skyPipeLayout));
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
    VKCHK(vkCreateGraphicsPipelines(c->rhi.dev, VK_NULL_HANDLE, 1, &pi, 0, &c->meshPipe));
    st[0].module = sVert;
    st[1].module = sFrag;
    pi.pVertexInputState = &vinEmpty;
    pi.pDepthStencilState = &dsOff;
    pi.layout = c->skyPipeLayout;
    VKCHK(vkCreateGraphicsPipelines(c->rhi.dev, VK_NULL_HANDLE, 1, &pi, 0, &c->skyPipe));
    vkDestroyShaderModule(c->rhi.dev, mVert, 0);
    vkDestroyShaderModule(c->rhi.dev, mFrag, 0);
    vkDestroyShaderModule(c->rhi.dev, sVert, 0);
    vkDestroyShaderModule(c->rhi.dev, sFrag, 0);
    // Ридбэк-буфер под своп-экстент (VK_SHOT), разово.
    if (!vk_hbuf(c, (VkDeviceSize)(c->rhi.extent.width * c->rhi.extent.height * 4),
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 &c->shotBuf, &c->shotMem, &c->shotPtr)) return 0;
    printf("vk core OK: mesh+sky, %u swap, fmt=%d\n", c->rhi.imgN, c->rhi.imgFmt);
    return 1;
}

// UBO кадра: те же числа что в GL-ядре. Аспект — от цели (своп или offscreen).
static void vk_frame_ubo(VKCore *c, const RcView *v, int W, int H, int fi) {
    double now = vk_time();
    // UBO кадра: те же числа что в GL-ядре.
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    Mat4 proj = m4persp_vk(2.0f * atanf(0.5f / (v->fov > 0.2f ? v->fov : 1.6f)),
                           (float)W / (float)H, 0.1f, 600.0f);
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
}

// Проход в фреймбуфер: begin (clear или load) -> небо (кроме поздних) ->
// меши -> [бейдж своему экрану] -> end. Без сабмита/ридбэка.
static void vk_draw(VKCore *c, const RcView *v, VkCommandBuffer cb, int fi, VkFramebuffer fb,
                    VkRenderPass rp, int W, int H, int first, int badge) {
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
    VkRenderPassBeginInfo rpi;
    memset(&rpi, 0, sizeof rpi);
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = rp;
    rpi.framebuffer = fb;
    rpi.renderArea.extent.width = (uint32_t)W;
    rpi.renderArea.extent.height = (uint32_t)H;
    rpi.clearValueCount = first ? 2u : 0u;
    rpi.pClearValues = first ? clr : 0;
    vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = vk_viewport_fill((uint32_t)W, (uint32_t)H);
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = vk_scissor_fill((uint32_t)W, (uint32_t)H);
    vkCmdSetScissor(cb, 0, 1, &sc);
    if (first) {
        // Небо (push 112: invVP + sun/night + top/resH + hor/resW).
        Vec3 f, r, u;
        cam_basis(v->yaw, v->pitch, &f, &r, &u);
        Mat4 proj = m4persp_vk(2.0f * atanf(0.5f / (v->fov > 0.2f ? v->fov : 1.6f)),
                               (float)W / (float)H, 0.1f, 600.0f);
        Mat4 view = m4look(v->camPos.x, v->camPos.y, v->camPos.z,
                           v->camPos.x + f.x, v->camPos.y + f.y, v->camPos.z + f.z);
        Mat4 pv = m4mul(&proj, &view);
        Mat4 inv = m4inv(&pv);
        float dayF = v->sunDir.y > 1.0f ? 1.0f : (v->sunDir.y < -1.0f ? -1.0f : v->sunDir.y);
        float nightF = dayF < 0.02f ? (dayF < -0.12f ? 1.0f : (0.02f - dayF) / 0.14f) : 0.0f;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->skyPipe);
        {
            float push[28];
            memcpy(push, inv.m, 64);
            push[16] = v->sunDir.x; push[17] = v->sunDir.y; push[18] = v->sunDir.z;
            push[19] = nightF;
            push[20] = 0.16f; push[21] = 0.32f; push[22] = 0.58f;
            push[23] = (float)H;
            push[24] = 0.55f; push[25] = 0.60f; push[26] = 0.68f;
            push[27] = (float)W;
            vkCmdPushConstants(cb, c->skyPipeLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, 112, push);
        }
        vkCmdDraw(cb, 3, 1, 0, 0);
    }
    // Меши.
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->meshPipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->meshPipeLayout,
                            0, 1, &c->sets[fi], 0, 0);
    for (int i = 0; i < VK_MESH_SLOTS; i++) {
        if (!c->meshes[i].used) continue;
        vkCmdPushConstants(cb, c->meshPipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, 64, c->meshes[i].model);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &c->meshes[i].buf, &off);
        vkCmdDraw(cb, (uint32_t)c->meshes[i].count, 1, 0, 0);
    }
    if (badge) {
        // Бейдж ядра: красный угол 64x32 слева внизу экрана. Строка 0
        // фреймбуфера сверху (vk_conv.h п.3), поэтому rect.y = H-32.
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
        cr.rect.offset.y = (int32_t)(H - 32);
        cr.rect.extent.width = 64;
        cr.rect.extent.height = 32;
        cr.baseArrayLayer = 0;
        cr.layerCount = 1;
        vkCmdClearAttachments(cb, 1, &ca, 1, &cr);
    }
    vkCmdEndRenderPass(cb);
}

// Нативная offscreen-цель Vulkan (лежит в RcTarget.nobj[0], владеет ядро).
// Форматы 1-в-1 со свопом (imgFmt + depthFmt): проходы совместимы,
// пайплайны те же. Два прохода: clear (первый) и load (поздние, глубина
// STORE — цепочка). Финал цвета — TRANSFER_SRC (сразу копируем в стейджинг).
static VkRenderPass vk_target_rp(VKCore *c, int clear) {
    VkAttachmentDescription at[2];
    memset(at, 0, sizeof at);
    at[0].format = c->rhi.imgFmt;
    at[0].samples = VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[0].initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED
                                : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    at[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    at[1].format = c->depthFmt;
    at[1].samples = VK_SAMPLE_COUNT_1_BIT;
    at[1].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    at[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[1].initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED
                                : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(c->rhi.dev, &rpi, 0, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    return rp;
}

static void vk_target_free(VKCore *c, VKTarget *t) {
    if (!t) return;
    vkDeviceWaitIdle(c->rhi.dev);
    if (t->fb) vkDestroyFramebuffer(c->rhi.dev, t->fb, 0);
    if (t->rpClear) vkDestroyRenderPass(c->rhi.dev, t->rpClear, 0);
    if (t->rpLoad) vkDestroyRenderPass(c->rhi.dev, t->rpLoad, 0);
    if (t->view) vkDestroyImageView(c->rhi.dev, t->view, 0);
    if (t->img) vkDestroyImage(c->rhi.dev, t->img, 0);
    if (t->mem) vkFreeMemory(c->rhi.dev, t->mem, 0);
    if (t->dview) vkDestroyImageView(c->rhi.dev, t->dview, 0);
    if (t->dimg) vkDestroyImage(c->rhi.dev, t->dimg, 0);
    if (t->dmem) vkFreeMemory(c->rhi.dev, t->dmem, 0);
    free(t);
}

static VKTarget *vk_target_ensure(VKCore *c, RcTarget *t) {
    if (!t || t->backend != RC_BACKEND_VK || t->w <= 0 || t->h <= 0) return 0;
    VKTarget *tg = (VKTarget *)t->nobj[0];
    if (tg && tg->w == t->w && tg->h == t->h) return tg;
    // Размер сменился (или впервые): старую снести, новую поднять.
    if (tg) {
        vk_target_free(c, tg);
        for (int i = 0; i < c->liveN; i++)
            if (c->liveT[i] == tg) {
                c->liveT[i] = c->liveT[--c->liveN];
                break;
            }
        t->nobj[0] = 0;
    }
    tg = (VKTarget *)calloc(1, sizeof *tg);
    if (!tg) return 0;
    tg->w = t->w;
    tg->h = t->h;
    // Цвет: как своп (совместимость проходов), +TRANSFER_SRC под ридбэк.
    {
        VkImageCreateInfo ici;
        memset(&ici, 0, sizeof ici);
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = c->rhi.imgFmt;
        ici.extent.width = (uint32_t)t->w;
        ici.extent.height = (uint32_t)t->h;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateImage(c->rhi.dev, &ici, 0, &tg->img) != VK_SUCCESS) {
            free(tg);
            return 0;
        }
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->rhi.dev, tg->img, &mr);
        uint32_t mi = rhi_vk_mem(&c->rhi, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) {
            vkDestroyImage(c->rhi.dev, tg->img, 0);
            free(tg);
            return 0;
        }
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        if (vkAllocateMemory(c->rhi.dev, &mai, 0, &tg->mem) != VK_SUCCESS) {
            vkDestroyImage(c->rhi.dev, tg->img, 0);
            free(tg);
            return 0;
        }
        vkBindImageMemory(c->rhi.dev, tg->img, tg->mem, 0);
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = tg->img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = c->rhi.imgFmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(c->rhi.dev, &vi, 0, &tg->view) != VK_SUCCESS) {
            vk_target_free(c, tg);
            return 0;
        }
    }
    // Глубина: тот же формат что свопная (совместимость).
    {
        VkImageCreateInfo ici;
        memset(&ici, 0, sizeof ici);
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = c->depthFmt;
        ici.extent.width = (uint32_t)t->w;
        ici.extent.height = (uint32_t)t->h;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (vkCreateImage(c->rhi.dev, &ici, 0, &tg->dimg) != VK_SUCCESS) {
            vk_target_free(c, tg);
            return 0;
        }
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(c->rhi.dev, tg->dimg, &mr);
        uint32_t mi = rhi_vk_mem(&c->rhi, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == 0xFFFFFFFFu) {
            vk_target_free(c, tg);
            return 0;
        }
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof mai);
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mi;
        if (vkAllocateMemory(c->rhi.dev, &mai, 0, &tg->dmem) != VK_SUCCESS) {
            vk_target_free(c, tg);
            return 0;
        }
        vkBindImageMemory(c->rhi.dev, tg->dimg, tg->dmem, 0);
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = tg->dimg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = c->depthFmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(c->rhi.dev, &vi, 0, &tg->dview) != VK_SUCCESS) {
            vk_target_free(c, tg);
            return 0;
        }
    }
    tg->rpClear = vk_target_rp(c, 1);
    tg->rpLoad = vk_target_rp(c, 0);
    if (!tg->rpClear || !tg->rpLoad) {
        vk_target_free(c, tg);
        return 0;
    }
    {
        VkImageView av[2] = {tg->view, tg->dview};
        VkFramebufferCreateInfo fi;
        memset(&fi, 0, sizeof fi);
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = tg->rpClear; // совместим и с rpLoad (те же аттачменты)
        fi.attachmentCount = 2;
        fi.pAttachments = av;
        fi.width = (uint32_t)t->w;
        fi.height = (uint32_t)t->h;
        fi.layers = 1;
        if (vkCreateFramebuffer(c->rhi.dev, &fi, 0, &tg->fb) != VK_SUCCESS) {
            vk_target_free(c, tg);
            return 0;
        }
    }
    t->nobj[0] = tg;
    if (c->liveN < 4) c->liveT[c->liveN++] = tg;
    return tg;
}

// Сабмит без свопа (композитор): забор+сброс, submit, ждать. 1 ок.
static int vk_submit_wait(VKCore *c, VkCommandBuffer cb) {
    int fi = c->frame % VK_FIF;
    vkWaitForFences(c->rhi.dev, 1, &c->rhi.fences[fi], 1, 1000000000ull);
    vkResetFences(c->rhi.dev, 1, &c->rhi.fences[fi]);
    VkSubmitInfo si;
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    if (vkQueueSubmit(c->rhi.q, 1, &si, c->rhi.fences[fi]) != VK_SUCCESS) return 0;
    if (vkWaitForFences(c->rhi.dev, 1, &c->rhi.fences[fi], 1, 1000000000ull) != VK_SUCCESS)
        return 0;
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
    uint32_t img = 0;
    if (rhi_vk_acquire(&c->rhi, fi, &img) < 0) return;
    vk_frame_ubo(c, v, (int)c->rhi.extent.width, (int)c->rhi.extent.height, fi);
    VkCommandBuffer cb = c->rhi.cmds[fi];
    vk_draw(c, v, cb, fi, c->fbs[img], c->rp, (int)c->rhi.extent.width,
            (int)c->rhi.extent.height, 1, 1);


    // Ридбэк кадра: VK_SHOT=путь.ppm VK_SHOT_AT=кадр (дефолт 60).
    // CPU-забор идёт своей дорогой (scratch-цель + frame_to), не здесь.
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
        b0.image = c->rhi.imgs[img];
        b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b0.subresourceRange.levelCount = 1;
        b0.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b0);
        VkBufferImageCopy cp;
        memset(&cp, 0, sizeof cp);
        cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cp.imageSubresource.layerCount = 1;
        cp.imageExtent.width = c->rhi.extent.width;
        cp.imageExtent.height = c->rhi.extent.height;
        cp.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(cb, c->rhi.imgs[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
    if (!rhi_vk_submit(&c->rhi, fi, img, cb)) return;
    if (shotArmed) {
        vkWaitForFences(c->rhi.dev, 1, &c->rhi.fences[fi], 1, 1000000000ull);
        // BGRA -> PPM RGB, строки снизу вверх (только по VK_SHOT).
        if (shotArmed) {
        FILE *f = fopen(sv, "wb");
        if (f) {
            uint32_t W = c->rhi.extent.width, H = c->rhi.extent.height;
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
        } // shotArmed
    }
    c->frame++;
}

// CPU-забор для Qt-вьюпорта: рендер кадра + скачать в RGBA (верх первый).
// Размер обязан совпасть со свопчейном (окно вьюпорта фиксировано).
// Кадр в нативную цель (композитор/Qt): тот же проход, финал цвета —
// TRANSFER_SRC, оттуда сразу копия в стейджинг. Глубина живёт в цели
// между проходами (STORE). Без бейджа (цель одна на всех).
static int core_frame_to(RenderCore *rc, const RcView *v, RcTarget *t) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    VKTarget *tg = vk_target_ensure(c, t);
    if (!tg) return 0;
    int W = tg->w, H = tg->h;
    int fi = c->frame % VK_FIF;
    vk_frame_ubo(c, v, W, H, fi);
    VkCommandBuffer cb = c->rhi.cmds[fi];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    vk_draw(c, v, cb, fi, tg->fb, t->passClear ? tg->rpClear : tg->rpLoad, W, H,
            t->passClear, 0);
    vkEndCommandBuffer(cb);
    if (!vk_submit_wait(c, cb)) return 0;
    return 1;
}

// Скачать нативную цель в CPU (RGBA, верх первый). Только сразу после
// рендера в неё (картинка в TRANSFER_SRC): барьер видимости + копия +
// ожидание + BGRA->RGBA. Для comp->canvas и тестов глубины.
static int core_download(RenderCore *rc, RcTarget *t, uint8_t *rgba, int w, int h) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!t || t->backend != RC_BACKEND_VK || !rgba || w <= 0 || h <= 0) return 0;
    VKTarget *tg = (VKTarget *)t->nobj[0];
    if (!tg || tg->w != w || tg->h != h) return 0;
    // Стейджинг чтения под размер цели (realloc по мере роста, не в цикле).
    VkDeviceSize need = (VkDeviceSize)w * h * 4;
    if (need > c->grabCap) {
        if (c->grabBuf) vkDestroyBuffer(c->rhi.dev, c->grabBuf, 0);
        if (c->grabMem) vkFreeMemory(c->rhi.dev, c->grabMem, 0);
        c->grabBuf = VK_NULL_HANDLE;
        c->grabMem = VK_NULL_HANDLE;
        c->grabPtr = 0;
        c->grabCap = 0;
        if (!vk_hbuf(c, need, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &c->grabBuf, &c->grabMem,
                     &c->grabPtr))
            return 0;
        c->grabCap = need;
    }
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (!rhi_vk_once(&c->rhi, &cb)) return 0;
    VkImageMemoryBarrier b0;
    memset(&b0, 0, sizeof b0);
    b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b0.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b0.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b0.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b0.image = tg->img;
    b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b0.subresourceRange.levelCount = 1;
    b0.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b0);
    VkBufferImageCopy cp;
    memset(&cp, 0, sizeof cp);
    cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.imageSubresource.layerCount = 1;
    cp.imageExtent.width = (uint32_t)w;
    cp.imageExtent.height = (uint32_t)h;
    cp.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cb, tg->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->grabBuf,
                           1, &cp);
    if (!rhi_vk_once_end(&c->rhi, cb)) return 0;
    // BGRA -> RGBA, строка 0 = верх (vk_conv.h п.3).
    uint8_t *px = (uint8_t *)c->grabPtr;
    for (int y = 0; y < h; y++) {
        uint8_t *src = px + (size_t)y * w * 4;
        uint8_t *dst = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    return 1;
}

// CPU-забор: scratch-цель заказа + frame_to + download (всё честно).
// Размер любой вменяемый (не только своп): цель пересоздаётся.
static int core_frame_cpu(RenderCore *rc, const RcView *v, uint8_t *rgba, int w, int h) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!rgba || w < 64 || h < 64 || w > 2048 || h > 2048) return 0;
    c->scratch.backend = RC_BACKEND_VK;
    c->scratch.w = w;
    c->scratch.h = h;
    c->scratch.passClear = 1;
    if (!core_frame_to(rc, v, &c->scratch)) return 0;
    return core_download(rc, &c->scratch, rgba, w, h);
}

static void core_shutdown(RenderCore *rc) {
    VKCore *c = &((VKCoreWrap *)rc->ctx)->core;
    if (!c->rhi.dev) return;
    vkDeviceWaitIdle(c->rhi.dev);
    for (int i = 0; i < VK_MESH_SLOTS; i++) {
        if (!c->meshes[i].used) continue;
        vkDestroyBuffer(c->rhi.dev, c->meshes[i].buf, 0);
        vkFreeMemory(c->rhi.dev, c->meshes[i].mem, 0);
    }
    if (c->shotBuf) vkDestroyBuffer(c->rhi.dev, c->shotBuf, 0);
    if (c->shotMem) vkFreeMemory(c->rhi.dev, c->shotMem, 0);
    // Живые offscreen-цели + стейджинг чтения (создавало ядро).
    for (int i = 0; i < c->liveN; i++) {
        VKTarget *tg = (VKTarget *)c->liveT[i];
        if (tg) {
            if (c->scratch.nobj[0] == tg) c->scratch.nobj[0] = 0;
            vk_target_free(c, tg);
        }
    }
    c->liveN = 0;
    if (c->scratch.nobj[0]) {
        vk_target_free(c, (VKTarget *)c->scratch.nobj[0]);
        c->scratch.nobj[0] = 0;
    }
    if (c->grabBuf) vkDestroyBuffer(c->rhi.dev, c->grabBuf, 0);
    if (c->grabMem) vkFreeMemory(c->rhi.dev, c->grabMem, 0);
    c->grabPtr = 0;
    c->grabCap = 0;
    for (int i = 0; i < VK_FIF; i++) {
        if (c->uboBuf[i]) vkDestroyBuffer(c->rhi.dev, c->uboBuf[i], 0);
        if (c->uboMem[i]) vkFreeMemory(c->rhi.dev, c->uboMem[i], 0);
    }
    if (c->tileSmp) vkDestroySampler(c->rhi.dev, c->tileSmp, 0);
    if (c->tileView) vkDestroyImageView(c->rhi.dev, c->tileView, 0);
    if (c->tileImg) vkDestroyImage(c->rhi.dev, c->tileImg, 0);
    if (c->tileMem) vkFreeMemory(c->rhi.dev, c->tileMem, 0);
    if (c->specSmp) vkDestroySampler(c->rhi.dev, c->specSmp, 0);
    if (c->specView) vkDestroyImageView(c->rhi.dev, c->specView, 0);
    if (c->specImg) vkDestroyImage(c->rhi.dev, c->specImg, 0);
    if (c->specMem) vkFreeMemory(c->rhi.dev, c->specMem, 0);
    if (c->meshPipe) vkDestroyPipeline(c->rhi.dev, c->meshPipe, 0);
    if (c->skyPipe) vkDestroyPipeline(c->rhi.dev, c->skyPipe, 0);
    if (c->meshPipeLayout) vkDestroyPipelineLayout(c->rhi.dev, c->meshPipeLayout, 0);
    if (c->skyPipeLayout) vkDestroyPipelineLayout(c->rhi.dev, c->skyPipeLayout, 0);
    if (c->pool) vkDestroyDescriptorPool(c->rhi.dev, c->pool, 0);
    if (c->setLayout) vkDestroyDescriptorSetLayout(c->rhi.dev, c->setLayout, 0);
    if (c->fbs) {
        for (uint32_t i = 0; i < c->rhi.imgN; i++)
            if (c->fbs[i]) vkDestroyFramebuffer(c->rhi.dev, c->fbs[i], 0);
        free(c->fbs);
    }
    if (c->rp) vkDestroyRenderPass(c->rhi.dev, c->rp, 0);
    if (c->depthView) vkDestroyImageView(c->rhi.dev, c->depthView, 0);
    if (c->depthImg) vkDestroyImage(c->rhi.dev, c->depthImg, 0);
    if (c->depthMem) vkFreeMemory(c->rhi.dev, c->depthMem, 0);
    // Свопчейн и его вьюхи — собственность RHI (rhi_vk_shutdown ниже).
    rhi_vk_shutdown(&c->rhi);
    memset(c, 0, sizeof *c);
}
