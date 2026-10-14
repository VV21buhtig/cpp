#ifndef RENDER_API_H
#define RENDER_API_H

#ifdef __cplusplus
extern "C" {
#endif

// Ядра рендера: РАЗНЫЕ движки на выбор (воксель-DDA, воксель-растр, SDF,
// вектор, пиксели). Общее только рамка кадра (RcView + init/frame/fps),
// входы у каждого домена свои. Ядро объявляет caps, app/редактор льют то,
// что ядро ест. Неподдерживаемый вход = NULL.
// Механики (камера, ввод, меню, время, стриминг-решения) — снаружи, в app.
#include <stdint.h>
#include "vec.h"
// Данные игры движку opaque: только указатели.
typedef struct VoxChunk VoxChunk;

// Домен данных ядра (битмаска caps). Что рисуем — отдельно от того,
// ЧЕРЕЗ ЧТО рисуем (см. backend ниже). Ядро = домен × бэкенд.
#define RC_CAP_VOXEL  (1u << 0) // чанки 16x64x16: upload_chunk/set_origin
#define RC_CAP_SDF    (1u << 1) // сцена примитивов: upload_sdf
#define RC_CAP_VECTOR (1u << 2) // 2D-примитивы: upload_vector
#define RC_CAP_PIXEL  (1u << 3) // CPU-кадр RGBA: upload_pixels
#define RC_CAP_MESH   (1u << 4) // суп треугольников: upload_mesh

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
} RcView;

// SDF-примитив (домен RC_CAP_SDF). p0 — центр/точка, p1 — второй конец/
// размер, r — радиус/скругление, mat — индекс материала палитры ядра.
typedef struct {
    int kind; // 0 сфера, 1 коробка, 2 тор, 3 плоскость, 4 капсула
    Vec3 p0, p1;
    float r;
    int mat;
} RcSdfObj;

// Треугольный суп (домен RC_CAP_MESH): неиндексированный, вершина 9 float
// (pos3 + nrm3 + col3). Нормали даёт загрузчик (плоские грани — по грани).
typedef struct {
    const float *verts;
    int nverts;
} RcMesh;

// 2D-примитив (домен RC_CAP_VECTOR). a,b,c,d — параметры фигуры:
// rect: x,y,w,h; circle: cx,cy,r,-; line: x0,y0,x1,y1.
typedef struct {
    int kind; // 0 rect, 1 circle, 2 line
    float a, b, c, d;
    float rgba[4];
} RcVecShape;

typedef struct RenderCore RenderCore;
typedef struct RcTarget RcTarget; // см. rtarget.h (полное определение там)
// Бэкенд ядра (на чём рисуем). Только три: Metal нет (не на чем запускать),
// DX12 не нужен (Vulkan удобнее и шире), DX11 хуже GL. Точка.
#define RC_BACKEND_NONE 0
#define RC_BACKEND_WEBGPU 1
#define RC_BACKEND_GL 2
#define RC_BACKEND_VK 3
struct RenderCore {
    void *ctx;
    unsigned caps; // RC_CAP_* — домен (что ест)
    unsigned backend; // RC_BACKEND_* — через что рисует
    const char *name; // ключ создания ("webgpu", "gl", ...)
    // Окно уже создано (GLFW), surface/устройство/ресурсы — тут.
    int (*init)(RenderCore *rc, void *glfwWindow);
    void (*shutdown)(RenderCore *rc);
    // Кадр мира из вида. Внутри: bake-if-needed, заливки, сабмит, present.
    void (*frame)(RenderCore *rc, const RcView *v);
    // Кадр НЕ на экран, а в цель (композитор L3: несколько доменов в кадре).
    // NULL = только свой экран (такие ядра в композицию не берём).
    // Цель создаёт композитор под бэкенд (GL: FBO; VK: позже).
    int (*frame_to)(RenderCore *rc, const RcView *v, RcTarget *t);
    // Кадр сразу в CPU-буфер (встраивание в чужое окно: Qt-вьюпорт редактора).
    // rgba: w*h*4 байт, строка 0 = ВЕРХ кадра, порядок RGBA. Ядро само рендерит
    // и скачивает своим путём (VK: staging-буфер; WebGPU: map+poll).
    // Возвращает 1 если залил, 0 если размер чужой/не готово/нет пути.
    // NULL = нет CPU-пути (тогда только frame_to/frame).
    int (*frame_cpu)(RenderCore *rc, const RcView *v, uint8_t *rgba, int w, int h);
    // Пикинг: нарисовать сцену плоским id в цель (R=id+1, 0 = мимо).
    // id — сущность вызывателя (ядро её не знает, просто красит).
    // NULL = ядро не умеет.
    int (*pick)(RenderCore *rc, const RcView *v, RcTarget *t, unsigned id);
    // Скачать ЦЕЛЬ в CPU (RGBA, строка 0 = верх): цель создана этим же ядром
    // (его натив в RcTarget), последний рендер — frame_to/comp (картинка уже
    // в читаемом лейауте). Для comp->canvas и тестов. NULL = не умеет.
    int (*download)(RenderCore *rc, RcTarget *t, uint8_t *rgba, int w, int h);
    // Горячая замена шейдера (Monaco/VFS): id — путь файла (полностью или
    // basename), code — новый текст стороны. Перекомпиляция БЕЗ пересоздания
    // контекста; при отказе старая программа жива, причина в log (всегда).
    // NULL = ядро не умеет (честный отказ, не молчание).
    int (*shader_reload)(RenderCore *rc, const char *id, const char *code, char *log,
                         int logcap);
    // --- Домен VOXEL (NULL если нет RC_CAP_VOXEL) ---
    // Стриминг: app решает ЧТО (dirty из мира), ядро — КАК.
    // nb[6]: соседи (-x,+x,-y,+y,-z,+z) для швов, NULL = воздух.
    // Возвращает 1 если залил (тогда гасить dirty), 0 если рано.
    int (*upload_chunk)(RenderCore *rc, int cx, int cz, const uint8_t *vox16,
                        const VoxChunk *nb[6]); // 16x64x16
    void (*set_origin)(RenderCore *rc, int ox, int oz); // чанк texel (0,*,0)
    // Выселение: чанк ушёл из кольца — выкинуть меш/тексели (NULL если не надо).
    void (*unload_chunk)(RenderCore *rc, int cx, int cz);
    // Готовый textured-меш (12 float: pos3 norm3 uv2 tile ao day night)
    // в слот + модель. Мешинг (чанки/соседи) — дело фидера, не ядра.
    // upload=NULL у ядер без textured-мешей.
    int (*upload_voxmesh)(RenderCore *rc, int slot, const float *v12,
                          int nverts, const float *model16);
    void (*unload_voxmesh)(RenderCore *rc, int slot);
    // Тайловый атлас вокселей (RGBA, tile x tile x layers; наш: 16x16x9).
    // Текстура материала домена: пиксели снаружи (фидер), объект на ядре.
    // Тинты травы/листвы — константы материала ядра (см. ядра).
    // Без вызова — белая заглушка (не падение). NULL у ядер без атласа.
    int (*upload_tiles)(RenderCore *rc, const uint8_t *rgba, int tile, int layers);
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
    // Строка 0 = ВЕРХ кадра (натурально для CPU-буферов; ядро флипает под GPU).
    int (*upload_pixels)(RenderCore *rc, const uint8_t *rgba, int w, int h);
    // --- Домен MESH (NULL если нет RC_CAP_MESH). Суп целиком за раз. ---
    int (*upload_mesh)(RenderCore *rc, const RcMesh *m);
    // fps кадра для губернатора снаружи (0 пока нет данных)
    float (*fps)(RenderCore *rc);
};

RenderCore *rc_webgpu_create(void);
RenderCore *rc_gl_create(void); // натив only (GL 4.5, не WebGL)
RenderCore *rc_vk_create(void); // натив only (Vulkan 1.0, без слоёв)
RenderCore *rc_sdf_create(void); // натив only (GL 4.5 реймарш, caps=SDF)
RenderCore *rc_pix_create(void); // натив only (GL 4.5 блит, caps=PIXEL)
RenderCore *rc_vec_create(void); // натив only (GL 4.5 + CPU-растр, caps=VECTOR)
// Выбор по имени ("webgpu", "gl"). NULL если нет такого. Для редактора.
RenderCore *rc_create(const char *name);
// Выбор по двум осям: домен (RC_CAP_*, один бит) + бэкенд (RC_BACKEND_*).
// NULL если такой ячейки нет. Это и есть таблица «домен × бэкенд».
RenderCore *rc_create_by(unsigned domain, unsigned backend);
// Версия движка для проверки совместимости оболочкой ("0.1.0").
// Редактор сверяет мажор прежде чем дёргать остальное.
const char *eng_version(void);
// Саморегистрация ядра (дергает конструктор ядра, не человек).
// fn: указатель на rc_*_create. Повторная регистрация того же имени — игнор.
void rc_register(const char *name, unsigned domain, unsigned backend,
                 RenderCore *(*fn)(void));
void rc_destroy(RenderCore *rc);

#ifdef __cplusplus
}
#endif
#endif // RENDER_API_H
