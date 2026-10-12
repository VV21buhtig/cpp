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
void eng_view_close(EngView *vw);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_VIEW_H
