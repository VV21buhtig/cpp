#ifndef SDF_SURFACE_H
#define SDF_SURFACE_H
// Surface без glfw3webgpu (тот под старый webgpu.h).
// Натив: Wayland, фолбэк Xlib. Веб: селектор канваса (emdawnwebgpu).
#include <GLFW/glfw3.h>
#ifndef __EMSCRIPTEN__
#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <wayland-client-core.h>
#include <X11/Xlib.h>
#endif
#include <webgpu/webgpu.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
static inline WGPUSurface sdf_create_surface(WGPUInstance inst, GLFWwindow *win) {
    (void)win;
    WGPUSurfaceSourceCanvasHTMLSelector src;
    memset(&src, 0, sizeof src);
    src.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
    src.selector = (WGPUStringView){"#canvas", 7};
    WGPUSurfaceDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.nextInChain = (const WGPUChainedStruct *)&src;
    return wgpuInstanceCreateSurface(inst, &desc);
}
#else

static inline WGPUSurface sdf_create_surface(WGPUInstance inst, GLFWwindow *win) {
    // 1) Wayland
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
    // 2) Xlib (XWayland / чистый X11)
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
#endif
#endif
