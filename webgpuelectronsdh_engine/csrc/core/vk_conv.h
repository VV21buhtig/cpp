#ifndef CORE_VK_CONV_H
#define CORE_VK_CONV_H
// Все противоречия GL/VK в одном месте. Правило одно:
// фреймбуфер Vulkan растёт СВЕРХУ ВНИЗ, GL — снизу вверх.
// Отсюда ровно три адаптации (больше нигде Y не трогаем):
//  1. Viewport с отрицательной высотой -> геометрия и winding как в GL
//     (CCW + CULL_BACK 1:1, мешер общий). См. vk_viewport_fill().
//  2. gl_FragCoord.y во фреймбуфере сверху -> небо отражает Y на входе
//     (shaders/vk/vk_sky.frag, ndc.y = 1 - ...).
//  3. Ридбэк: строка 0 = верх кадра -> PPM пишем сверху вниз
//     (в GL наоборот). См. VK_SHOT в vk_core.c.
// Плюс глубина [0,1] вместо [-1,1]: m4persp_vk() в mat4.h.
#include <vulkan/vulkan.h>

static inline VkViewport vk_viewport_fill(uint32_t w, uint32_t h) {
    VkViewport vp;
    vp.x = 0.0f;
    vp.y = (float)h;
    vp.width = (float)w;
    vp.height = -(float)h; // п.1: Y-зеркало под GL-winding
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    return vp;
}

static inline VkRect2D vk_scissor_fill(uint32_t w, uint32_t h) {
    VkRect2D sc;
    sc.offset.x = 0;
    sc.offset.y = 0;
    sc.extent.width = w;
    sc.extent.height = h;
    return sc;
}
#endif
