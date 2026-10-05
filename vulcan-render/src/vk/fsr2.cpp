// fsr2: см. vk/fsr2.h.
#include "vk/fsr2.h"
#include "ffx_fsr2.h"
#include "vk/ffx_fsr2_vk.h"

#include <cstdio>
#include <cstdlib>

static void fsrMsg(FfxFsr2MsgType type, const wchar_t* msg) {
    char narrow[512];
    size_t i = 0;
    for (; msg[i] && i + 1 < sizeof(narrow); i++) narrow[i] = (char)msg[i];
    narrow[i] = 0;
    printf("FSR2[%d]: %s\n", (int)type, narrow);
}

bool makeFsr(VkCore& core, Fsr& f) {
    size_t scratchSize = ffxFsr2GetScratchMemorySizeVK(core.gpu);
    f.scratch = malloc(scratchSize ? scratchSize : 64);
    if (!f.scratch) { printf("FSR2: no scratch\n"); return false; }
    FfxFsr2Interface iface{};
    if (ffxFsr2GetInterfaceVK(&iface, f.scratch, scratchSize, core.gpu,
                              vkGetDeviceProcAddr) != FFX_OK) {
        printf("FSR2: no interface\n");
        return false;
    }
    FfxFsr2ContextDescription desc{};
    desc.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE |
                 FFX_FSR2_ENABLE_DYNAMIC_RESOLUTION | // renderSize меняется (F9 режимы)
                 FFX_FSR2_ENABLE_DEBUG_CHECKING; // валидация параметров, снять позже
    desc.maxRenderSize = {(uint32_t)core.swapExtent.width, (uint32_t)core.swapExtent.height};
    desc.displaySize = desc.maxRenderSize; // 1.0x: без апскейла
    desc.callbacks = iface;
    desc.device = ffxGetDeviceVK(core.device);
    desc.fpMessage = fsrMsg;
    if (ffxFsr2ContextCreate(&f.ctx, &desc) != FFX_OK) {
        printf("FSR2: context create failed\n");
        return false;
    }
    f.ready = true;
    printf("FSR2: context ready (%ux%u)\n", desc.displaySize.width, desc.displaySize.height);
    return true;
}

void destroyFsr(VkCore& core, Fsr& f) {
    (void)core;
    if (f.ready) ffxFsr2ContextDestroy(&f.ctx);
    free(f.scratch);
    f.scratch = nullptr;
    f.ready = false;
}
