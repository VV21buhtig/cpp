// fsr2: контекст FidelityFX Super Resolution 2 (1.0x, замена нашего TAA).
// Scratch — обычный CPU-кусок (malloc/free); внутренние ресурсы бэкенд
// создаёт/чистит сам через vkGetDeviceProcAddr.
#pragma once

#include "vk/vk_ctx.h"
#include "ffx_fsr2.h"

struct Fsr {
    FfxFsr2Context ctx{};
    void* scratch = nullptr;
    bool ready = false;
};

bool makeFsr(VkCore& core, Fsr& f);
void destroyFsr(VkCore& core, Fsr& f);
