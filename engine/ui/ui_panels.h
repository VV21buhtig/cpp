// Панели редактора: менюбар + тулбар + иерархия + статусбар.
// Прямоугольники идут в UiFrame (vec-ядро), текст — через ui_text.
// Состояние: selected entity, tool mode, menu-flash. Всё retained.
#ifndef ENGINE_UI_PANELS_H
#define ENGINE_UI_PANELS_H

#include "scene.h"
#include "ui.h"
#include "ui_bind.h" // UiReply шва
#include "ui_text.h"
#include "ui_undo.h" // общий стек (коммит гизмо идёт туда же)
#include "ui_undo.h"

typedef struct UiPanels UiPanels;

UiPanels *ui_panels_create(void);
void ui_panels_destroy(UiPanels *p);
// Явное сохранение раскладки доков в path (чужая секция "widgets" переносится).
int ui_panels_save_layout(UiPanels *p, const char *path);
// Ввод как FReply: handled + захват мыши (см. ui_bind.h). Dirty внутри reply.
UiReply ui_panels_event(UiPanels *p, ScWorld *scene, const UiEvent *e);
// Построить rect-примитивы (dirty-паттерн как eng_ui_build).
int ui_panels_build(UiPanels *p, ScWorld *scene, int w, int h, const UiFrame **out);
// Текст поверх (имена, режимы, статус). Всегда рисует (дешёвые квады).
void ui_panels_text(UiPanels *p, ScWorld *scene, UiText *text, int w, int h);

unsigned ui_panels_selected(const UiPanels *p);
int ui_panels_tool(const UiPanels *p); // 0 select 1 move 2 rotate 3 scale
int ui_panels_edit(const UiPanels *p); // 1 ручной режим (§11): гизмо скрыто
// Ховер хрома (1 над сплиттером/коллапсом — курсор-резайз, иначе 0).
int ui_panels_hover(const UiPanels *p, float x, float y);
// Прямоугольник вьюпорта (док-модель UE: остаток после панелей).
void ui_panels_viewport(const UiPanels *p, int W, int H, int *x, int *y, int *w, int *h);
// Общий стек undo (туда же коммитит гизмо).
UiUndo *ui_panels_undo(UiPanels *p);
// Трансформ трогали скрабом (тесту надо перекачать суп). Сбрасывается чтением.
int ui_panels_posed(UiPanels *p);

#endif // ENGINE_UI_PANELS_H
