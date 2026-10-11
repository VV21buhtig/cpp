#ifndef ENGINE_RHI_WEBGPU_H
#define ENGINE_RHI_WEBGPU_H
// RHI-WebGPU: сырой бэкенд Dawn/wgpu-native. Instance -> surface ->
// adapter -> device/queue -> configure. Без сцены: только устройство.
// Асинхронность Dawn — слип-вейтом (ProcessEvents в native паникует).
#include <webgpu/webgpu.h>
#include <stdint.h>

typedef struct {
    WGPUInstance inst;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat fmt;
    char devName[256];
} RhiWebgpu;

// Поднять всё вплоть до device/queue (окно уже создано, GLFW_NO_API).
// 1 ок, 0 нет (причина в stderr).
int rhi_webgpu_init(RhiWebgpu *r, void *glfwWindow);
// Настроить поверхность под размер (usage всегда +COPY_SRC под VOX_SHOT).
// 1 ок, 0 нет.
int rhi_webgpu_configure(RhiWebgpu *r, uint32_t w, uint32_t h);
// Человеческий статус getCurrentTexture (для лога, не ошибка).
const char *rhi_webgpu_tex_status(int status);
void rhi_webgpu_shutdown(RhiWebgpu *r);
#endif
