// SDF-движок: один C-исходник — натив (while) + веб (set_main_loop).
// Лестница сцены в shaders/sdf.wgsl. Сюда правим только трубу и ввод.
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
#include "sdf_surface.h"
#include "sdf_math.h"
#include "sdf_ubo.h"
#include "sdf_scene.h"
#include "sdf_gpu.h"
#include "sky_lut.h"
#include "vox/vox_gen.h"
#include "vox/vox_world.h"
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
    WGPUBindGroupLayout bgl;
    SkyLuts sky;
    WGPUBindGroup skyBind;
    WGPUTexture voxTex;
    WGPUTextureView voxView;
    VoxWorld world;
    float streamOX, streamOZ; // мировая клетка texel (0,*,0) — в UBO pad0/pad1
    int mode; // дебаг-вид: 0 цвет, 1 нормали, 2 глубина (клавиша N)
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
    g_app->yaw += (x - g_lx) * g_sens;
    g_app->pitch -= (y - g_ly) * g_sens;
    if (g_app->pitch > 1.45) g_app->pitch = 1.45;
    if (g_app->pitch < -1.45) g_app->pitch = -1.45;
    g_lx = x; g_ly = y;
}
static void on_btn(GLFWwindow *w, int b, int act, int m) {
    (void)m;
    if (b == GLFW_MOUSE_BUTTON_LEFT && act == GLFW_PRESS && !g_locked) set_locked(w, 1);
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
        for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
            WGPUBindGroupEntry be[2];
            memset(be, 0, sizeof be);
            be[0].binding = 0;
            be[0].buffer = app->ubo[i];
            be[0].size = 64;
            be[1].binding = 1;
            be[1].buffer = app->tagsBuf;
            be[1].size = 1936;
            WGPUBindGroupDescriptor bgdef;
            memset(&bgdef, 0, sizeof bgdef);
            bgdef.layout = app->bgl;
            bgdef.entryCount = 2;
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
        // Печка неба + вторая бинд-группа (LUT): текстуры фиксированы.
        sky_luts_init(&app->sky, app->device, app->queue);
        {
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(app->pipeline, 1);
            WGPUBindGroupEntry e[4];
            memset(e, 0, sizeof e);
            e[0].binding = 0; e[0].textureView = app->sky.sunView;
            e[1].binding = 1; e[1].textureView = app->sky.moonView;
            e[2].binding = 2; e[2].sampler = app->sky.smp;
            e[3].binding = 3; e[3].textureView = app->voxView;
            WGPUBindGroupDescriptor d;
            memset(&d, 0, sizeof d);
            d.layout = l;
            d.entryCount = 4;
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
    static int nPrev = 0;
    int nDown = glfwGetKey(app->win, GLFW_KEY_N) == GLFW_PRESS;
    if (nDown && !nPrev) { app->mode = (app->mode + 1) % 3; printf("view mode=%d\n", app->mode); }
    nPrev = nDown;
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

    float cp = cosf((float)app->pitch);
    Vec3 fwd = v3(cp * cosf((float)app->yaw), sinf((float)app->pitch), cp * sinf((float)app->yaw));
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
    float cy0 = app->camPos.y;
    float nx = app->camPos.x + wish.x, nz = app->camPos.z + wish.z;
    if (check_aabb(nx, nz, cy0)) {
        app->camPos.x = nx;
        app->camPos.z = nz;
    }
    else if (check_aabb(app->camPos.x + wish.x, app->camPos.z, cy0)) { app->camPos.x += wish.x; }
    else if (check_aabb(app->camPos.x, app->camPos.z + wish.z, cy0)) { app->camPos.z += wish.z; }
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) app->camPos.y += sp;
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) app->camPos.y -= sp;
    float cx = app->camPos.x, cy = app->camPos.y, cz = app->camPos.z;
    // Пол: не ниже поверхности + 0.6 (только посадка, без телепортов вверх).
    float fl = vox_floor_y(cx, cz);
    if (cy < fl) cy = fl;
    app->camPos = v3(cx, cy, cz);

    SdfUBO u;
    memset(&u, 0, sizeof u);
    Vec3 sunDir = sdf_sun((float)app->dayT);
    Vec3 moonDir = v3(-sunDir.x, -sunDir.y, -sunDir.z);
    // Стриминг за игроком: чанк из позиции камеры (floor делит отрицательные верно).
    {
        int pcx = (int)floorf(cx / 16.0f), pcz = (int)floorf(cz / 16.0f);
        vox_stream_sync(app, pcx, pcz);
    }
    app->rebakes += sky_luts_update(&app->sky, sunDir, moonDir, cy);
    u.camPos = app->camPos; u.time = (float)app->cloudT;
    u.camTarget = v3_add(app->camPos, fwd); u.resX = (float)ww;
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

    if (t - app->lastLog >= 4.0) {
        app->lastLog = t;
        printf("f=%d pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f spd=%.1f fps=%.0f steps=%.0f x%.0f dtmax=%.0fms rebake=%d\n",
            app->frame, cx, cy, cz, app->yaw, app->pitch, app->speed, app->fpsEma, app->maxSteps, app->timeScale,
            app->dtMax * 1000.0f, app->rebakes);
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
    app.mode = 0;
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
