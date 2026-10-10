// Ядро webgpu: RenderCore API. Механики (ввод/камеры/меню/часы/мир) — снаружи.
// Владеет: surface/device, пайплайны, текстуры (воксели/тайлы/LUT), UBO,
// заливки, сабмит, present. Ноль аллокаций в кадре после init.
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "core/render_api.h"
#include "sdf_surface.h"
#include "sdf_math.h"
#include "sdf_ubo.h"
#include "sky_lut.h"
#include "vox/vox_chunk.h"
#include "vox/vox_tex.h"
#include "sdf_wgsl.h"

#ifdef __EMSCRIPTEN__
#define CB_MODE WGPUCallbackMode_AllowSpontaneous
#else
#define CB_MODE WGPUCallbackMode_WaitAnyOnly
#endif

#define FRAMES_IN_FLIGHT 2
#define VOX_STREAM_CH 11
#define VOX_STEX (VOX_STREAM_CH * VOX_SX)

typedef struct {
    GLFWwindow *win;
    WGPUInstance inst;
    WGPUSurface surface;
    WGPUAdapter adapter;
    int adapterDone;
    WGPUDevice device;
    int deviceDone;
    WGPUQueue queue;
    WGPUTextureFormat fmt;
    WGPUSurfaceConfiguration cfg;
    WGPUShaderModule mod;
    WGPUBuffer ubo[FRAMES_IN_FLIGHT];
    WGPURenderPipeline pipeline;
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

static void onAdapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg,
                      void *u1, void *u2) {
    (void)u2;
    VoxCore *c = (VoxCore *)u1;
    if (st != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "adapter fail: %.*s\n", (int)msg.length, msg.data);
        c->adapterDone = 1;
        return;
    }
    c->adapter = a;
    c->adapterDone = 1;
}

static void onDevice(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg,
                     void *u1, void *u2) {
    (void)u2;
    VoxCore *c = (VoxCore *)u1;
    if (st != WGPURequestDeviceStatus_Success) {
        fprintf(stderr, "device fail: %.*s\n", (int)msg.length, msg.data);
        c->deviceDone = 1;
        return;
    }
    c->device = d;
    c->deviceDone = 1;
}

#ifndef __EMSCRIPTEN__
// wgpu-native: ProcessEvents не реализован — ждём слипом.
static void wait_for(volatile int *done) {
    struct timespec ts = {0, 2000000};
    for (int i = 0; i < 5000 && !*done; i++) nanosleep(&ts, 0);
}
#endif

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

static void core_build_all(VoxCore *c, const RcView *v) {
    // Вызывается один раз, когда device готов. Значения юниформов придут с кадрами.
    WGPUSurfaceCapabilities caps;
    memset(&caps, 0, sizeof caps);
    wgpuSurfaceGetCapabilities(c->surface, c->adapter, &caps);
    c->fmt = caps.formats[0];
    memset(&c->cfg, 0, sizeof c->cfg);
    c->cfg.device = c->device;
    c->cfg.format = c->fmt;
    c->cfg.usage = WGPUTextureUsage_RenderAttachment;
    c->cfg.width = (uint32_t)(v->resW > 0 ? v->resW : 1280);
    c->cfg.height = (uint32_t)(v->resH > 0 ? v->resH : 720);
    c->cfg.presentMode = WGPUPresentMode_Mailbox; // Immediate не поддерживается ([Mailbox, Fifo])
    c->cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(c->surface, &c->cfg);

    c->mod = make_module(c->device, SDF_WGSL);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
        c->ubo[i] = ubo_buf(c->device, 64);
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
    c->pipeline = wgpuDeviceCreateRenderPipeline(c->device, &pipe);
    c->bgl = wgpuRenderPipelineGetBindGroupLayout(c->pipeline, 0);

    c->tagsBuf = ubo_buf(c->device, sizeof c->tags);
    for (int i = 0; i < 484; i++) c->tags[i] = INT32_MAX;
    wgpuQueueWriteBuffer(c->queue, c->tagsBuf, 0, c->tags, sizeof c->tags);
    c->gradeBuf = ubo_buf(c->device, 16);
    c->viewBuf = ubo_buf(c->device, 16);
    c->frustumBuf = ubo_buf(c->device, 64);
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
        c->bind[i] = wgpuDeviceCreateBindGroup(c->device, &bgdef);
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
        c->voxTex = wgpuDeviceCreateTexture(c->device, &td);
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
    // Атлас тайлов.
    {
        static uint8_t tiles[VOX_TEXELS * 4];
        if (!vox_tex_load("../voxel-render/texture/tiles", tiles)) {
            memset(tiles, 0x80, sizeof tiles);
            printf("tiles: fallback gray\n");
        } else {
            printf("tiles OK: 7 layers\n");
        }
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
        c->tileTex = wgpuDeviceCreateTexture(c->device, &td);
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
        c->tileSmp = wgpuDeviceCreateSampler(c->device, &sd);
        static uint8_t staging[256 * VOX_TILE * VOX_LAYERS];
        for (int l = 0; l < VOX_LAYERS; l++)
            for (int y = 0; y < VOX_TILE; y++)
                memcpy(&staging[((size_t)l * VOX_TILE + (size_t)y) * 256],
                       &tiles[((size_t)l * VOX_TILE + (size_t)y) * VOX_TILE * 4], VOX_TILE * 4);
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
        wgpuQueueWriteTexture(c->queue, &dst, staging, sizeof staging, &layout, &extent);
    }
    sky_luts_init(&c->sky, c->device, c->queue);
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
        c->skyBind = wgpuDeviceCreateBindGroup(c->device, &d);
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
    wgpuQueueWriteTexture(c->queue, &dst, staging, sizeof staging, &layout, &extent);
    c->slotCX[sx][sz] = cx;
    c->slotCZ[sx][sz] = cz;
    c->slotOk[sx][sz] = 1;
    c->tags[(sz * VOX_STREAM_CH + sx) * 4] = cx;
    c->tags[(sz * VOX_STREAM_CH + sx) * 4 + 1] = cz;
    wgpuQueueWriteBuffer(c->queue, c->tagsBuf, 0, c->tags, sizeof c->tags);
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
    wgpuQueueWriteTexture(c->queue, &dst, air, sizeof air, &layout, &extent);
    c->slotOk[sx][sz] = 0;
    c->tags[(sz * VOX_STREAM_CH + sx) * 4] = INT32_MAX; // сентинел: мимо
    c->tags[(sz * VOX_STREAM_CH + sx) * 4 + 1] = INT32_MAX;
    wgpuQueueWriteBuffer(c->queue, c->tagsBuf, 0, c->tags, sizeof c->tags);
}

static float core_fps(RenderCore *rc) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    return c->fpsEma;
}

static void core_frame(RenderCore *rc, const RcView *v) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    if (!c->ready) {
#ifndef __EMSCRIPTEN__
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, 0);
#endif
        if (!c->adapterDone) return;
        if (!c->adapter) return;
        if (!c->deviceDone) {
            static int asked = 0;
            if (!asked) {
                asked = 1;
                WGPUDeviceDescriptor ddef;
                memset(&ddef, 0, sizeof ddef);
                ddef.label = (WGPUStringView){"sdf device", 10};
                WGPURequestDeviceCallbackInfo dci;
                memset(&dci, 0, sizeof dci);
                dci.mode = CB_MODE;
                dci.callback = onDevice;
                dci.userdata1 = c;
                wgpuAdapterRequestDevice(c->adapter, &ddef, dci);
            }
            return;
        }
        if (!c->device) return;
        c->queue = wgpuDeviceGetQueue(c->device);
        core_build_all(c, v);
        return;
    }

    int ww = v->resW, hh = v->resH;
    if (ww <= 0) ww = 1280;
    if (hh <= 0) hh = 720;
    if ((uint32_t)ww != c->cfg.width || (uint32_t)hh != c->cfg.height) {
        c->cfg.width = (uint32_t)ww;
        c->cfg.height = (uint32_t)hh;
        wgpuSurfaceConfigure(c->surface, &c->cfg);
    }
    double now = glfwGetTime();
    float dt = (float)(now - c->prevT);
    c->prevT = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        c->fpsEma = c->fpsEma > 0.0f ? c->fpsEma * 0.95f + fps * 0.05f : fps;
    }

    Vec3 f, r, u_;
    cam_basis(v->yaw, v->pitch, &f, &r, &u_);
    Vec3 sunDir = sdf_sun(v->dayT);
    Vec3 moonDir = v3(-sunDir.x, -sunDir.y, -sunDir.z);
    sky_luts_update(&c->sky, sunDir, moonDir, v->camPos.y);

    SdfUBO u;
    memset(&u, 0, sizeof u);
    u.camPos = v->camPos; u.time = v->time;
    u.camTarget = v3_add(v->camPos, f); u.resX = (float)ww;
    u.sunDir = sunDir; u.maxSteps = v->maxSteps;
    u.resY = (float)hh; u.mode = (float)v->viewMode;
    u.pad[0] = c->streamOX; u.pad[1] = c->streamOZ;
    int fi = c->frame % FRAMES_IN_FLIGHT;
    wgpuQueueWriteBuffer(c->queue, c->ubo[fi], 0, &u, sizeof u);
    {
        float g[4] = {v->gamma, v->exposure, v->fog,
                      v->menuOpen ? (float)v->menuSel : -1.0f};
        wgpuQueueWriteBuffer(c->queue, c->gradeBuf, 0, g, sizeof g);
        float vw[4] = {v->fov, v->shadowOn, v->frustumOn ? 1.0f : 0.0f, 0};
        wgpuQueueWriteBuffer(c->queue, c->viewBuf, 0, vw, sizeof vw);
        float cp = cosf(v->mainYaw), sp = sinf(v->mainYaw);
        float cq = cosf(v->mainPitch), sq = sinf(v->mainPitch);
        Vec3 mf = v3(cq * cp, sq, cq * sp);
        Vec3 mr = v3_norm(v3_cross(mf, v3(0.0f, 1.0f, 0.0f)));
        Vec3 mu = v3_cross(mr, mf);
        float fr[16] = {
            v->mainPos.x, v->mainPos.y, v->mainPos.z, 0,
            mf.x, mf.y, mf.z, 0,
            mr.x, mr.y, mr.z, 0,
            mu.x, mu.y, mu.z, 0,
        };
        wgpuQueueWriteBuffer(c->queue, c->frustumBuf, 0, fr, sizeof fr);
    }

    WGPUSurfaceTexture st;
    memset(&st, 0, sizeof st);
    wgpuSurfaceGetCurrentTexture(c->surface, &st);
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

    WGPUCommandEncoderDescriptor edef;
    memset(&edef, 0, sizeof edef);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(c->device, &edef);
    WGPURenderPassColorAttachment ca;
    memset(&ca, 0, sizeof ca);
    ca.view = view;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0.02, 0.03, 0.06, 1.0};
    WGPURenderPassDescriptor rp;
    memset(&rp, 0, sizeof rp);
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, c->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, c->bind[fi], 0, 0);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, c->skyBind, 0, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor cbdef;
    memset(&cbdef, 0, sizeof cbdef);
    WGPUCommandBuffer cbuf = wgpuCommandEncoderFinish(enc, &cbdef);
    wgpuQueueSubmit(c->queue, 1, &cbuf);
    wgpuSurfacePresent(c->surface);

    wgpuCommandBufferRelease(cbuf);
    wgpuCommandEncoderRelease(enc);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);
    c->frame++;
}

static int core_init(RenderCore *rc, void *glfwWindow) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    memset(c, 0, sizeof *c);
    c->win = (GLFWwindow *)glfwWindow;
    c->inst = wgpuCreateInstance(0);
    if (!c->inst) { fprintf(stderr, "instance fail\n"); return 0; }
    c->surface = sdf_create_surface(c->inst, c->win);
    if (!c->surface) { fprintf(stderr, "surface fail\n"); return 0; }
    WGPURequestAdapterOptions aopt;
    memset(&aopt, 0, sizeof aopt);
    aopt.powerPreference = WGPUPowerPreference_HighPerformance;
    aopt.compatibleSurface = c->surface;
    aopt.featureLevel = WGPUFeatureLevel_Core;
    WGPURequestAdapterCallbackInfo aci;
    memset(&aci, 0, sizeof aci);
    aci.mode = CB_MODE;
    aci.callback = onAdapter;
    aci.userdata1 = c;
    wgpuInstanceRequestAdapter(c->inst, &aopt, aci);
    c->prevT = glfwGetTime();
    return 1;
}

static void core_shutdown(RenderCore *rc) {
    VoxCore *c = &((VoxCoreWrap *)rc->ctx)->core;
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        wgpuBindGroupRelease(c->bind[i]);
        wgpuBufferRelease(c->ubo[i]);
    }
    wgpuBindGroupLayoutRelease(c->bgl);
    wgpuRenderPipelineRelease(c->pipeline);
    wgpuShaderModuleRelease(c->mod);
    wgpuSurfaceUnconfigure(c->surface);
    wgpuQueueRelease(c->queue);
    wgpuDeviceRelease(c->device);
    wgpuAdapterRelease(c->adapter);
    wgpuSurfaceRelease(c->surface);
    wgpuInstanceRelease(c->inst);
}

RenderCore *rc_webgpu_create(void) {
    VoxCoreWrap *w = (VoxCoreWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_VOXEL; // DDA-луч по вокселям, других входов нет
    w->api.name = "webgpu";
    w->api.init = core_init;
    w->api.shutdown = core_shutdown;
    w->api.frame = core_frame;
    w->api.upload_chunk = core_upload_chunk;
    w->api.set_origin = core_set_origin;
    w->api.unload_chunk = core_unload_chunk;
    w->api.fps = core_fps;
    return &w->api;
}

RenderCore *rc_create(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "webgpu")) return rc_webgpu_create();
#ifndef __EMSCRIPTEN__
    if (!strcmp(name, "gl")) return rc_gl_create();
    if (!strcmp(name, "vk")) return rc_vk_create();
#else
    (void)rc_gl_create;
#endif
    return 0;
}

void rc_destroy(RenderCore *rc) {
    free(rc->ctx);
}
