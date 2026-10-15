// Панели редактора: менюбар + тулбар + иерархия + статусбар.
// Прямоугольники идут в UiFrame (vec-ядро), текст — через ui_text.
// Состояние: selected entity, tool mode, menu-flash. Всё retained.
#ifndef ENGINE_UI_PANELS_H
#define ENGINE_UI_PANELS_H

#include "scene.h"
#include "ui.h"
#include "ui_text.h"

typedef struct UiPanels UiPanels;

UiPanels *ui_panels_create(void);
void ui_panels_destroy(UiPanels *p);
// Ввод (мышь y вниз, клавиши Q/W/E/R). 1 если грязно.
int ui_panels_event(UiPanels *p, ScWorld *scene, const UiEvent *e);
// Построить rect-примитивы (dirty-паттерн как eng_ui_build).
int ui_panels_build(UiPanels *p, ScWorld *scene, int w, int h, const UiFrame **out);
// Текст поверх (имена, режимы, статус). Всегда рисует (дешёвые квады).
void ui_panels_text(UiPanels *p, ScWorld *scene, UiText *text, int w, int h);

unsigned ui_panels_selected(const UiPanels *p);
int ui_panels_tool(const UiPanels *p); // 0 select 1 move 2 rotate 3 scale

#endif // ENGINE_UI_PANELS_H
