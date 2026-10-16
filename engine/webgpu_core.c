// Ядро webgpu (воксельный DDA): RenderCore API. Третье живое ядро орфана.
// Устройство/поверхность — из RHI (rhi_webgpu), остальное своё: пайплайн,
// тороид вокселей, атлас, небо-LUT, теги против миражей.
// VOX_SHOT читает прямо с поверхности (COPY_SRC в configure, цены ноль).
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "render_api.h"
#include "rtarget.h"
#include "rhi_webgpu.h"
#include "sdf_ubo.h"
#include "sky_lut.h"
#include "vox/vox_chunk.h"
#include "vox/vox_tex.h"
#include "sdf_wgsl.h"

#define FRAMES_IN_FLIGHT 2
#define VOX_STREAM_CH 11
#define VOX_STEX (VOX_STREAM_CH * VOX_SX)

typedef struct {
    GLFWwindow *win;
    RhiWebgpu rhi; // устройство/очередь/поверхность/адаптер — отсюда
    WGPUTextureFormat fmt;
    WGPUSurfaceConfiguration cfg;
    WGPUShaderModule mod;
    WGPUBuffer ubo[FRAMES_IN_FLIGHT];
    WGPURenderPipeline pipeline; // без глубины (своп: глубины нет)
    WGPURenderPipeline pipeDepth; // дубль под цель с глубиной (тест Always, не пишет)
    WGPUBindGroupLayout dbgl; // лейаут pipeDepth (группы только свои)
    WGPUBindGroup dbind[FRAMES_IN_FLIGHT];
    WGPUBindGroup skyBindDepth; // небесная группа под второй лейаут
    WGPUBindGroup bind[FRAMES_IN_FLIGHT];
    WGPUBindGroupLayout bgl;
    WGPUBuffer tagsBuf;
    int32_t tags[484];
    WGPUBuffer gradeBuf;
    WGPUBuffer viewBuf;
    WGPUBuffer frustumBuf;
    SkyLuts sky;
    WGPUBindGroup skyBind;
    WGPUTexture voxTex;
    WGPUTextureView voxView;
    WGPUTexture tileTex;
    WGPUTextureView tileView;
    WGPUSampler tileSmp;
    int slotCX[VOX_STREAM_CH][VOX_STREAM_CH];
    int slotCZ[VOX_STREAM_CH][VOX_STREAM_CH];
    unsigned char slotOk[VOX_STREAM_CH][VOX_STREAM_CH];
    float streamOX, streamOZ;
    // Живые offscreen-цели (создаёт ядро, смерть — в shutdown).
    void *liveT[4];
    int liveN;
    // Scratch-цель frame_cpu (натив в nobj[0], трекается в liveT).
    RcTarget scratch;
    int ready;
    int frame;
    double prevT;
    float fpsEma;
} VoxCore;

typedef struct {
    RenderCore api;
    VoxCore core;
} VoxCoreWrap;

static int wrap11(int c) { int r = c % VOX_STREAM_CH; return r < 0 ? r + VOX_STREAM_CH : r; }

static WGPUShaderModule make_module(WGPUDevice device, const char *src) {
    WGPUShaderSourceWGSL wgsl;
    memset(&wgsl, 0, sizeof wgsl);
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){src, strlen(src)};
    WGPUShaderModuleDescriptor def;
    memset(&def, 0, sizeof def);
    def.nextInChain = (const WGPUChainedStruct *)&wgsl;
    return wgpuDeviceCreateShaderModule(device, &def);
}

static WGPUBuffer ubo_buf(WGPUDevice dev, size_t n) {
    WGPUBufferDescriptor d;
    memset(&d, 0, sizeof d);
    d.size = (uint64_t)n;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(dev, &d);
}

// Конфиг поверхности + зеркало размера/формата для ресайз-проверки.
static void core_reconf(VoxCore *c, int w, int h) {
    if (!rhi_webgpu_configure(&c->rhi, (uint32_t)w, (uint32_t)h)) return;
    c->fmt = c->rhi.fmt;
    memset(&c->cfg, 0, sizeof c->cfg);
    c->cfg.width = (uint32_t)w;
    c->cfg.height = (uint32_t)h;
}

// Заливка пикселей в tile-текстуру (swizzle под pitch 256 + queue write).
// Без тинтов (как было): webgpu-шейдер тинтует сам.
static void wgpu_tile_pixels(VoxCore *c, const uint8_t *px) {
    static uint8_t staging[256 * VOX_TILE * VOX_LAYERS];
    for (int l = 0; l < VOX_LAYERS; l++)
        for (int y = 0; y < VOX_TILE; y++)
            memcpy(&staging[((size_t)l * VOX_TILE + (size_t)y) * 256],
                   &px[((size_t)l * VOX_TILE + (size_t)y) * VOX_TILE * 4], VOX_TILE * 4);
    WGPUTexelCopyTextureInfo dst;
    memset(&dst, 0, sizeof dst);
    dst.texture = c->tileTex;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout;
    memset(&layout, 0, sizeof layout);
    layout.bytesPerRow = 256;
    layout.rowsPerImage = VOX_TILE;
    WGPUExtent3D extent;
    memset(&extent, 0, sizeof extent);
    extent.width = VOX_TILE;
    extent.height = VOX_TILE;
    extent.depthOrArrayLayers = VOX_LAYERS;
    wgpuQueueWriteTexture(c->rhi.queue, &dst, staging, sizeof staging, &layout, &extent);
}

// Атлас из API (фидер). Тинтов нет (шейдерные, как было).
static int core_upload_tiles(RenderCore *rc, const uint8_t *rgba, int tile, int layers) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    if (!rgba || tile != VOX_TILE || layers != VOX_LAYERS) return 0;
    if (!c->tileTex) return 0;
    wgpu_tile_pixels(c, rgba);
    return 1;
}

static void core_build_all(VoxCore *c, const RcView *v) {
    // Вызывается один раз, когда device готов. Значения юниформов придут с кадрами.
    // Поверхность — через RHI (там же COPY_SRC под VOX_SHOT, Mailbox).
    core_reconf(c, v->resW > 0 ? v->resW : 1280, v->resH > 0 ? v->resH : 720);

    c->mod = make_module(c->rhi.device, SDF_WGSL);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
        c->ubo[i] = ubo_buf(c->rhi.device, 64);
    WGPUColorTargetState color;
    memset(&color, 0, sizeof color);
    color.format = c->fmt;
    color.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState frag;
    memset(&frag, 0, sizeof frag);
    frag.module = c->mod;
    frag.entryPoint = (WGPUStringView){"fs", 2};
    frag.targetCount = 1;
    frag.targets = &color;
    WGPUVertexState vert;
    memset(&vert, 0, sizeof vert);
    vert.module = c->mod;
    vert.entryPoint = (WGPUStringView){"vs", 2};
    WGPURenderPipelineDescriptor pipe;
    memset(&pipe, 0, sizeof pipe);
    pipe.vertex = vert;
    pipe.fragment = &frag;
    pipe.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipe.primitive.frontFace = WGPUFrontFace_CCW;
    pipe.primitive.cullMode = WGPUCullMode_None;
    pipe.multisample.count = 1;
    pipe.multisample.mask = 0xFFFFFFFFu;
    c->pipeline = wgpuDeviceCreateRenderPipeline(c->rhi.device, &pipe);
    c->bgl = wgpuRenderPipelineGetBindGroupLayout(c->pipeline, 0);
    // Дубль под offscreen-цель с глубиной: тот же шейдер, тест Always
    // (DDA глубину не пишет — в WGSL нет gl_FragDepth; WG-mesh придёт позже).
    {
        WGPUDepthStencilState ds;
        memset(&ds, 0, sizeof ds);
        ds.format = WGPUTextureFormat_Depth32Float;
        ds.depthWriteEnabled = WGPUOptionalBool_False;
        ds.depthCompare = WGPUCompareFunction_Always;
        pipe.depthStencil = &ds;
        c->pipeDepth = wgpuDeviceCreateRenderPipeline(c->rhi.device, &pipe);
    }

    c->tagsBuf = ubo_buf(c->rhi.device, sizeof c->tags);
    for (int i = 0; i < 484; i++) c->tags[i] = INT32_MAX;
    wgpuQueueWriteBuffer(c->rhi.queue, c->tagsBuf, 0, c->tags, sizeof c->tags);
    c->gradeBuf = ubo_buf(c->rhi.device, 16);
    c->viewBuf = ubo_buf(c->rhi.device, 16);
    c->frustumBuf = ubo_buf(c->rhi.device, 64);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        WGPUBindGroupEntry be[5];
        memset(be, 0, sizeof be);
        be[0].binding = 0; be[0].buffer = c->ubo[i]; be[0].size = 64;
        be[1].binding = 1; be[1].buffer = c->tagsBuf; be[1].size = sizeof c->tags;
        be[2].binding = 2; be[2].buffer = c->gradeBuf; be[2].size = 16;
        be[3].binding = 3; be[3].buffer = c->viewBuf; be[3].size = 16;
        be[4].binding = 4; be[4].buffer = c->frustumBuf; be[4].size = 64;
        WGPUBindGroupDescriptor bgdef;
        memset(&bgdef, 0, sizeof bgdef);
        bgdef.layout = c->bgl;
        bgdef.entryCount = 5;
        bgdef.entries = be;
        c->bind[i] = wgpuDeviceCreateBindGroup(c->rhi.device, &bgdef);
    }
    // Бинд-группы под второй лейаут pipeDepth (те же буферы: чужой лейаут
    // с теми же handles не катит — exclusive pipelines).
    c->dbgl = wgpuRenderPipelineGetBindGroupLayout(c->pipeDepth, 0);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        WGPUBindGroupEntry be[5];
        memset(be, 0, sizeof be);
        be[0].binding = 0; be[0].buffer = c->ubo[i]; be[0].size = 64;
        be[1].binding = 1; be[1].buffer = c->tagsBuf; be[1].size = sizeof c->tags;
        be[2].binding = 2; be[2].buffer = c->gradeBuf; be[2].size = 16;
        be[3].binding = 3; be[3].buffer = c->viewBuf; be[3].size = 16;
        be[4].binding = 4; be[4].buffer = c->frustumBuf; be[4].size = 64;
        WGPUBindGroupDescriptor bgdef;
        memset(&bgdef, 0, sizeof bgdef);
        bgdef.layout = c->dbgl;
        bgdef.entryCount = 5;
        bgdef.entries = be;
        c->dbind[i] = wgpuDeviceCreateBindGroup(c->rhi.device, &bgdef);
    }

    // Тороидальная 3D-текстура вокселей.
    {
        WGPUTextureDescriptor td;
        memset(&td, 0, sizeof td);
        td.size.width = VOX_STEX;
        td.size.height = VOX_SY;
        td.size.depthOrArrayLayers = VOX_STEX;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_3D;
        td.format = WGPUTextureFormat_R8Uint;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        c->voxTex = wgpuDeviceCreateTexture(c->rhi.device, &td);
        WGPUTextureViewDescriptor vd;
        memset(&vd, 0, sizeof vd);
        vd.format = WGPUTextureFormat_R8Uint;
        vd.dimension = WGPUTextureViewDimension_3D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        c->voxView = wgpuTextureCreateView(c->voxTex, &vd);
        printf("voxels OK: stream %dx%dx%d (%dx%d chunks)\n",
            VOX_STEX, VOX_SY, VOX_STEX, VOX_STREAM_CH, VOX_STREAM_CH);
    }
    // Атлас тайлов: объекты тут, пиксели — через upload_tiles (фидер).
    // До первой заливки — белая заглушка (валидный сэмплер).
    {
        WGPUTextureDescriptor td;
        memset(&td, 0, sizeof td);
        td.size.width = VOX_TILE;
        td.size.height = VOX_TILE;
        td.size.depthOrArrayLayers = VOX_LAYERS;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        c->tileTex = wgpuDeviceCreateTexture(c->rhi.device, &td);
        WGPUTextureViewDescriptor vd;
        memset(&vd, 0, sizeof vd);
        vd.format = WGPUTextureFormat_RGBA8Unorm;
        vd.dimension = WGPUTextureViewDimension_2DArray;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = VOX_LAYERS;
        vd.aspect = WGPUTextureAspect_All;
        c->tileView = wgpuTextureCreateView(c->tileTex, &vd);
        WGPUSamplerDescriptor sd;
        memset(&sd, 0, sizeof sd);
        sd.addressModeU = WGPUAddressMode_ClampToEdge;
        sd.addressModeV = WGPUAddressMode_ClampToEdge;
        sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.magFilter = WGPUFilterMode_Nearest;
        sd.minFilter = WGPUFilterMode_Nearest;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.maxAnisotropy = 1;
        c->tileSmp = wgpuDeviceCreateSampler(c->rhi.device, &sd);
        static uint8_t white16[(size_t)VOX_TILE * VOX_TILE * VOX_LAYERS * 4];
        memset(white16, 0xFF, sizeof white16);
        wgpu_tile_pixels(c, white16);
    }
    sky_luts_init(&c->sky, c->rhi.device, c->rhi.queue);
    {
        WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(c->pipeline, 1);
        WGPUBindGroupEntry e[6];
        memset(e, 0, sizeof e);
        e[0].binding = 0; e[0].textureView = c->sky.sunView;
        e[1].binding = 1; e[1].textureView = c->sky.moonView;
        e[2].binding = 2; e[2].sampler = c->sky.smp;
        e[3].binding = 3; e[3].textureView = c->voxView;
        e[4].binding = 4; e[4].textureView = c->tileView;
        e[5].binding = 5; e[5].sampler = c->tileSmp;
        WGPUBindGroupDescriptor d;
        memset(&d, 0, sizeof d);
        d.layout = l;
        d.entryCount = 6;
        d.entries = e;
        c->skyBind = wgpuDeviceCreateBindGroup(c->rhi.device, &d);
        wgpuBindGroupLayoutRelease(l);
    }
    // Небесная группа под второй лейаут (те же вьюхи).
    {
        WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(c->pipeDepth, 1);
        WGPUBindGroupEntry e[6];
        memset(e, 0, sizeof e);
        e[0].binding = 0; e[0].textureView = c->sky.sunView;
        e[1].binding = 1; e[1].textureView = c->sky.moonView;
        e[2].binding = 2; e[2].sampler = c->sky.smp;
        e[3].binding = 3; e[3].textureView = c->voxView;
        e[4].binding = 4; e[4].textureView = c->tileView;
        e[5].binding = 5; e[5].sampler = c->tileSmp;
        WGPUBindGroupDescriptor d;
        memset(&d, 0, sizeof d);
        d.layout = l;
        d.entryCount = 6;
        d.entries = e;
        c->skyBindDepth = wgpuDeviceCreateBindGroup(c->rhi.device, &d);
        wgpuBindGroupLayoutRelease(l);
    }
    c->ready = 1;
    printf("pipe OK: core ready\n");
}

static int core_upload_chunk(RenderCore *rc, int cx, int cz, const uint8_t *vox16,
                             const VoxChunk *nb[6]) {
    (void)nb; // DDA читает сырые воксели, соседи не нужны
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    if (!c->ready) return 0; // устройства нет — app повторит (dirty не гасим)
    int sx = wrap11(cx), sz = wrap11(cz);
    static uint8_t staging[256 * VOX_SY * VOX_SZ];
    for (int y = 0; y < VOX_SY; y++)
        for (int z = 0; z < VOX_SZ; z++)
            memcpy(&staging[((size_t)z * VOX_SY + (size_t)y) * 256],
                   &vox16[((size_t)y * VOX_SZ + (size_t)z) * VOX_SX], VOX_SX);
    WGPUTexelCopyTextureInfo dst;
    memset(&dst, 0, sizeof dst);
    dst.texture = c->voxTex;
    dst.aspect = WGPUTextureAspect_All;
    dst.origin.x = (uint32_t)(sx * VOX_SX);
    dst.origin.z = (uint32_t)(sz * VOX_SZ);
    WGPUTexelCopyBufferLayout layout;
    memset(&layout, 0, sizeof layout);
    layout.bytesPerRow = 256;
    layout.rowsPerImage = VOX_SY;
    WGPUExtent3D extent;
    memset(&extent, 0, sizeof extent);
    extent.width = VOX_SX;
    extent.height = VOX_SY;
    extent.depthOrArrayLayers = VOX_SZ;
    wgpuQueueWriteTexture(c->rhi.queue, &dst, staging, sizeof staging, &layout, &extent);
    c->slotCX[sx][sz] = cx;
    c->slotCZ[sx][sz] = cz;
    c->slotOk[sx][sz] = 1;
    c->tags[(sz * VOX_STREAM_CH + sx) * 4] = cx;
    c->tags[(sz * VOX_STREAM_CH + sx) * 4 + 1] = cz;
    wgpuQueueWriteBuffer(c->rhi.queue, c->tagsBuf, 0, c->tags, sizeof c->tags);
    return 1;
}

static void core_set_origin(RenderCore *rc, int ox, int oz) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    c->streamOX = (float)(ox * VOX_SX);
    c->streamOZ = (float)(oz * VOX_SZ);
}

static void core_unload_chunk(RenderCore *rc, int cx, int cz) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    if (!c->ready) return;
    int sx = wrap11(cx), sz = wrap11(cz);
    // Тороидальный слот могли уже перезалить чужим — трогаем только своё.
    if (!c->slotOk[sx][sz] || c->slotCX[sx][sz] != cx || c->slotCZ[sx][sz] != cz) return;
    static uint8_t air[256 * VOX_SY * VOX_SZ];
    memset(air, 0, sizeof air);
    WGPUTexelCopyTextureInfo dst;
    memset(&dst, 0, sizeof dst);
    dst.texture = c->voxTex;
    dst.aspect = WGPUTextureAspect_All;
    dst.origin.x = (uint32_t)(sx * VOX_SX);
    dst.origin.z = (uint32_t)(sz * VOX_SZ);
    WGPUTexelCopyBufferLayout layout;
    memset(&layout, 0, sizeof layout);
    layout.bytesPerRow = 256;
    layout.rowsPerImage = VOX_SY;
    WGPUExtent3D extent;
    memset(&extent, 0, sizeof extent);
    extent.width = VOX_SX;
    extent.height = VOX_SY;
    extent.depthOrArrayLayers = VOX_SZ;
    wgpuQueueWriteTexture(c->rhi.queue, &dst, air, sizeof air, &layout, &extent);
    c->slotOk[sx][sz] = 0;
    c->tags[(sz * VOX_STREAM_CH + sx) * 4] = INT32_MAX; // сентинел: мимо
    c->tags[(sz * VOX_STREAM_CH + sx) * 4 + 1] = INT32_MAX;
    wgpuQueueWriteBuffer(c->rhi.queue, c->tagsBuf, 0, c->tags, sizeof c->tags);
}

static float core_fps(RenderCore *rc) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    return c->fpsEma;
}

// Скриншот: поверхность -> буфер -> PPM (BGRA/RGBA по факту формата).
typedef struct {
    int done;
} ShotWait;

static void on_shot_map(WGPUMapAsyncStatus st, WGPUStringView msg, void *u1, void *u2) {
    (void)msg; (void)u2;
    ShotWait *w = (ShotWait *)u1;
    if (st != WGPUMapAsyncStatus_Success)
        fprintf(stderr, "shot map fail\n");
    w->done = 1;
}

// Скриншот/забор: поверхность -> буфер -> PPM в файл (path) и/или RGBA
// в память (rgba, верх первый). Возвращает 1 если скачал, 0 если map не встал.
static int wgpu_grab(VoxCore *c, WGPUTexture tex, uint32_t w, uint32_t h,
                     const char *path, uint8_t *rgba) {
    uint32_t pitch = (w * 4 + 255) & ~255u;
    WGPUBufferDescriptor bd;
    memset(&bd, 0, sizeof bd);
    bd.size = (uint64_t)pitch * h;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer rb = wgpuDeviceCreateBuffer(c->rhi.device, &bd);
    if (!rb) return 0;
    WGPUCommandEncoderDescriptor ed;
    memset(&ed, 0, sizeof ed);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(c->rhi.device, &ed);
    WGPUTexelCopyTextureInfo src;
    memset(&src, 0, sizeof src);
    src.texture = tex;
    src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst;
    memset(&dst, 0, sizeof dst);
    dst.buffer = rb;
    dst.layout.bytesPerRow = pitch;
    dst.layout.rowsPerImage = h;
    WGPUExtent3D ex;
    memset(&ex, 0, sizeof ex);
    ex.width = w;
    ex.height = h;
    ex.depthOrArrayLayers = 1;
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ex);
    WGPUCommandBufferDescriptor cd;
    memset(&cd, 0, sizeof cd);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cd);
    wgpuQueueSubmit(c->rhi.queue, 1, &cb);
    WGPUBufferMapCallbackInfo mi;
    memset(&mi, 0, sizeof mi);
    mi.mode = WGPUCallbackMode_AllowSpontaneous; // native: WaitAny паникует
    mi.callback = on_shot_map;
    ShotWait sw = {0};
    mi.userdata1 = &sw;
    wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, bd.size, mi);
    // wgpuDevicePoll вычищен из заголовка harmonization, но жив в .so
    // (проверено nm). Без него map-колбэк не прилетает никогда.
    void wgpuDevicePoll(WGPUDevice device, _Bool wait, const void *wrapped);
    for (int i = 0; i < 5000 && !sw.done; i++) wgpuDevicePoll(c->rhi.device, 1, 0);
    int ok = 0;
    if (sw.done) {
        const uint8_t *px = (const uint8_t *)wgpuBufferGetConstMappedRange(rb, 0, bd.size);
        if (px) {
            int bgra = (c->fmt == WGPUTextureFormat_BGRA8Unorm);
            if (rgba) {
                // В память (RGBA, верх первый).
                for (uint32_t y = 0; y < h; y++) {
                    const uint8_t *row = px + (size_t)y * pitch;
                    uint8_t *dst = rgba + (size_t)y * w * 4;
                    for (uint32_t x = 0; x < w; x++) {
                        dst[x * 4 + 0] = row[x * 4 + (bgra ? 2 : 0)];
                        dst[x * 4 + 1] = row[x * 4 + 1];
                        dst[x * 4 + 2] = row[x * 4 + (bgra ? 0 : 2)];
                        dst[x * 4 + 3] = row[x * 4 + 3];
                    }
                }
                ok = 1;
            }
            if (path) {
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%u %u\n255\n", w, h);
                for (uint32_t y = 0; y < h; y++) {
                    const uint8_t *row = px + (size_t)(h - 1 - y) * pitch;
                    for (uint32_t x = 0; x < w; x++) {
                        uint8_t bgr[3] = {
                            row[x * 4 + (bgra ? 2 : 0)],
                            row[x * 4 + 1],
                            row[x * 4 + (bgra ? 0 : 2)],
                        };
                        fwrite(bgr, 1, 3, f);
                    }
                }
                fclose(f);
                fprintf(stderr, "shot: %s (%ux%u)\n", path, w, h);
                ok = 1;
            }
            } // path
            wgpuBufferUnmap(rb);
        }
    }
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(rb);
    return ok;
}

// Скриншот: поверхность -> PPM (BGRA/RGBA по факту формата).
static void wgpu_shot(VoxCore *c, WGPUTexture tex, uint32_t w, uint32_t h, const char *path) {
    wgpu_grab(c, tex, w, h, path, 0);
}

// Рендер DDA в вид (своп или таргет): юниформы + проход + сабмит.
// Без present/шота. first=0: чисток нет (LOAD), небо красит всё равно
// (отдельного неба нет — miss-цвет внутри марша; поздние WG-проходы
// придут с mesh-ядром, тогда и uLate).
// dview NULL = без глубины (сегодняшний своп-путь 1-в-1).
static int wg_render(VoxCore *c, const RcView *v, WGPUTextureView view, WGPUTextureView dview,
                     int W, int H, int first) {
    Vec3 f, r, u_;
    cam_basis(v->yaw, v->pitch, &f, &r, &u_);
    Vec3 sunDir = sdf_sun(v->dayT);
    Vec3 moonDir = v3(-sunDir.x, -sunDir.y, -sunDir.z);
    sky_luts_update(&c->sky, sunDir, moonDir, v->camPos.y);

    SdfUBO u;
    memset(&u, 0, sizeof u);
    u.camPos = v->camPos; u.time = v->time;
    u.camTarget = v3_add(v->camPos, f); u.resX = (float)W;
    u.sunDir = sunDir; u.maxSteps = v->maxSteps;
    u.resY = (float)H; u.mode = (float)v->viewMode;
    u.pad[0] = c->streamOX; u.pad[1] = c->streamOZ;
    int fi = c->frame % FRAMES_IN_FLIGHT;
    wgpuQueueWriteBuffer(c->rhi.queue, c->ubo[fi], 0, &u, sizeof u);
    {
        // Грейд — честные поля вида. Меню-оверлей и фрустум-дебаг из RcView
        // вычищены (были мёртвыми: все всегда слали нули): меню выкл (-1),
        // фрустум выкл (0) с нулевой матрицей — шейдер их не рисует.
        float g[4] = {v->gamma, v->exposure, v->fog, -1.0f};
        wgpuQueueWriteBuffer(c->rhi.queue, c->gradeBuf, 0, g, sizeof g);
        float vw[4] = {v->fov, v->shadowOn, 0.0f, 0};
        wgpuQueueWriteBuffer(c->rhi.queue, c->viewBuf, 0, vw, sizeof vw);
        float fr[16] = {0};
        wgpuQueueWriteBuffer(c->rhi.queue, c->frustumBuf, 0, fr, sizeof fr);
    }

    WGPUCommandEncoderDescriptor edef;
    memset(&edef, 0, sizeof edef);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(c->rhi.device, &edef);
    WGPURenderPassColorAttachment ca;
    memset(&ca, 0, sizeof ca);
    ca.view = view;
    ca.loadOp = first ? WGPULoadOp_Clear : WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0.02, 0.03, 0.06, 1.0};
    WGPURenderPassDescriptor rp;
    memset(&rp, 0, sizeof rp);
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    WGPURenderPassDepthStencilAttachment da;
    memset(&da, 0, sizeof da);
    if (dview) {
        // Глубина цели: чистим первым, храним всегда (цепочка проходов).
        da.view = dview;
        da.depthLoadOp = first ? WGPULoadOp_Clear : WGPULoadOp_Load;
        da.depthStoreOp = WGPUStoreOp_Store;
        da.depthClearValue = 1.0f;
        da.stencilLoadOp = WGPULoadOp_Undefined;
        da.stencilStoreOp = WGPUStoreOp_Undefined;
        rp.depthStencilAttachment = &da;
    }
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, dview ? c->pipeDepth : c->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, dview ? c->dbind[fi] : c->bind[fi], 0, 0);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, dview ? c->skyBindDepth : c->skyBind, 0, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor cbdef;
    memset(&cbdef, 0, sizeof cbdef);
    WGPUCommandBuffer cbuf = wgpuCommandEncoderFinish(enc, &cbdef);
    wgpuQueueSubmit(c->rhi.queue, 1, &cbuf);
    wgpuCommandBufferRelease(cbuf);
    wgpuCommandEncoderRelease(enc);

    return 1;
}

// Нативная offscreen-цель WebGPU (лежит в RcTarget.nobj[0], владеет ядро).
// Цвет c->fmt + RENDER_ATTACHMENT|COPY_SRC|TEXTURE_BINDING; глубина —
// честный Depth32Float (пишет/тестит её будущий WG-mesh; DDA-марш глубину
// не выдаёт — в WGSL нет gl_FragDepth. Цель готова, интероп — следующим).
typedef struct {
    int w, h;
    WGPUTexture tex;
    WGPUTextureView view;
    WGPUTexture dtex;
    WGPUTextureView dview;
} WGTarget;

static void wg_target_free(VoxCore *c, WGTarget *t) {
    (void)c;
    if (!t) return;
    if (t->view) wgpuTextureViewRelease(t->view);
    if (t->tex) wgpuTextureRelease(t->tex);
    if (t->dview) wgpuTextureViewRelease(t->dview);
    if (t->dtex) wgpuTextureRelease(t->dtex);
    free(t);
}

static WGTarget *wg_target_ensure(VoxCore *c, RcTarget *t) {
    if (!t || t->backend != RC_BACKEND_WEBGPU || t->w <= 0 || t->h <= 0) return 0;
    WGTarget *tg = (WGTarget *)t->nobj[0];
    if (tg && tg->w == t->w && tg->h == t->h) return tg;
    if (tg) {
        wg_target_free(c, tg);
        for (int i = 0; i < c->liveN; i++)
            if (c->liveT[i] == tg) {
                c->liveT[i] = c->liveT[--c->liveN];
                break;
            }
        t->nobj[0] = 0;
    }
    tg = (WGTarget *)calloc(1, sizeof *tg);
    if (!tg) return 0;
    tg->w = t->w;
    tg->h = t->h;
    {
        WGPUTextureDescriptor td;
        memset(&td, 0, sizeof td);
        td.size.width = (uint32_t)t->w;
        td.size.height = (uint32_t)t->h;
        td.size.depthOrArrayLayers = 1;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = c->fmt;
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                   WGPUTextureUsage_TextureBinding;
        tg->tex = wgpuDeviceCreateTexture(c->rhi.device, &td);
        if (!tg->tex) {
            wg_target_free(c, tg);
            return 0;
        }
        WGPUTextureViewDescriptor vd;
        memset(&vd, 0, sizeof vd);
        vd.format = c->fmt;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        tg->view = wgpuTextureCreateView(tg->tex, &vd);
        if (!tg->view) {
            wg_target_free(c, tg);
            return 0;
        }
    }
    {
        WGPUTextureDescriptor td;
        memset(&td, 0, sizeof td);
        td.size.width = (uint32_t)t->w;
        td.size.height = (uint32_t)t->h;
        td.size.depthOrArrayLayers = 1;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_Depth32Float;
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        tg->dtex = wgpuDeviceCreateTexture(c->rhi.device, &td);
        if (!tg->dtex) {
            wg_target_free(c, tg);
            return 0;
        }
        WGPUTextureViewDescriptor vd;
        memset(&vd, 0, sizeof vd);
        vd.format = WGPUTextureFormat_Depth32Float;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_DepthOnly;
        tg->dview = wgpuTextureCreateView(tg->dtex, &vd);
        if (!tg->dview) {
            wg_target_free(c, tg);
            return 0;
        }
    }
    t->nobj[0] = tg;
    if (c->liveN < 4) c->liveT[c->liveN++] = tg;
    return tg;
}

static void core_frame(RenderCore *rc, const RcView *v) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    // Устройство готово из init (RHI ждёт сам); пайплайны — лениво под размер.
    if (!c->ready) {
        core_build_all(c, v);
        return;
    }

    int ww = v->resW, hh = v->resH;
    if (ww <= 0) ww = 1280;
    if (hh <= 0) hh = 720;
    if ((uint32_t)ww != c->cfg.width || (uint32_t)hh != c->cfg.height)
        core_reconf(c, ww, hh);
    double now = glfwGetTime();
    float dt = (float)(now - c->prevT);
    c->prevT = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        c->fpsEma = c->fpsEma > 0.0f ? c->fpsEma * 0.95f + fps * 0.05f : fps;
    }

    WGPUSurfaceTexture st;
    memset(&st, 0, sizeof st);
    wgpuSurfaceGetCurrentTexture(c->rhi.surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        fprintf(stderr, "getCurrentTexture status=%d\n", (int)st.status);
        return;
    }
    WGPUTextureViewDescriptor vdef;
    memset(&vdef, 0, sizeof vdef);
    vdef.format = c->fmt;
    vdef.dimension = WGPUTextureViewDimension_2D;
    vdef.mipLevelCount = 1;
    vdef.arrayLayerCount = 1;
    vdef.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, &vdef);
    wg_render(c, v, view, NULL, ww, hh, 1);
    // VOX_SHOT: копия поверхности в хост (COPY_SRC выставлен в configure).
    {
        const char *sp = getenv("VOX_SHOT");
        if (sp && sp[0]) {
            const char *sa = getenv("VOX_SHOT_AT");
            int at = sa && sa[0] ? atoi(sa) : 60;
            if (c->frame == at) wgpu_shot(c, st.texture, (uint32_t)ww, (uint32_t)hh, sp);
        }
    }
    wgpuSurfacePresent(c->rhi.surface);

    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);
    c->frame++;
}

// Кадр в нативную цель (композитор): тот же рендер, без present.
// Глубина цели чистится/хранится (про запас под WG-mesh), DDA её
// не пишет (в WGSL нет gl_FragDepth) — честно зафиксировано.
static int core_frame_to(RenderCore *rc, const RcView *v, RcTarget *t) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    RcView vv = *v;
    if (!c->ready) {
        // Ленивый билд и тут (иначе первый кадр через цель — отказ).
        if (!vv.resW) vv.resW = 1280;
        if (!vv.resH) vv.resH = 720;
        core_build_all(c, &vv);
        if (!c->ready) return 0;
    }
    WGTarget *tg = wg_target_ensure(c, t);
    if (!tg) return 0;
    vv.resW = tg->w;
    vv.resH = tg->h;
    return wg_render(c, &vv, tg->view, tg->dview, tg->w, tg->h, t->passClear);
}

// Скачать нативную цель в CPU (RGBA, верх первый). Сразу после рендера.
static int core_download(RenderCore *rc, RcTarget *t, uint8_t *rgba, int w, int h) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    if (!t || t->backend != RC_BACKEND_WEBGPU || !rgba || w <= 0 || h <= 0) return 0;
    WGTarget *tg = (WGTarget *)t->nobj[0];
    if (!tg || tg->w != w || tg->h != h) return 0;
    return wgpu_grab(c, tg->tex, (uint32_t)w, (uint32_t)h, 0, rgba);
}

// CPU-забор: scratch-цель заказа + frame_to + download (всё честно).
// Размер любой вменяемый: цель пересоздаётся (своп не трогаем).
static int core_frame_cpu(RenderCore *rc, const RcView *v, uint8_t *rgba, int w, int h) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    if (!rgba || w < 64 || h < 64 || w > 2048 || h > 2048) return 0;
    c->scratch.backend = RC_BACKEND_WEBGPU;
    c->scratch.w = w;
    c->scratch.h = h;
    c->scratch.passClear = 1;
    RcView vv = *v;
    vv.resW = w;
    vv.resH = h;
    if (!core_frame_to(rc, &vv, &c->scratch)) return 0;
    return core_download(rc, &c->scratch, rgba, w, h);
}

static int core_init(RenderCore *rc, void *glfwWindow) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    memset(c, 0, sizeof *c);
    c->win = (GLFWwindow *)glfwWindow;
    if (!rhi_webgpu_init(&c->rhi, c->win)) return 0; // instance→device сам ждёт
    printf("webgpu: %s\n", c->rhi.devName[0] ? c->rhi.devName : "?");
    c->prevT = glfwGetTime();
    return 1;
}

static void core_shutdown(RenderCore *rc) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    // Ядро могли не построить (ни одного кадра: build ленивый) — тогда
    // хендлы NULL, а wgpu-native на release(NULL) паникует. Гасим только RHI.
    if (c->ready) {
        for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
            wgpuBindGroupRelease(c->bind[i]);
            wgpuBufferRelease(c->ubo[i]);
            if (c->dbind[i]) wgpuBindGroupRelease(c->dbind[i]);
            c->dbind[i] = 0;
        }
        if (c->skyBindDepth) wgpuBindGroupRelease(c->skyBindDepth);
        c->skyBindDepth = 0;
        wgpuBindGroupLayoutRelease(c->bgl);
        if (c->dbgl) wgpuBindGroupLayoutRelease(c->dbgl);
        c->dbgl = 0;
        wgpuRenderPipelineRelease(c->pipeline);
        if (c->pipeDepth) wgpuRenderPipelineRelease(c->pipeDepth);
        c->pipeDepth = 0;
        wgpuShaderModuleRelease(c->mod);
        c->ready = 0;
    }
    // Живые offscreen-цели (создавало ядро; RHI ещё жив — порядок важен).
    for (int i = 0; i < c->liveN; i++) {
        WGTarget *tg = (WGTarget *)c->liveT[i];
        if (tg) {
            if (c->scratch.nobj[0] == tg) c->scratch.nobj[0] = 0;
            wg_target_free(c, tg);
        }
    }
    c->liveN = 0;
    if (c->scratch.nobj[0]) {
        wg_target_free(c, (WGTarget *)c->scratch.nobj[0]);
        c->scratch.nobj[0] = 0;
    }
    rhi_webgpu_shutdown(&c->rhi);
}


// Саморегистрация в реестре corelib (выбор по имени/домену безifndef).
#ifdef __GNUC__
__attribute__((constructor))
#endif
static void rc_reg_self(void) {
    rc_register("webgpu", RC_CAP_VOXEL, RC_BACKEND_WEBGPU, rc_webgpu_create);
}

RenderCore *rc_webgpu_create(void) {
    VoxCoreWrap *w = (VoxCoreWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_VOXEL; // DDA-луч по вокселям, других входов нет
    w->api.name = "webgpu";
    w->api.backend = RC_BACKEND_WEBGPU;
    w->api.init = core_init;
    w->api.shutdown = core_shutdown;
    w->api.frame = core_frame;
    w->api.upload_chunk = core_upload_chunk;
    w->api.set_origin = core_set_origin;
    w->api.unload_chunk = core_unload_chunk;
    w->api.upload_tiles = core_upload_tiles;
    w->api.frame_cpu = core_frame_cpu;
    w->api.frame_to = core_frame_to; // нативный offscreen-таргет (композитор)
    w->api.download = core_download;
    w->api.fps = core_fps;
    return &w->api;
}

