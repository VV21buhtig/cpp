#ifndef RENDER_API_H
#define RENDER_API_H
// Ядро рендера: API, которое реализует каждый пайплайн (webgpu/gl/vulkan).
// Механики (камера, ввод, меню, время, стриминг-решения) — снаружи, в app.
// Ядро владеет: surface/device, пайплайны, текстуры, UBO, заливки, present.
#include "sdf_math.h"
#include "vox/vox_chunk.h"

typedef struct {
    // Кадр вида (рендер-камера)
    Vec3 camPos;
    float yaw, pitch, fov;
    int resW, resH;
    // Время и светила (часы ведёт app)
    float time;
    float dayT;
    Vec3 sunDir, moonDir;
    // Качество и грейд
    float maxSteps; // губернатор: дальность теней и т.п.
    float gamma, exposure, fog;
    float shadowOn;
    int viewMode; // 0 цвет, 1 нормали, 2 глубина, 3 цена
    int menuOpen, menuSel; // меню поверх (-1 закрыто через menuOpen=0)
    // Фрустум главной для дебага (frustumOn=0 — не рисуем)
    int frustumOn;
    Vec3 mainPos;
    float mainYaw, mainPitch;
} RcView;

typedef struct RenderCore RenderCore;
struct RenderCore {
    void *ctx;
    // Окно уже создано (GLFW), surface/устройство/ресурсы — тут.
    int (*init)(RenderCore *rc, void *glfwWindow);
    void (*shutdown)(RenderCore *rc);
    // Кадр мира из вида. Внутри: bake-if-needed, заливки, сабмит, present.
    void (*frame)(RenderCore *rc, const RcView *v);
    // Стриминг: app решает ЧТО (dirty из мира), ядро — КАК (текстура+теги).
    // Стриминг: app решает ЧТО (dirty из мира), ядро — КАК.
    // nb[6]: соседи (-x,+x,-y,+y,-z,+z) для швов, NULL = воздух.
    // Возвращает 1 если залил (тогда гасить dirty), 0 если рано.
    int (*upload_chunk)(RenderCore *rc, int cx, int cz, const uint8_t *vox16,
                        const VoxChunk *nb[6]); // 16x64x16
    void (*set_origin)(RenderCore *rc, int ox, int oz); // чанк texel (0,*,0)
    // fps кадра для губернатора снаружи (0 пока нет данных)
    float (*fps)(RenderCore *rc);
};

RenderCore *rc_webgpu_create(void);
RenderCore *rc_gl_create(void); // натив only (GL 4.5, не WebGL)
void rc_destroy(RenderCore *rc);
#endif
