// RHI-WebGPU: мясной бэкенд. C11. Поверхность: Wayland, фолбэк Xlib
// (тот же приём что sdf_surface.h в игре — без glfw3webgpu).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <wayland-client-core.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rhi_webgpu.h"

typedef struct {
    int done;
    WGPUAdapter adapter;
    WGPUDevice device;
} RhiWait;

static void on_adapter(WGPURequestAdapterStatus st, WGPUAdapter a,
                       WGPUStringView msg, void *u1, void *u2) {
    (void)u2;
    RhiWait *w = (RhiWait *)u1;
    if (st != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "rhi_webgpu: adapter fail: %.*s\n", (int)msg.length, msg.data);
    } else {
        w->adapter = a;
    }
    w->done = 1;
}

static void on_device(WGPURequestDeviceStatus st, WGPUDevice d,
                      WGPUStringView msg, void *u1, void *u2) {
    (void)u2;
    RhiWait *w = (RhiWait *)u1;
    if (st != WGPURequestDeviceStatus_Success) {
        fprintf(stderr, "rhi_webgpu: device fail: %.*s\n", (int)msg.length, msg.data);
    } else {
        w->device = d;
    }
    w->done = 1;
}

static void sleep_wait(volatile int *done) {
    struct timespec ts = {0, 2000000};
    for (int i = 0; i < 5000 && !*done; i++) nanosleep(&ts, 0);
}

static WGPUSurface rhi_surface(WGPUInstance inst, GLFWwindow *win) {
    {
        struct wl_display *dpy = glfwGetWaylandDisplay();
        if (dpy) {
            struct wl_surface *ws = glfwGetWaylandWindow(win);
            if (ws) {
                WGPUSurfaceSourceWaylandSurface src;
                memset(&src, 0, sizeof src);
                src.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
                src.display = dpy;
                src.surface = ws;
                WGPUSurfaceDescriptor desc;
                memset(&desc, 0, sizeof desc);
                desc.nextInChain = (const WGPUChainedStruct *)&src;
                WGPUSurface s = wgpuInstanceCreateSurface(inst, &desc);
                if (s) return s;
            }
        }
    }
    {
        Display *dpy = glfwGetX11Display();
        if (dpy) {
            Window w = glfwGetX11Window(win);
            if (w) {
                WGPUSurfaceSourceXlibWindow src;
                memset(&src, 0, sizeof src);
                src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
                src.display = dpy;
                src.window = (uint64_t)w;
                WGPUSurfaceDescriptor desc;
                memset(&desc, 0, sizeof desc);
                desc.nextInChain = (const WGPUChainedStruct *)&src;
                WGPUSurface s = wgpuInstanceCreateSurface(inst, &desc);
                if (s) return s;
            }
        }
    }
    return 0;
}

int rhi_webgpu_init(RhiWebgpu *r, void *glfwWindow) {
    memset(r, 0, sizeof *r);
    GLFWwindow *win = (GLFWwindow *)glfwWindow;
    r->inst = wgpuCreateInstance(0);
    if (!r->inst) { fprintf(stderr, "rhi_webgpu: instance fail\n"); return 0; }
    r->surface = rhi_surface(r->inst, win);
    if (!r->surface) { fprintf(stderr, "rhi_webgpu: surface fail\n"); return 0; }
    WGPURequestAdapterOptions aopt;
    memset(&aopt, 0, sizeof aopt);
    aopt.powerPreference = WGPUPowerPreference_HighPerformance;
    aopt.compatibleSurface = r->surface;
    aopt.featureLevel = WGPUFeatureLevel_Core;
    WGPURequestAdapterCallbackInfo aci;
    memset(&aci, 0, sizeof aci);
    aci.mode = WGPUCallbackMode_WaitAnyOnly;
    aci.callback = on_adapter;
    RhiWait w;
    memset(&w, 0, sizeof w);
    aci.userdata1 = &w;
    wgpuInstanceRequestAdapter(r->inst, &aopt, aci);
    sleep_wait(&w.done);
    if (!w.adapter) return 0;
    r->adapter = w.adapter;
    WGPUAdapterInfo ainfo;
    memset(&ainfo, 0, sizeof ainfo);
    if (wgpuAdapterGetInfo(r->adapter, &ainfo) == WGPUStatus_Success) {
        snprintf(r->devName, sizeof r->devName, "%.*s",
                 (int)ainfo.device.length, ainfo.device.data);
    }
    WGPUDeviceDescriptor ddef;
    memset(&ddef, 0, sizeof ddef);
    ddef.label = (WGPUStringView){"rhi device", 10};
    WGPURequestDeviceCallbackInfo dci;
    memset(&dci, 0, sizeof dci);
    dci.mode = WGPUCallbackMode_WaitAnyOnly;
    dci.callback = on_device;
    memset(&w, 0, sizeof w);
    dci.userdata1 = &w;
    wgpuAdapterRequestDevice(r->adapter, &ddef, dci);
    sleep_wait(&w.done);
    if (!w.device) return 0;
    r->device = w.device;
    r->queue = wgpuDeviceGetQueue(r->device);
    return 1;
}

int rhi_webgpu_configure(RhiWebgpu *r, uint32_t w, uint32_t h) {
    WGPUSurfaceCapabilities caps;
    memset(&caps, 0, sizeof caps);
    wgpuSurfaceGetCapabilities(r->surface, r->adapter, &caps);
    if (caps.formatCount < 1) { fprintf(stderr, "rhi_webgpu: no formats\n"); return 0; }
    r->fmt = caps.formats[0];
    WGPUSurfaceConfiguration cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.device = r->device;
    cfg.format = r->fmt;
    // COPY_SRC всегда: VOX_SHOT читает прямо с поверхности, цены ноль.
    cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    cfg.width = w;
    cfg.height = h;
    cfg.presentMode = WGPUPresentMode_Mailbox; // Immediate не поддерживается
    cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(r->surface, &cfg);
    return 1;
}

const char *rhi_webgpu_tex_status(int status) {
    switch (status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal: return "optimal";
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal: return "suboptimal";
    case WGPUSurfaceGetCurrentTextureStatus_Timeout: return "timeout";
    case WGPUSurfaceGetCurrentTextureStatus_Outdated: return "outdated";
    case WGPUSurfaceGetCurrentTextureStatus_Lost: return "lost";
    default: return "error";
    }
}

void rhi_webgpu_shutdown(RhiWebgpu *r) {
    if (r->surface) wgpuSurfaceUnconfigure(r->surface);
    if (r->queue) wgpuQueueRelease(r->queue);
    if (r->device) wgpuDeviceRelease(r->device);
    if (r->adapter) wgpuAdapterRelease(r->adapter);
    if (r->surface) wgpuSurfaceRelease(r->surface);
    if (r->inst) wgpuInstanceRelease(r->inst);
    memset(r, 0, sizeof *r);
}
