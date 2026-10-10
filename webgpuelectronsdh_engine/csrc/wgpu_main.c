// SDF-движок: один C-исходник — натив (while) + веб (set_main_loop).
// Лестница сцены в shaders/sdf.wgsl. Сюда правим только трубу и ввод.
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "sdf_surface.h"
#include "sdf_math.h"
#include "sdf_ubo.h"
#include "sdf_scene.h"
#include "sdf_gpu.h"
#include "sdf_settings.h"
#include "sky_lut.h"
#include "vox/vox_gen.h"
#include "vox/vox_world.h"
#include "vox/vox_tex.h"
#include "sdf_wgsl.h"

#define VOX_STREAM_CH 11 // окно 11x11 чанков = 176 клеток (кольцо R=4 + борт)
#define VOX_STEX (VOX_STREAM_CH * VOX_SX)
#define VOX_UP_BUDGET 12 // заливок чанков за кадр (16КБ шт, дёшево)

#define FRAMES_IN_FLIGHT 2 // ring UBO: GPU читает свой, CPU пишет свой (иначе разрыв кадра на UMA)

#ifdef __EMSCRIPTEN__
#define CB_MODE WGPUCallbackMode_AllowSpontaneous
#else
#define CB_MODE WGPUCallbackMode_WaitAnyOnly
#endif

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
    WGPUBuffer tagsBuf; // 121 пара (cx,cz) реально залитых чанков, сентинел = воздух
    SdfSettings settings;
    WGPUBuffer gradeBuf; // 16Б: gamma, exposure, fog
    WGPUBuffer viewBuf;  // 16Б: fov, shadow, frustumOn
    WGPUBuffer frustumBuf; // 64Б: mainPos/Fwd/Right/Up для дебаг-вида
    Vec3 mainPos;          // замороженная главная камера (дебаг летает сам)
    double mainYaw, mainPitch;
    int debugCam; // 1 — летим дебагом (F1), мир смотрим со стороны
    int ctrlHeld; // 1 — в дебаге ввод едет фрикамом, иначе главной (вид всегда от фри)
    time_t cfgMtime; // дозор settings.cfg (пишет страница Electron)
    WGPUBindGroupLayout bgl;
    SkyLuts sky;
    WGPUBindGroup skyBind;
    WGPUTexture voxTex;
    WGPUTextureView voxView;
    WGPUTexture tileTex;
    WGPUTextureView tileView;
    WGPUSampler tileSmp;
    VoxWorld world;
    float streamOX, streamOZ; // мировая клетка texel (0,*,0) — в UBO pad0/pad1
    int mode; // дебаг-вид: 0 цвет, 1 нормали, 2 глубина (клавиша N)
    int menuOpen; // меню настроек (Tab)
    int menuSel;  // выбранная строка 0..2
    volatile int uboBusy[FRAMES_IN_FLIGHT]; // забор: слот занят, пока GPU не отработал кадр
    int ready; // труба собрана
    float fpsEma; // сглаженный fps для губернатора шагов (идея из B)
    float maxSteps; // текущий лимит марша: 100 -> 25 по просадке, обратно по запасу
    Vec3 camPos;
    double yaw, pitch, speed;
    double t0, prevT, lastLog;
    double dayT;   // часы солнца (T — перемотка x36 как у них)
    double cloudT; // часы облаков/мерцания: реальный dt всегда (их cloud_time += dt)
    double timeScale;
    float dtMax;   // диагностика: худший dt за окно лога
    int rebakes;   // диагностика: ребейков неба за окно лога
    int frame;
    int maxFrames;
} App;

static void onAdapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg,
                      void *u1, void *u2) {
    (void)u2;
    App *app = (App *)u1;
    if (st != WGPURequestAdapterStatus_Success)
        fprintf(stderr, "adapter fail: %.*s\n", (int)msg.length, msg.data);
    else
        app->adapter = a;
    app->adapterDone = 1;
}

static void onDevice(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg,
                     void *u1, void *u2) {
    (void)u2;
    App *app = (App *)u1;
    if (st != WGPURequestDeviceStatus_Success)
        fprintf(stderr, "device fail: %.*s\n", (int)msg.length, msg.data);
    else
        app->device = d;
    app->deviceDone = 1;
}

#ifndef __EMSCRIPTEN__
// wgpu-native: ProcessEvents не реализован — ждём слипом.
static void wait_for(volatile int *done) {
    struct timespec ts = {0, 2000000};
    for (int i = 0; i < 5000 && !*done; i++) nanosleep(&ts, 0);
}
#endif

static double g_lx, g_ly;
static double g_sens = 0.0012;
static int g_locked = 0;
static App *g_app = 0; // ввод правит камеру приложения

static void set_locked(GLFWwindow *w, int locked) {
    g_locked = locked;
    glfwSetInputMode(w, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    int ww, hh;
    glfwGetWindowSize(w, &ww, &hh);
    g_lx = ww * 0.5; g_ly = hh * 0.5;
}
#define CAM_RADIUS 0.25f // толщина камеры: центр не подходит к стене ближе

// AABB: ни один из 4 углов не должен залезть в стену
static int check_aabb(float x, float z, float y) {
    return vox_floor(x - CAM_RADIUS, z - CAM_RADIUS, y) &&
           vox_floor(x + CAM_RADIUS, z - CAM_RADIUS, y) &&
           vox_floor(x - CAM_RADIUS, z + CAM_RADIUS, y) &&
           vox_floor(x + CAM_RADIUS, z + CAM_RADIUS, y);
}

static int g_slotCX[VOX_STREAM_CH][VOX_STREAM_CH];
static int g_slotCZ[VOX_STREAM_CH][VOX_STREAM_CH];
static unsigned char g_slotOk[VOX_STREAM_CH][VOX_STREAM_CH];

static int wrap11(int c) { int r = c % VOX_STREAM_CH; return r < 0 ? r + VOX_STREAM_CH : r; }

// Синк кольца в тороид: ensure + заливка грязных столбцов (бюджет).
// Всегда: origin = pcx-5 (окно 11 вокруг игрока).
static void vox_stream_sync(App *app, int pcx, int pcz) {
    vox_world_ensure(&app->world, pcx, pcz);
    int ox = pcx - 5, oz = pcz - 5;
    app->streamOX = (float)(ox * VOX_SX);
    app->streamOZ = (float)(oz * VOX_SZ);
    static uint8_t staging[256 * VOX_SY * VOX_SZ];
    static int32_t tags[484];
    static int tagsInit = 0;
    if (!tagsInit) {
        for (int i = 0; i < 484; i++) tags[i] = INT32_MAX;
        tagsInit = 1;
    }
    // Ближние первыми: проход кольцами, дальние ждут (дыр рядом с игроком нет).
    int up = 0;
    int tagsDirty = 0;
    for (int r = 0; r <= VOX_RADIUS + 1 && up < VOX_UP_BUDGET; r++) {
    for (int i = 0; i < VOX_POOL && up < VOX_UP_BUDGET; i++) {
        VoxSlot *s = &app->world.slots[i];
        if (!s->used) continue;
        int dx = s->cx - ox, dz = s->cz - oz;
        if (dx < 0 || dz < 0 || dx >= VOX_STREAM_CH || dz >= VOX_STREAM_CH) continue;
        int dcx = s->cx - pcx, dcz = s->cz - pcz;
        if (dcx < 0) dcx = -dcx;
        if (dcz < 0) dcz = -dcz;
        if ((dcx > dcz ? dcx : dcz) != r) continue;
        int sx = wrap11(s->cx), sz = wrap11(s->cz);
        if (g_slotOk[sx][sz] && g_slotCX[sx][sz] == s->cx && g_slotCZ[sx][sz] == s->cz) continue;
        for (int y = 0; y < VOX_SY; y++)
            for (int z = 0; z < VOX_SZ; z++)
                memcpy(&staging[((size_t)z * VOX_SY + (size_t)y) * 256],
                       &s->data.id[((size_t)y * VOX_SZ + (size_t)z) * VOX_SX], VOX_SX);
        WGPUTexelCopyTextureInfo dst;
        memset(&dst, 0, sizeof dst);
        dst.texture = app->voxTex;
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
        wgpuQueueWriteTexture(app->queue, &dst, staging, sizeof staging, &layout, &extent);
        g_slotCX[sx][sz] = s->cx;
        g_slotCZ[sx][sz] = s->cz;
        g_slotOk[sx][sz] = 1;
        tags[(sz * VOX_STREAM_CH + sx) * 4] = s->cx;
        tags[(sz * VOX_STREAM_CH + sx) * 4 + 1] = s->cz;
        tagsDirty = 1;
        up++;
    }
    }
    if (tagsDirty)
        wgpuQueueWriteBuffer(app->queue, app->tagsBuf, 0, tags, sizeof tags);
}

static void on_mouse(GLFWwindow *w, double x, double y) {
    (void)w;
    if (!g_locked || !g_app) { g_lx = x; g_ly = y; return; }
    // В дебаге без Ctrl мышь крутит главную (вслепую), вид от свободной.
    double *YW = &g_app->yaw, *PT = &g_app->pitch;
    if (g_app->debugCam && !g_app->ctrlHeld) { YW = &g_app->mainYaw; PT = &g_app->mainPitch; }
    *YW += (x - g_lx) * g_sens;
    *PT -= (y - g_ly) * g_sens;
    if (*PT > 1.45) *PT = 1.45;
    if (*PT < -1.45) *PT = -1.45;
    g_lx = x; g_ly = y;
}
static void on_btn(GLFWwindow *w, int b, int act, int m) {
    (void)m;
    if (b != GLFW_MOUSE_BUTTON_LEFT || act != GLFW_PRESS) return;
    if (!g_locked && g_app && !g_app->menuOpen) { set_locked(w, 1); return; }
    // Клик по меню: строка по y (16..136 шаг 40), значение по x (120..280).
    if (g_app && g_app->menuOpen) {
        double cx, cy;
        glfwGetCursorPos(w, &cx, &cy);
        int ww, hh, fw, fh;
        glfwGetWindowSize(w, &ww, &hh);
        glfwGetFramebufferSize(w, &fw, &fh);
        double px = ww > 0 ? cx * fw / ww : cx;
        double py = hh > 0 ? cy * fh / hh : cy;
        if (px >= 24 && px < 560 && py >= 24 && py < 308) {
            int row = (int)((py - 24) / 56);
            if (row < 0) row = 0;
            if (row > 4) row = 4;
            g_app->menuSel = row;
            if (row == 4) {
                g_app->settings.shadow = g_app->settings.shadow >= 0.5f ? 0.0f : 1.0f;
            } else if (px >= 190 && px < 350) {
                double f = (px - 190) / 160;
                if (f < 0) f = 0;
                if (f > 1) f = 1;
                if (row == 0) g_app->settings.gamma = (float)(0.5 + f * 3.5);
                else if (row == 1) g_app->settings.exposure = (float)(0.1 + f * 3.9);
                else if (row == 2) g_app->settings.fog = (float)(f * 3.0);
                else g_app->settings.fov = (float)(0.5 + f * 3.5);
            }
        }
    }
}
static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    if (!g_app) return;
    g_app->speed *= (1.0 + (dy > 0 ? 0.15 : dy < 0 ? -0.15 : 0.0));
    if (g_app->speed < 1.0) g_app->speed = 1.0;
    if (g_app->speed > 12.0) g_app->speed = 12.0;
}

static WGPUShaderModule make_module(WGPUDevice device) {
    WGPUShaderSourceWGSL wgsl;
    memset(&wgsl, 0, sizeof wgsl);
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){SDF_WGSL, strlen(SDF_WGSL)};
    WGPUShaderModuleDescriptor def;
    memset(&def, 0, sizeof def);
    def.nextInChain = (const WGPUChainedStruct *)&wgsl;
    return wgpuDeviceCreateShaderModule(device, &def);
}

// Один кадр. Возвращает 1 когда пора выходить (только натив).
static int app_frame(App *app) {
    glfwPollEvents();
    if (glfwGetKey(app->win, GLFW_KEY_ESCAPE) == GLFW_PRESS && g_locked) set_locked(app->win, 0);

    // GPU ещё нет: запросы уже летят, ждём флагов (на вебе колбэки между кадрами).
    if (!app->ready) {
#ifndef __EMSCRIPTEN__
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, 0);
#endif
        if (!app->adapterDone) return 0;
        if (!app->adapter) return 0;
        if (!app->deviceDone) {
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
                dci.userdata1 = app;
                wgpuAdapterRequestDevice(app->adapter, &ddef, dci);
            }
            return 0;
        }
        if (!app->device) return 0;
        app->queue = wgpuDeviceGetQueue(app->device);
        WGPUSurfaceCapabilities caps;
        memset(&caps, 0, sizeof caps);
        wgpuSurfaceGetCapabilities(app->surface, app->adapter, &caps);
        app->fmt = caps.formats[0];
        memset(&app->cfg, 0, sizeof app->cfg);
        app->cfg.device = app->device;
        app->cfg.format = app->fmt;
        app->cfg.usage = WGPUTextureUsage_RenderAttachment;
        app->cfg.width = 1280;
        app->cfg.height = 720;
        app->cfg.presentMode = WGPUPresentMode_Mailbox; // Immediate не поддерживается ([Mailbox, Fifo])
        app->cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
        wgpuSurfaceConfigure(app->surface, &app->cfg);
        app->mod = make_module(app->device);
        WGPUBufferDescriptor bdef;
        memset(&bdef, 0, sizeof bdef);
        bdef.size = 64;
        bdef.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
            app->ubo[i] = wgpuDeviceCreateBuffer(app->device, &bdef);
        WGPUColorTargetState color;
        memset(&color, 0, sizeof color);
        color.format = app->fmt;
        color.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState frag;
        memset(&frag, 0, sizeof frag);
        frag.module = app->mod;
        frag.entryPoint = (WGPUStringView){"fs", 2};
        frag.targetCount = 1;
        frag.targets = &color;
        WGPUVertexState vert;
        memset(&vert, 0, sizeof vert);
        vert.module = app->mod;
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
        app->pipeline = wgpuDeviceCreateRenderPipeline(app->device, &pipe);
        app->bgl = wgpuRenderPipelineGetBindGroupLayout(app->pipeline, 0);
        // Теги слотов: сентинел = незалито.
        {
            WGPUBufferDescriptor td;
            memset(&td, 0, sizeof td);
            td.size = 1936;
            td.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            app->tagsBuf = wgpuDeviceCreateBuffer(app->device, &td);
            static int32_t sentinel[484];
            static int sentinelInit = 0;
            if (!sentinelInit) {
                for (int i = 0; i < 484; i++) sentinel[i] = INT32_MAX;
                sentinelInit = 1;
            }
            wgpuQueueWriteBuffer(app->queue, app->tagsBuf, 0, sentinel, sizeof sentinel);
        }
        // Грейд: settings.cfg + 16Б юниформ.
        sdf_settings_load(&app->settings, "settings.cfg");
        {
            WGPUBufferDescriptor gd;
            memset(&gd, 0, sizeof gd);
            gd.size = 16;
            gd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            app->gradeBuf = wgpuDeviceCreateBuffer(app->device, &gd);
            app->viewBuf = wgpuDeviceCreateBuffer(app->device, &gd);
            {
                WGPUBufferDescriptor fd;
                memset(&fd, 0, sizeof fd);
                fd.size = 64;
                fd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
                app->frustumBuf = wgpuDeviceCreateBuffer(app->device, &fd);
            }
            float g[4] = {app->settings.gamma, app->settings.exposure, app->settings.fog, 0};
            wgpuQueueWriteBuffer(app->queue, app->gradeBuf, 0, g, sizeof g);
            float vw[4] = {app->settings.fov, app->settings.shadow, 0, 0};
            wgpuQueueWriteBuffer(app->queue, app->viewBuf, 0, vw, sizeof vw);
            printf("grade: gamma=%.2f exposure=%.2f fog=%.2f fov=%.2f shadow=%.0f (Tab-меню)\n",
                app->settings.gamma, app->settings.exposure, app->settings.fog,
                app->settings.fov, app->settings.shadow);
        }
        for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
            WGPUBindGroupEntry be[5];
            memset(be, 0, sizeof be);
            be[0].binding = 0;
            be[0].buffer = app->ubo[i];
            be[0].size = 64;
            be[1].binding = 1;
            be[1].buffer = app->tagsBuf;
            be[1].size = 1936;
            be[2].binding = 2;
            be[2].buffer = app->gradeBuf;
            be[2].size = 16;
            be[3].binding = 3;
            be[3].buffer = app->viewBuf;
            be[3].size = 16;
            be[4].binding = 4;
            be[4].buffer = app->frustumBuf;
            be[4].size = 64;
            WGPUBindGroupDescriptor bgdef;
            memset(&bgdef, 0, sizeof bgdef);
            bgdef.layout = app->bgl;
            bgdef.entryCount = 5;
            bgdef.entries = be;
            app->bind[i] = wgpuDeviceCreateBindGroup(app->device, &bgdef);
        }
        // Тороидальная 3D-текстура 176x64x176 (11x11 чанков): заливка — по грязным
        // столбцам каждый кадр (см. vox_stream_sync ниже), wrap — на стороне C.
        {
            WGPUTextureDescriptor td;
            memset(&td, 0, sizeof td);
            td.size.width = 176;
            td.size.height = VOX_SY;
            td.size.depthOrArrayLayers = 176;
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_3D;
            td.format = WGPUTextureFormat_R8Uint;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            app->voxTex = wgpuDeviceCreateTexture(app->device, &td);
            WGPUTextureViewDescriptor vd;
            memset(&vd, 0, sizeof vd);
            vd.format = WGPUTextureFormat_R8Uint;
            vd.dimension = WGPUTextureViewDimension_3D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            vd.aspect = WGPUTextureAspect_All;
            app->voxView = wgpuTextureCreateView(app->voxTex, &vd);
            vox_world_init(&app->world, 1337);
            printf("voxels OK: stream 176x64x176 (11x11 chunks)\n");
        }
        // Атлас блоков 16x16x7 (их тайлы). Нет файлов — серая заглушка, не падаем.
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
            app->tileTex = wgpuDeviceCreateTexture(app->device, &td);
            WGPUTextureViewDescriptor vd;
            memset(&vd, 0, sizeof vd);
            vd.format = WGPUTextureFormat_RGBA8Unorm;
            vd.dimension = WGPUTextureViewDimension_2DArray;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = VOX_LAYERS;
            vd.aspect = WGPUTextureAspect_All;
            app->tileView = wgpuTextureCreateView(app->tileTex, &vd);
            WGPUSamplerDescriptor sd;
            memset(&sd, 0, sizeof sd);
            sd.addressModeU = WGPUAddressMode_ClampToEdge;
            sd.addressModeV = WGPUAddressMode_ClampToEdge;
            sd.addressModeW = WGPUAddressMode_ClampToEdge;
            sd.magFilter = WGPUFilterMode_Nearest;
            sd.minFilter = WGPUFilterMode_Nearest;
            sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            sd.maxAnisotropy = 1;
            app->tileSmp = wgpuDeviceCreateSampler(app->device, &sd);
            static uint8_t staging[256 * VOX_TILE * VOX_LAYERS];
            for (int l = 0; l < VOX_LAYERS; l++)
                for (int y = 0; y < VOX_TILE; y++)
                    memcpy(&staging[((size_t)l * VOX_TILE + (size_t)y) * 256],
                           &tiles[((size_t)l * VOX_TILE + (size_t)y) * VOX_TILE * 4], VOX_TILE * 4);
            WGPUTexelCopyTextureInfo dst;
            memset(&dst, 0, sizeof dst);
            dst.texture = app->tileTex;
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
            wgpuQueueWriteTexture(app->queue, &dst, staging, sizeof staging, &layout, &extent);
        }
        // Печка неба + вторая бинд-группа (LUT): текстуры фиксированы.
        sky_luts_init(&app->sky, app->device, app->queue);
        {
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(app->pipeline, 1);
            WGPUBindGroupEntry e[6];
            memset(e, 0, sizeof e);
            e[0].binding = 0; e[0].textureView = app->sky.sunView;
            e[1].binding = 1; e[1].textureView = app->sky.moonView;
            e[2].binding = 2; e[2].sampler = app->sky.smp;
            e[3].binding = 3; e[3].textureView = app->voxView;
            e[4].binding = 4; e[4].textureView = app->tileView;
            e[5].binding = 5; e[5].sampler = app->tileSmp;
            WGPUBindGroupDescriptor d;
            memset(&d, 0, sizeof d);
            d.layout = l;
            d.entryCount = 6;
            d.entries = e;
            app->skyBind = wgpuDeviceCreateBindGroup(app->device, &d);
            wgpuBindGroupLayoutRelease(l);
        }
        app->ready = 1;
        printf("pipe OK: sdf.wgsl -> triangle + UBO\n");
        return 0;
    }

    int ww, hh;
    glfwGetFramebufferSize(app->win, &ww, &hh);
    if ((uint32_t)ww != app->cfg.width || (uint32_t)hh != app->cfg.height) {
        app->cfg.width = (uint32_t)ww;
        app->cfg.height = (uint32_t)hh;
        wgpuSurfaceConfigure(app->surface, &app->cfg);
    }
    double now = glfwGetTime();
    double frameStart = now;
    double t = now - app->t0;
    float dt = (float)(now - app->prevT);
    app->prevT = now;
    static int nPrev = 0, f1Prev = 0;
    int nDown = glfwGetKey(app->win, GLFW_KEY_N) == GLFW_PRESS;
    if (nDown && !nPrev) { app->mode = (app->mode + 1) % 4; printf("view mode=%d\n", app->mode); }
    nPrev = nDown;
    int ctrlHeld = glfwGetKey(app->win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(app->win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    app->ctrlHeld = ctrlHeld;
    // F1: дебаг-камера. Вкл — главная замирает, летим сами; выкл — возврат.
    int f1D = glfwGetKey(app->win, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1D && !f1Prev) {
        app->debugCam = !app->debugCam;
        if (app->debugCam) {
            app->mainPos = app->camPos;
            app->mainYaw = app->yaw;
            app->mainPitch = app->pitch;
            printf("debug cam: лети, главная заморожена (F1 назад)\n");
        } else {
            app->camPos = app->mainPos;
            app->yaw = app->mainYaw;
            app->pitch = app->mainPitch;
            printf("debug cam: выкл, возврат\n");
        }
    }
    f1Prev = f1D;
    // Меню настроек (Tab): стрелки вместо хоткеев. Открыто — курсор свободен.
    static int tabPrev = 0;
    int tabDown = glfwGetKey(app->win, GLFW_KEY_TAB) == GLFW_PRESS;
    if (tabDown && !tabPrev) {
        app->menuOpen = !app->menuOpen;
        if (app->menuOpen) { set_locked(app->win, 0); }
        else {
            set_locked(app->win, 1);
            sdf_settings_save(&app->settings, "settings.cfg");
            printf("grade: saved gamma=%.2f exposure=%.2f fog=%.2f fov=%.2f shadow=%.0f\n",
                app->settings.gamma, app->settings.exposure, app->settings.fog,
                app->settings.fov, app->settings.shadow);
        }
    }
    tabPrev = tabDown;
    if (app->menuOpen) {
        static int upPrev = 0, dnPrev = 0;
        int upD = glfwGetKey(app->win, GLFW_KEY_UP) == GLFW_PRESS;
        int dnD = glfwGetKey(app->win, GLFW_KEY_DOWN) == GLFW_PRESS;
        if (upD && !upPrev) { app->menuSel = (app->menuSel + 4) % 5; }
        if (dnD && !dnPrev) { app->menuSel = (app->menuSel + 1) % 5; }
        upPrev = upD; dnPrev = dnD;
        // Удержание = плавно (1 ед/с). Строка 4 — тумблер по любому нажатию.
        float rate = dt * 1.0f;
        static int lfPrev = 0, rtPrev = 0;
        int lfD = glfwGetKey(app->win, GLFW_KEY_LEFT) == GLFW_PRESS;
        int rtD = glfwGetKey(app->win, GLFW_KEY_RIGHT) == GLFW_PRESS;
        int lfE = lfD && !lfPrev, rtE = rtD && !rtPrev;
        lfPrev = lfD; rtPrev = rtD;
        if (app->menuSel == 4) {
            if (lfE || rtE) app->settings.shadow = app->settings.shadow >= 0.5f ? 0.0f : 1.0f;
        } else if (lfD || rtD) {
            float d = ((lfD ? -1.0f : 0.0f) + (rtD ? 1.0f : 0.0f)) * rate;
            if (app->menuSel == 0) app->settings.gamma += d;
            else if (app->menuSel == 1) app->settings.exposure += d;
            else if (app->menuSel == 2) app->settings.fog += d;
            else app->settings.fov += d;
        }
        if (app->settings.gamma < 0.5f) app->settings.gamma = 0.5f;
        if (app->settings.gamma > 4.0f) app->settings.gamma = 4.0f;
        if (app->settings.exposure < 0.1f) app->settings.exposure = 0.1f;
        if (app->settings.exposure > 4.0f) app->settings.exposure = 4.0f;
        if (app->settings.fog < 0.0f) app->settings.fog = 0.0f;
        if (app->settings.fog > 3.0f) app->settings.fog = 3.0f;
        if (app->settings.fov < 0.5f) app->settings.fov = 0.5f;
        if (app->settings.fov > 4.0f) app->settings.fov = 4.0f;
    }
    if (dt > 0.5f) dt = 0.5f; // кламп широкий: истинный шип должен быть виден в dtmax
    if (dt > app->dtMax) app->dtMax = dt;
    float logic_dt = dt > 0.033f ? 0.033f : dt; // физика без телепортов
    // Перемотка времени как у них: T вперёд x36, Shift+T назад (их wc_game.c:287).
    GLFWwindow *win = app->win;
    int tDown = glfwGetKey(win, GLFW_KEY_T) == GLFW_PRESS;
    int shDown = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    app->timeScale = tDown ? (shDown ? -36.0 : 36.0) : 1.0;
    app->dayT += logic_dt * app->timeScale;
    app->cloudT += logic_dt;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        app->fpsEma = app->fpsEma > 0.0f ? app->fpsEma * 0.95f + fps * 0.05f : fps;
        // Губернатор: держим >=45 fps шагами марша (Vega). Гистерезис: вниз быстро, вверх медленно.
        if (app->fpsEma < 45.0f && app->maxSteps > 25.0f) app->maxSteps -= 5.0f;
        else if (app->fpsEma > 57.0f && app->maxSteps < 100.0f) app->maxSteps += 1.0f;
    }

    // Цель ввода: в дебаге без Ctrl едет главная (вид от свободной), иначе активная.
    Vec3 *CP = &app->camPos;
    double *YW = &app->yaw, *PT = &app->pitch;
    if (app->debugCam && !app->ctrlHeld) { CP = &app->mainPos; YW = &app->mainYaw; PT = &app->mainPitch; }
    float cp = cosf((float)*PT);
    Vec3 fwd = v3(cp * cosf((float)*YW), sinf((float)*PT), cp * sinf((float)*YW));
    Vec3 right = v3_norm(v3_cross(fwd, v3(0.0f, 1.0f, 0.0f)));
    // Горизонталь отдельно от вертикали: W/S не втыкают в холм носом.
    Vec3 fh = v3_norm(v3(fwd.x, 0.0f, fwd.z));
    Vec3 rh = v3_norm(v3(right.x, 0.0f, right.z));
    float sp = (float)app->speed * logic_dt;
    Vec3 wish = v3(0.0f, 0.0f, 0.0f);
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) wish = v3_add(wish, v3_mul(fh, sp));
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) wish = v3_sub(wish, v3_mul(fh, sp));
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) wish = v3_add(wish, v3_mul(rh, sp));
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) wish = v3_sub(wish, v3_mul(rh, sp));
    // Скольжение вдоль холма: целиком -> только X -> только Z -> стоим.
    float cy0 = CP->y;
    float nx = CP->x + wish.x, nz = CP->z + wish.z;
    if (check_aabb(nx, nz, cy0)) {
        CP->x = nx;
        CP->z = nz;
    }
    else if (check_aabb(CP->x + wish.x, CP->z, cy0)) { CP->x += wish.x; }
    else if (check_aabb(CP->x, CP->z + wish.z, cy0)) { CP->z += wish.z; }
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) CP->y += sp;
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) CP->y -= sp;
    float cx = CP->x, cy = CP->y, cz = CP->z;
    // Пол: не ниже поверхности + 0.6 (только посадка, без телепортов вверх).
    float fl = vox_floor_y(cx, cz);
    if (cy < fl) cy = fl;
    CP->x = cx; CP->y = cy; CP->z = cz;
    // Рендер всегда от свободной/активной камеры вида.
    float rcx = app->camPos.x, rcy = app->camPos.y, rcz = app->camPos.z;

    SdfUBO u;
    memset(&u, 0, sizeof u);
    Vec3 sunDir = sdf_sun((float)app->dayT);
    Vec3 moonDir = v3(-sunDir.x, -sunDir.y, -sunDir.z);
    // Рендер и стриминг — от камеры вида (в дебаге это свободная).
    float rcp = cosf((float)app->pitch);
    Vec3 rfwd = v3(rcp * cosf((float)app->yaw), sinf((float)app->pitch), rcp * sinf((float)app->yaw));
    // Стриминг за игроком: чанк из позиции камеры (floor делит отрицательные верно).
    {
        int pcx = (int)floorf(rcx / 16.0f), pcz = (int)floorf(rcz / 16.0f);
        vox_stream_sync(app, pcx, pcz);
    }
    app->rebakes += sky_luts_update(&app->sky, sunDir, moonDir, rcy);
    u.camPos = v3(rcx, rcy, rcz); u.time = (float)app->cloudT;
    u.camTarget = v3_add(v3(rcx, rcy, rcz), rfwd); u.resX = (float)ww;
    u.sunDir = sunDir; u.maxSteps = app->maxSteps;
    u.resY = (float)hh; u.mode = (float)app->mode;
    u.pad[0] = app->streamOX; u.pad[1] = app->streamOZ;
    int fi = app->frame % FRAMES_IN_FLIGHT; // свой UBO на кадр (гигиена UMA)
#ifndef __EMSCRIPTEN__
    // NOTE: забора нет — wgpu-native не даёт помпы колбэков (ProcessEvents panic).
    // writeBuffer идёт строго по очереди, гонки через API нет.
    (void)fi;
#endif
    wgpuQueueWriteBuffer(app->queue, app->ubo[fi], 0, &u, sizeof u);
    // Грейд+вид каждый кадр: значения + состояние меню (w=-1 закрыто).
    {
        float g[4] = {app->settings.gamma, app->settings.exposure, app->settings.fog,
                      app->menuOpen ? (float)app->menuSel : -1.0f};
        wgpuQueueWriteBuffer(app->queue, app->gradeBuf, 0, g, sizeof g);
        float vw[4] = {app->settings.fov, app->settings.shadow, app->debugCam ? 1.0f : 0.0f, 0};
        wgpuQueueWriteBuffer(app->queue, app->viewBuf, 0, vw, sizeof vw);
        // Фрустум главной: поз/базис. Вне дебага не читается (гейт view.z).
        // main* догоняет текущую, пока не заморожена.
        if (!app->debugCam) {
            app->mainPos = app->camPos;
            app->mainYaw = app->yaw;
            app->mainPitch = app->pitch;
        }
        float cp = cosf((float)app->mainYaw), sp = sinf((float)app->mainYaw);
        float cq = cosf((float)app->mainPitch), sq = sinf((float)app->mainPitch);
        Vec3 mf = v3(cq * cp, sq, cq * sp);
        Vec3 mr = v3_norm(v3_cross(mf, v3(0.0f, 1.0f, 0.0f)));
        Vec3 mu = v3_cross(mr, mf);
        float fr[16] = {
            app->mainPos.x, app->mainPos.y, app->mainPos.z, 0,
            mf.x, mf.y, mf.z, 0,
            mr.x, mr.y, mr.z, 0,
            mu.x, mu.y, mu.z, 0,
        };
        wgpuQueueWriteBuffer(app->queue, app->frustumBuf, 0, fr, sizeof fr);
    }

    WGPUSurfaceTexture st;
    memset(&st, 0, sizeof st);
    wgpuSurfaceGetCurrentTexture(app->surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        fprintf(stderr, "getCurrentTexture status=%d\n", (int)st.status);
        return 1;
    }
    WGPUTextureViewDescriptor vdef;
    memset(&vdef, 0, sizeof vdef);
    vdef.format = app->fmt;
    vdef.dimension = WGPUTextureViewDimension_2D;
    vdef.mipLevelCount = 1;
    vdef.arrayLayerCount = 1;
    vdef.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, &vdef);

    WGPUCommandEncoderDescriptor edef;
    memset(&edef, 0, sizeof edef);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(app->device, &edef);
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
    wgpuRenderPassEncoderSetPipeline(pass, app->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app->bind[fi], 0, 0);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, app->skyBind, 0, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor cbdef;
    memset(&cbdef, 0, sizeof cbdef);
    WGPUCommandBuffer cbuf = wgpuCommandEncoderFinish(enc, &cbdef);
    wgpuQueueSubmit(app->queue, 1, &cbuf);
    wgpuSurfacePresent(app->surface);

    wgpuCommandBufferRelease(cbuf);
    wgpuCommandEncoderRelease(enc);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);

    if ((app->frame % 30) == 0) {
        struct stat st;
        if (stat("settings.cfg", &st) == 0 && st.st_mtime != app->cfgMtime) {
            app->cfgMtime = st.st_mtime;
            SdfSettings r;
            sdf_settings_load(&r, "settings.cfg");
            app->settings = r;
            printf("grade: reload gamma=%.2f exposure=%.2f fog=%.2f\n", r.gamma, r.exposure, r.fog);
        }
    }
    if (t - app->lastLog >= 4.0) {
        app->lastLog = t;
        printf("f=%d pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f spd=%.1f fps=%.0f steps=%.0f x%.0f dtmax=%.0fms rebake=%d%s\n",
            app->frame, rcx, rcy, rcz, app->yaw, app->pitch, app->speed, app->fpsEma, app->maxSteps, app->timeScale,
            app->dtMax * 1000.0f, app->rebakes, app->debugCam ? (app->ctrlHeld ? " DBG+ctrl" : " DBG") : "");
        app->dtMax = 0.0f;
        app->rebakes = 0;
    }
    app->frame++;
    if (app->maxFrames > 0 && app->frame >= app->maxFrames) return 1;
#ifndef __EMSCRIPTEN__
    // Лимит 60 fps: ноут не жарим. В тестах (--frames) не спим.
    if (app->maxFrames <= 0) {
        double elapsed = glfwGetTime() - frameStart;
        double want = 1.0 / 60.0;
        if (elapsed < want) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (long)((want - elapsed) * 1e9);
            nanosleep(&ts, 0);
        }
    }
#endif
    return glfwWindowShouldClose(win) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
static void frame_trampoline(void *arg) { app_frame((App *)arg); }
#endif

int main(int argc, char **argv) {
    static App app;
    memset(&app, 0, sizeof app);
    app.maxFrames = -1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) app.maxFrames = atoi(argv[++i]);

    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    app.win = glfwCreateWindow(1280, 720, "sdf — C/WebGPU", 0, 0);
    if (!app.win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    g_app = &app;
    glfwSetCursorPosCallback(app.win, on_mouse);
    glfwSetMouseButtonCallback(app.win, on_btn);
    glfwSetScrollCallback(app.win, on_scroll);
    set_locked(app.win, 1);

    app.inst = wgpuCreateInstance(0);
    if (!app.inst) { fprintf(stderr, "instance fail\n"); return 1; }
    app.surface = sdf_create_surface(app.inst, app.win);
    if (!app.surface) { fprintf(stderr, "surface fail\n"); return 1; }

    WGPURequestAdapterOptions aopt;
    memset(&aopt, 0, sizeof aopt);
    aopt.powerPreference = WGPUPowerPreference_HighPerformance;
    aopt.compatibleSurface = app.surface;
    aopt.featureLevel = WGPUFeatureLevel_Core;
    WGPURequestAdapterCallbackInfo aci;
    memset(&aci, 0, sizeof aci);
    aci.mode = CB_MODE;
    aci.callback = onAdapter;
    aci.userdata1 = &app;
    wgpuInstanceRequestAdapter(app.inst, &aopt, aci);

    app.camPos = v3(32.0f, 42.0f, 12.0f); // над патчем, взгляд в центр
    app.yaw = 2.16; app.pitch = -0.69; app.speed = 4.0;
    app.mode = 0; app.menuOpen = 0; app.menuSel = 0; app.debugCam = 0;
    app.dayT = 0.0; app.cloudT = 0.0; app.timeScale = 1.0;
    app.fpsEma = 0.0f; app.maxSteps = 100.0f;
    app.t0 = app.prevT = glfwGetTime();
    app.lastLog = -10.0;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame_trampoline, &app, 0, 1);
#else
    wait_for(&app.adapterDone);
    if (!app.adapter) { fprintf(stderr, "adapter timeout\n"); return 1; }
    while (!app_frame(&app)) { }
    printf("done: %d frames\n", app.frame);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        wgpuBindGroupRelease(app.bind[i]);
        wgpuBufferRelease(app.ubo[i]);
    }
    wgpuBindGroupLayoutRelease(app.bgl);
    wgpuRenderPipelineRelease(app.pipeline);
    wgpuShaderModuleRelease(app.mod);
    wgpuSurfaceUnconfigure(app.surface);
    wgpuQueueRelease(app.queue);
    wgpuDeviceRelease(app.device);
    wgpuAdapterRelease(app.adapter);
    wgpuSurfaceRelease(app.surface);
    wgpuInstanceRelease(app.inst);
    glfwDestroyWindow(app.win);
    glfwTerminate();
#endif
    return 0;
}
