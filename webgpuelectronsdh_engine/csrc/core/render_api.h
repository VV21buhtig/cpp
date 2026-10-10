#ifndef RENDER_API_H
#define RENDER_API_H
// Ядра рендера: РАЗНЫЕ движки на выбор (воксель-DDA, воксель-растр, SDF,
// вектор, пиксели). Общее только рамка кадра (RcView + init/frame/fps),
// входы у каждого домена свои. Ядро объявляет caps, app/редактор льют то,
// что ядро ест. Неподдерживаемый вход = NULL.
// Механики (камера, ввод, меню, время, стриминг-решения) — снаружи, в app.
#include "sdf_math.h"
#include "vox/vox_chunk.h"

// Домен данных ядра (битмаска caps).
#define RC_CAP_VOXEL  (1u << 0) // чанки 16x64x16: upload_chunk/set_origin
#define RC_CAP_SDF    (1u << 1) // сцена примитивов: upload_sdf
#define RC_CAP_VECTOR (1u << 2) // 2D-примитивы: upload_vector
#define RC_CAP_PIXEL  (1u << 3) // CPU-кадр RGBA: upload_pixels

typedef struct {
    // Кадр вида (рендер-камера). 2D-доменам нужны в основном resW/resH/time.
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

// SDF-примитив (домен RC_CAP_SDF). p0 — центр/точка, p1 — второй конец/
// размер, r — радиус/скругление, mat — индекс материала палитры ядра.
typedef struct {
    int kind; // 0 сфера, 1 коробка, 2 тор, 3 плоскость, 4 капсула
    Vec3 p0, p1;
    float r;
    int mat;
} RcSdfObj;

// 2D-примитив (домен RC_CAP_VECTOR). a,b,c,d — параметры фигуры:
// rect: x,y,w,h; circle: cx,cy,r,-; line: x0,y0,x1,y1.
typedef struct {
    int kind; // 0 rect, 1 circle, 2 line
    float a, b, c, d;
    float rgba[4];
} RcVecShape;

typedef struct RenderCore RenderCore;
struct RenderCore {
    void *ctx;
    unsigned caps; // RC_CAP_* — что ядро умеет
    const char *name; // "webgpu", "gl", ... — для выбора в редакторе
    // Окно уже создано (GLFW), surface/устройство/ресурсы — тут.
    int (*init)(RenderCore *rc, void *glfwWindow);
    void (*shutdown)(RenderCore *rc);
    // Кадр мира из вида. Внутри: bake-if-needed, заливки, сабмит, present.
    void (*frame)(RenderCore *rc, const RcView *v);
    // --- Домен VOXEL (NULL если нет RC_CAP_VOXEL) ---
    // Стриминг: app решает ЧТО (dirty из мира), ядро — КАК.
    // nb[6]: соседи (-x,+x,-y,+y,-z,+z) для швов, NULL = воздух.
    // Возвращает 1 если залил (тогда гасить dirty), 0 если рано.
    int (*upload_chunk)(RenderCore *rc, int cx, int cz, const uint8_t *vox16,
                        const VoxChunk *nb[6]); // 16x64x16
    void (*set_origin)(RenderCore *rc, int ox, int oz); // чанк texel (0,*,0)
    // Выселение: чанк ушёл из кольца — выкинуть меш/тексели (NULL если не надо).
    void (*unload_chunk)(RenderCore *rc, int cx, int cz);
    // SDF-объём (VOXEL-домен, NULL если ядро не ест): печёный R8 16x64x16,
    // байт = clamp(d/12,-1,1)*0.5+0.5. Тороид на ядре, швы решает app.
    int (*upload_sdf_chunk)(RenderCore *rc, int cx, int cz, const uint8_t *sdf16);
    // Мировой воксель угла (0,*,0) SDF-объёма (следует за кольцом).
    void (*set_sdf_origin)(RenderCore *rc, int ox, int oy, int oz);
    // --- Домен SDF (NULL если нет RC_CAP_SDF). Полная сцена каждый раз. ---
    int (*upload_sdf)(RenderCore *rc, const RcSdfObj *objs, int n);
    // --- Домен VECTOR (NULL если нет RC_CAP_VECTOR). Кадр примитивов. ---
    int (*upload_vector)(RenderCore *rc, const RcVecShape *shapes, int n);
    // --- Домен PIXEL (NULL если нет RC_CAP_PIXEL). CPU-кадр на весь экран. ---
    int (*upload_pixels)(RenderCore *rc, const uint8_t *rgba, int w, int h);
    // fps кадра для губернатора снаружи (0 пока нет данных)
    float (*fps)(RenderCore *rc);
};

RenderCore *rc_webgpu_create(void);
RenderCore *rc_gl_create(void); // натив only (GL 4.5, не WebGL)
RenderCore *rc_vk_create(void); // натив only (Vulkan 1.0, без слоёв)
RenderCore *rc_sdf_create(void); // натив only (GL 4.5 реймарш, caps=SDF)
// Выбор по имени ("webgpu", "gl"). NULL если нет такого. Для редактора.
RenderCore *rc_create(const char *name);
void rc_destroy(RenderCore *rc);
#endif
