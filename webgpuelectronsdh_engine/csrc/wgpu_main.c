// C + WebGPU: окно GLFW -> surface -> adapter -> device -> swapchain-clear.
// Веха: доказать путь на Vega (RADV/Vulkan) без JS. SDF-пайплайн — следующим шагом.
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdf_surface.h"

// wgpu-native: wgpuInstanceProcessEvents не реализован (panic), запросы
// дозревают во внутренних тредах — ждём слипом, не помпой.
static void wait_for(volatile int *done) {
    struct timespec ts = {0, 2000000}; // 2мс
    for (int i = 0; i < 5000 && !*done; i++) nanosleep(&ts, 0);
}

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

int main(int argc, char **argv) {
    int maxFrames = -1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);

    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "sdf engine — C/WebGPU", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }

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
    printf("webgpu OK: format=%d (surface 1280x720, clear-loop)\n", (int)fmt);

    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        if ((uint32_t)ww != cfg.width || (uint32_t)hh != cfg.height) {
            cfg.width = (uint32_t)ww;
            cfg.height = (uint32_t)hh;
            wgpuSurfaceConfigure(surface, &cfg);
        }
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
        ca.clearValue = (WGPUColor){0.05, 0.10, 0.22, 1.0};
        WGPURenderPassDescriptor rp;
        memset(&rp, 0, sizeof rp);
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBufferDescriptor bdef;
        memset(&bdef, 0, sizeof bdef);
        WGPUCommandBuffer buf = wgpuCommandEncoderFinish(enc, &bdef);
        wgpuQueueSubmit(queue, 1, &buf);
        wgpuSurfacePresent(surface);

        wgpuCommandBufferRelease(buf);
        wgpuCommandEncoderRelease(enc);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);
        if (++frame == maxFrames) break;
    }
    printf("wgpu-render OK: %d frames\n", frame);
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
