// Move-гизмо (стрелки RGB=XYZ) поверх вьюпорта. Проекция тем же PV что ядра
// (mat4.h), хит — дистанция до сегмента <8px, drag вдоль оси в мировых
// единицах, коммит на UP + undo push. Rotate/scale — позже (mode уже есть).
// Приоритет хит-теста: гизмо > панели > сцена (как UE/Roblox).
#ifndef ENGINE_UI_GIZMO_H
#define ENGINE_UI_GIZMO_H

#include "scene.h"
#include "render_api.h" // RcView
#include "ui.h"
#include "ui_bind.h" // UiReply
#include "ui_undo.h" // коммит drag в общий стек

typedef struct UiGizmo UiGizmo;

UiGizmo *ui_gizmo_create(void);
void ui_gizmo_destroy(UiGizmo *g);
// tool: 0 select (прячем), 1 move (стрелки). Остальные — позже.
void ui_gizmo_tool(UiGizmo *g, int tool);
// Событие. undo — общий стек панелей (коммит drag идёт туда же).
UiReply ui_gizmo_event(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H,
                       UiUndo *undo, const UiEvent *e);
// Какая сущность под гизмо (ставит вызывающий из panels-selected).
void ui_gizmo_select(UiGizmo *g, unsigned ent);
// Прямоугольник вьюпорта в пикселях окна (y вниз, как UE-документ): проекция
// NDC маппится ВНУТРЬ rect (док-панели отъедают место сплиттерами, вьюпорту —
// остаток). По умолчанию весь кадр (вызывать до build/event).
void ui_gizmo_viewport(UiGizmo *g, int x, int y, int w, int h);
// Down-хит стрелков сущности (приоритет выше панелей).
UiReply ui_gizmo_pick(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H,
                      unsigned ent, float x, float y);
// Точка ручки для тестов/автоматизации: axis 0/1/2, kind 0 стрелка / 1 кольцо,
// angleDeg вдоль ручки (стрелка: 0 начало 100 конец; кольцо: градусы).
// Возвращает 1 + экранные x/y.
int ui_gizmo_handle(UiGizmo *g, ScWorld *scene, const RcView *v, int axis, int kind,
                    float angleDeg, float *x, float *y);
// Стрелки выбранного в out (до cap примитивов kind=2 line). Возвращает число.
int ui_gizmo_build(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H,
                   UiPrim *out, int cap);
// Трансформ трогали drag (тесту надо перекачать суп). Сбрасывается чтением.
int ui_gizmo_posed(UiGizmo *g);
// Проекция мировой точки в экран (y вниз). 0 если за камерой.
int ui_gizmo_project(const RcView *v, int W, int H, const float w[3], float *sx, float *sy);

#endif // ENGINE_UI_GIZMO_H
