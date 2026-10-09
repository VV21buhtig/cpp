// Шаг 0: ОДИН КУБ через SDF-пайплайн (sdf.wgsl -> модуль -> треугольник).
// Лестница: куб -> +шар -> +пол -> +тени -> +небо. Сюда правим только трубу,
// сцену — в shaders/sdf.wgsl (+ зеркало csrc/sdf_scene.h).
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "sdf_surface.h"
#include "sdf_math.h"
#include "sdf_ubo.h"
#include "sdf_scene.h"
#include "sdf_gpu.h"
#include "sdf_wgsl.h"

typedef struct { WGPUAdapter adapter; int done; } AdapterSlot;
typedef struct { WGPUDevice device; int done; } DeviceSlot;

static void onAdapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg,
                      void *u1, void *u2) {
    (void)u2;
    AdapterSlot *s = (AdapterSlot *)u1;
    if (st != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "adapter fail: %.*s\n", (int)msg.length, msg.data);
        s->done = 1;
        return;
    }
    s->adapter = a;
    s->done = 1;
}

static void onDevice(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg,
                     void *u1, void *u2) {
    (void)u2;
    DeviceSlot *s = (DeviceSlot *)u1;
    if (st != WGPURequestDeviceStatus_Success) {
        fprintf(stderr, "device fail: %.*s\n", (int)msg.length, msg.data);
        s->done = 1;
        return;
    }
    s->device = d;
    s->done = 1;
}

// wgpu-native: wgpuInstanceProcessEvents не реализован — ждём слипом.
static void wait_for(volatile int *done) {
    struct timespec ts = {0, 2000000};
    for (int i = 0; i < 5000 && !*done; i++) nanosleep(&ts, 0);
}

static double g_yaw = 2.54, g_pitch = -0.30, g_speed = 4.0, g_sens = 0.0012;
static double g_lx, g_ly;
static int g_locked = 0;

static void set_locked(GLFWwindow *w, int locked) {
    g_locked = locked;
    glfwSetInputMode(w, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    int ww, hh;
    glfwGetWindowSize(w, &ww, &hh);
    g_lx = ww * 0.5; g_ly = hh * 0.5;
}
static void on_mouse(GLFWwindow *w, double x, double y) {
    (void)w;
    if (!g_locked) { g_lx = x; g_ly = y; return; }
    g_yaw -= (x - g_lx) * g_sens;
    g_pitch -= (y - g_ly) * g_sens;
    if (g_pitch > 1.55) g_pitch = 1.55;
    if (g_pitch < -1.55) g_pitch = -1.55;
    g_lx = x; g_ly = y;
}
static void on_btn(GLFWwindow *w, int b, int act, int m) {
    (void)m;
    if (b == GLFW_MOUSE_BUTTON_LEFT && act == GLFW_PRESS && !g_locked) set_locked(w, 1);
}
static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    g_speed *= (1.0 + (dy > 0 ? 0.15 : dy < 0 ? -0.15 : 0.0));
    if (g_speed < 1.0) g_speed = 1.0;
    if (g_speed > 12.0) g_speed = 12.0;
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

int main(int argc, char **argv) {
    int maxFrames = -1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);

    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "sdf cam — freecam WASD", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    glfwSetCursorPosCallback(win, on_mouse);
    glfwSetMouseButtonCallback(win, on_btn);
    glfwSetScrollCallback(win, on_scroll);
    set_locked(win, 1); // сразу FPS-режим; ESC — отпустить, клик — вернуть

    WGPUInstanceDescriptor idef;
    memset(&idef, 0, sizeof idef);
    WGPUInstance inst = wgpuCreateInstance(&idef);
    if (!inst) { fprintf(stderr, "instance fail\n"); return 1; }
    WGPUSurface surface = sdf_create_surface(inst, win);
    if (!surface) { fprintf(stderr, "surface fail\n"); return 1; }

    WGPURequestAdapterOptions aopt;
    memset(&aopt, 0, sizeof aopt);
    aopt.powerPreference = WGPUPowerPreference_HighPerformance;
    aopt.compatibleSurface = surface;
    aopt.featureLevel = WGPUFeatureLevel_Core;
    AdapterSlot as = {0, 0};
    WGPURequestAdapterCallbackInfo aci;
    memset(&aci, 0, sizeof aci);
    aci.mode = WGPUCallbackMode_WaitAnyOnly;
    aci.callback = onAdapter;
    aci.userdata1 = &as;
    wgpuInstanceRequestAdapter(inst, &aopt, aci);
    wait_for(&as.done);
    if (!as.adapter) { fprintf(stderr, "adapter timeout\n"); return 1; }

    WGPUDeviceDescriptor ddef;
    memset(&ddef, 0, sizeof ddef);
    ddef.label = (WGPUStringView){"sdf device", 10};
    DeviceSlot ds = {0, 0};
    WGPURequestDeviceCallbackInfo dci;
    memset(&dci, 0, sizeof dci);
    dci.mode = WGPUCallbackMode_WaitAnyOnly;
    dci.callback = onDevice;
    dci.userdata1 = &ds;
    wgpuAdapterRequestDevice(as.adapter, &ddef, dci);
    wait_for(&ds.done);
    if (!ds.device) { fprintf(stderr, "device timeout\n"); return 1; }
    WGPUDevice device = ds.device;
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    WGPUSurfaceCapabilities caps;
    memset(&caps, 0, sizeof caps);
    wgpuSurfaceGetCapabilities(surface, as.adapter, &caps);
    WGPUTextureFormat fmt = caps.formats[0];
    WGPUSurfaceConfiguration cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.device = device;
    cfg.format = fmt;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.presentMode = WGPUPresentMode_Fifo;
    cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(surface, &cfg);

    // Труба SDF: модуль из канона + треугольник без буферов + 1 UBO.
    WGPUShaderModule mod = make_module(device);
    printf("WGSL module created (валидация — глазами: куб должен быть виден)\n");
    WGPUBufferDescriptor bdef;
    memset(&bdef, 0, sizeof bdef);
    bdef.size = 64;
    bdef.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer ubo = wgpuDeviceCreateBuffer(device, &bdef);

    WGPUColorTargetState color;
    memset(&color, 0, sizeof color);
    color.format = fmt;
    color.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState frag;
    memset(&frag, 0, sizeof frag);
    frag.module = mod;
    frag.entryPoint = (WGPUStringView){"fs", 2};
    frag.targetCount = 1;
    frag.targets = &color;
    WGPUVertexState vert;
    memset(&vert, 0, sizeof vert);
    vert.module = mod;
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
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipe);
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry be;
    memset(&be, 0, sizeof be);
    be.binding = 0;
    be.buffer = ubo;
    be.size = 64;
    WGPUBindGroupDescriptor bgdef;
    memset(&bgdef, 0, sizeof bgdef);
    bgdef.layout = bgl;
    bgdef.entryCount = 1;
    bgdef.entries = &be;
    WGPUBindGroup bind = wgpuDeviceCreateBindGroup(device, &bgdef);
    printf("step0 pipe OK: cube, flat light, dark bg\n");

    Vec3 camPos = v3(3.94f, 1.48f, -2.70f); // старт как раньше: смотрим на сцену
    double t0 = glfwGetTime();
    double prevT = t0;
    double lastLog = -10.0;
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS && g_locked) set_locked(win, 0);
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        if ((uint32_t)ww != cfg.width || (uint32_t)hh != cfg.height) {
            cfg.width = (uint32_t)ww;
            cfg.height = (uint32_t)hh;
            wgpuSurfaceConfigure(surface, &cfg);
        }
        double t = glfwGetTime() - t0;
        double now = glfwGetTime();
        float dt = (float)(now - prevT);
        prevT = now;
        if (dt > 0.05f) dt = 0.05f;
        // Freecam: WASD + Space вверх / Shift вниз, скорость на колесе.
        float cp = cosf((float)g_pitch);
        Vec3 fwd = v3(cp * cosf((float)g_yaw), sinf((float)g_pitch), cp * sinf((float)g_yaw));
        Vec3 right = v3_norm(v3_cross(fwd, v3(0.0f, 1.0f, 0.0f)));
        float sp = (float)g_speed * dt;
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camPos = v3_add(camPos, v3_mul(fwd, sp));
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camPos = v3_sub(camPos, v3_mul(fwd, sp));
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camPos = v3_add(camPos, v3_mul(right, sp));
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camPos = v3_sub(camPos, v3_mul(right, sp));
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) camPos.y += sp;
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) camPos.y -= sp;
        float cx = camPos.x, cy = camPos.y, cz = camPos.z;
        sdf_guard_fly(&cx, &cy, &cz, fwd);
        camPos = v3(cx, cy, cz);
        Vec3 camTarget = v3_add(camPos, fwd);

        SdfUBO u;
        memset(&u, 0, sizeof u);
        u.camPos = v3(cx, cy, cz); u.time = (float)t;
        u.camTarget = camTarget;  u.resX = (float)ww;
        u.sunDir = sdf_sun((float)t); u.maxSteps = 100.0f;
        u.resY = (float)hh;
        wgpuQueueWriteBuffer(queue, ubo, 0, &u, sizeof u);

        WGPUSurfaceTexture st;
        memset(&st, 0, sizeof st);
        wgpuSurfaceGetCurrentTexture(surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            fprintf(stderr, "getCurrentTexture status=%d\n", (int)st.status);
            break;
        }
        WGPUTextureViewDescriptor vdef;
        memset(&vdef, 0, sizeof vdef);
        vdef.format = fmt;
        vdef.dimension = WGPUTextureViewDimension_2D;
        vdef.mipLevelCount = 1;
        vdef.arrayLayerCount = 1;
        vdef.aspect = WGPUTextureAspect_All;
        WGPUTextureView view = wgpuTextureCreateView(st.texture, &vdef);

        WGPUCommandEncoderDescriptor edef;
        memset(&edef, 0, sizeof edef);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &edef);
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
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, 0);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBufferDescriptor cbdef;
        memset(&cbdef, 0, sizeof cbdef);
        WGPUCommandBuffer cbuf = wgpuCommandEncoderFinish(enc, &cbdef);
        wgpuQueueSubmit(queue, 1, &cbuf);
        wgpuSurfacePresent(surface);

        wgpuCommandBufferRelease(cbuf);
        wgpuCommandEncoderRelease(enc);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);

        if (t - lastLog >= 4.0) {
            lastLog = t;
            printf("f=%d pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f spd=%.1f\n",
                frame, cx, cy, cz, g_yaw, g_pitch, g_speed);
        }
        if (++frame == maxFrames) break;
    }
    printf("step0 OK: %d frames\n", frame);
    wgpuBindGroupRelease(bind);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuRenderPipelineRelease(pipeline);
    wgpuBufferRelease(ubo);
    wgpuShaderModuleRelease(mod);
    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(as.adapter);
    wgpuSurfaceRelease(surface);
    wgpuInstanceRelease(inst);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
