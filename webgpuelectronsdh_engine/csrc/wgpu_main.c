// SDF-движок: один C-исходник — натив (while) + веб (set_main_loop).
// Лестница сцены в shaders/sdf.wgsl. Сюда правим только трубу и ввод.
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "sdf_wgsl.h"

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
    WGPUBuffer ubo;
    WGPURenderPipeline pipeline;
    WGPUBindGroup bind;
    WGPUBindGroupLayout bgl;
    SkyLuts sky;
    WGPUBindGroup skyBind;
    WGPUTexture voxTex;
    WGPUTextureView voxView;
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
        app->cfg.presentMode = WGPUPresentMode_Fifo;
        app->cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
        wgpuSurfaceConfigure(app->surface, &app->cfg);
        app->mod = make_module(app->device);
        WGPUBufferDescriptor bdef;
        memset(&bdef, 0, sizeof bdef);
        bdef.size = 64;
        bdef.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        app->ubo = wgpuDeviceCreateBuffer(app->device, &bdef);
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
        WGPUBindGroupEntry be;
        memset(&be, 0, sizeof be);
        be.binding = 0;
        be.buffer = app->ubo;
        be.size = 64;
        WGPUBindGroupDescriptor bgdef;
        memset(&bgdef, 0, sizeof bgdef);
        bgdef.layout = app->bgl;
        bgdef.entryCount = 1;
        bgdef.entries = &be;
        app->bind = wgpuDeviceCreateBindGroup(app->device, &bgdef);
        // Воксельный патч 48x64x48 в 3D-текстуру (R8Uint, строки паддинг 256).
        {
            WGPUTextureDescriptor td;
            memset(&td, 0, sizeof td);
            td.size.width = VOX_PW;
            td.size.height = VOX_SY;
            td.size.depthOrArrayLayers = VOX_PZ;
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
            static uint8_t staging[256 * VOX_SY * VOX_PZ];
            uint8_t vox[VOX_PW * VOX_SY * VOX_PZ];
            vox_gen_patch(vox, 0, 0, 1337);
            for (int z = 0; z < VOX_PZ; z++)
                for (int y = 0; y < VOX_SY; y++)
                    memcpy(&staging[(size_t)(z * VOX_SY + y) * 256],
                           &vox[((size_t)y * VOX_PZ + (size_t)z) * VOX_PW], VOX_PW);
            WGPUTexelCopyTextureInfo dst;
            memset(&dst, 0, sizeof dst);
            dst.texture = app->voxTex;
            dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout layout;
            memset(&layout, 0, sizeof layout);
            layout.bytesPerRow = 256;
            layout.rowsPerImage = VOX_SY;
            WGPUExtent3D extent;
            memset(&extent, 0, sizeof extent);
            extent.width = VOX_PW;
            extent.height = VOX_SY;
            extent.depthOrArrayLayers = VOX_PZ;
            wgpuQueueWriteTexture(app->queue, &dst, staging, sizeof staging, &layout, &extent);
            printf("voxels OK: patch %dx%dx%d seed 1337\n", VOX_PW, VOX_SY, VOX_PZ);
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
    double t = now - app->t0;
    float dt = (float)(now - app->prevT);
    app->prevT = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt > app->dtMax) app->dtMax = dt;
    // Перемотка времени как у них: T вперёд x36, Shift+T назад (их wc_game.c:287).
    GLFWwindow *win = app->win;
    int tDown = glfwGetKey(win, GLFW_KEY_T) == GLFW_PRESS;
    int shDown = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    app->timeScale = tDown ? (shDown ? -36.0 : 36.0) : 1.0;
    app->dayT += dt * app->timeScale;
    app->cloudT += dt;
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
    float sp = (float)app->speed * dt;
    Vec3 wish = v3(0.0f, 0.0f, 0.0f);
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) wish = v3_add(wish, v3_mul(fh, sp));
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) wish = v3_sub(wish, v3_mul(fh, sp));
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) wish = v3_add(wish, v3_mul(rh, sp));
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) wish = v3_sub(wish, v3_mul(rh, sp));
    // Скольжение вдоль холма: целиком -> только X -> только Z -> стоим.
    float cy0 = app->camPos.y;
    float nx = app->camPos.x + wish.x, nz = app->camPos.z + wish.z;
    if (vox_floor(nx, nz, cy0)) { app->camPos.x = nx; app->camPos.z = nz; }
    else if (vox_floor(app->camPos.x + wish.x, app->camPos.z, cy0)) { app->camPos.x += wish.x; }
    else if (vox_floor(app->camPos.x, app->camPos.z + wish.z, cy0)) { app->camPos.z += wish.z; }
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
    app->rebakes += sky_luts_update(&app->sky, sunDir, moonDir, cy);
    u.camPos = app->camPos; u.time = (float)app->cloudT;
    u.camTarget = v3_add(app->camPos, fwd); u.resX = (float)ww;
    u.sunDir = sunDir; u.maxSteps = app->maxSteps;
    u.resY = (float)hh;
    wgpuQueueWriteBuffer(app->queue, app->ubo, 0, &u, sizeof u);

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
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app->bind, 0, 0);
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
    wgpuBindGroupRelease(app.bind);
    wgpuBindGroupLayoutRelease(app.bgl);
    wgpuRenderPipelineRelease(app.pipeline);
    wgpuBufferRelease(app.ubo);
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
