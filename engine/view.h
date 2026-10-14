// Headless-вьювер бекенда: окно+ядро+подача+кадр в CPU за 4 вызова.
// Для тонкого клиента (Electron через koffi), headless-тестов и раннера.
// Никакого Qt: только libengine.so + скрытое GLFW-окно. Размер кадра
// фиксирован VW_W x VW_H (динамика через resize — следующим срезом).
#ifndef ENGINE_VIEW_H
#define ENGINE_VIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VW_W 1280
#define VW_H 720
// Динамика (eng_viewport_resize): 64..2048, остальное — clamp.
#define VW_MIN 64
#define VW_MAX 2048

typedef struct EngView EngView;

// Открыть: скрытое окно + ядро (домен x бэкенд) + демо-мир.
// NULL если нет ячейки, нет окна или ядро не встало.
EngView *eng_view_open(unsigned domain, unsigned backend);
// Кадр: орбита тикает внутри, подача дотягивается, пиксели в rgba
// (w*h*4, RGBA, строка 0 = верх). w,h обязаны быть VW_W/VW_H.
// Возвращает 1 если залил, 0 если нет.
int eng_view_frame(EngView *vw, uint8_t *rgba, int w, int h);
// Дерево сцены для клиента: JSON (eng_scene_json), живёт до следующего
// вызова или close. Не free().
const char *eng_view_scene(EngView *vw);
// Кадров отрисовано (кадр растёт только при рендере — для вердиктов).
int eng_view_frames(const EngView *vw);
// Горячая замена шейдера (Monaco): id — путь файла, code — новый текст.
// Лог компиляции всегда в log (и при успехе короткое "имя: ok").
// 1 ок, 0 отказ (нет ядра/API, чужой id, не скомпилилось — старое живо).
int eng_view_shader(EngView *vw, const char *id, const char *code, char *log, int logcap);
// Динамика как в UE5/Unity: новый размер кадра. Пересоздаёт цель (GL)
// или всё окно+свопчейн (VK/WebGPU — экстент там от окна). Подача мира
// дотягивается за пару кадров (как при смене бэкенда). 1 ок, 0 отказ.
// Диапазон VW_MIN..VW_MAX, остальное — clamp (не отказ).
int eng_view_resize(EngView *vw, int w, int h);
void eng_view_close(EngView *vw);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_VIEW_H
