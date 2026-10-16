// Дерево виджетов конструктора (§14): модель данных, чистые операции.
// Без UI/GL/рендера: add/remove/find/reparent + save/load JSON-секции.
// Пулы фиксированные (malloc нет вообще). Координаты — якоря+оффсеты
// (независимость от размера окна, как WPF-margins, не абсолютные px).
#ifndef ENGINE_UI_WIDGET_H
#define ENGINE_UI_WIDGET_H

#define UI_MAX_WIDGETS 64
#define UI_MAX_WIDGET_KIDS 8
#define UI_WIDGET_NAME 48
#define UI_WIDGET_HANDLER 64

// Типы v1: panel/button/label. Остальное (field/slider/check/separator/
// viewport) — позже списком, не кодом.
#define UI_W_PANEL 0
#define UI_W_BUTTON 1
#define UI_W_LABEL 2

// Якоря rect (битмаска): какие края прибиты. Разрешение при окне W×H:
// L: x = оффсет слева; R без L: x = W - x - w; иначе центр + x.
// T/B аналогично по Y. w,h всегда явный размер.
#define UI_A_LEFT 1
#define UI_A_TOP 2
#define UI_A_RIGHT 4
#define UI_A_BOTTOM 8

typedef struct {
    int anchors; // UI_A_*
    float x, y, w, h; // оффсеты/размер (см. правило выше)
} UiWidgetRect;

typedef struct {
    float bg[4];
    float rounding; // px, 0 = остро
    float alpha; // 0..1, множитель поверх bg[3] (применяет рендер F3)
    float fontSize; // px текста (label/button)
} UiWidgetStyle;

typedef struct {
    char handler[UI_WIDGET_HANDLER]; // кнопка on_click ("" = нет)
} UiWidgetBind;

typedef struct {
    int id; // index+1 (как Entity), 0 = нет
    int type; // UI_W_*
    char name[UI_WIDGET_NAME];
    int parent; // id родителя, 0 = корень
    int children[UI_MAX_WIDGET_KIDS]; // id детей (ведём руками, как layHist)
    int nchildren;
    UiWidgetRect rect;
    UiWidgetStyle style;
    UiWidgetBind bind;
} UiWidget;

typedef struct {
    UiWidget items[UI_MAX_WIDGETS];
    int alive[UI_MAX_WIDGETS];
    int count;
} UiWidgetTree;

void widget_tree_init(UiWidgetTree *t);
// Создать (parent 0 = корень). Возвращает id, 0 = отказ (пул полон,
// нет родителя, дети родителя полны). rect/style NULL = нули.
int widget_add(UiWidgetTree *t, int type, const char *name, int parent,
               const UiWidgetRect *rect, const UiWidgetStyle *style);
// Удалить с поддеревом. 1 ок, 0 нет такого.
int widget_remove(UiWidgetTree *t, int id);
// Найти по имени (первый). 0 = нет.
int widget_find(const UiWidgetTree *t, const char *name);
// Перевесить к новому родителю (0 = корень). Циклы/чужие/переполнение = 0.
int widget_reparent(UiWidgetTree *t, int id, int newParent);
// Указатель (живёт пока дерево). 0 = нет.
UiWidget *widget_get(UiWidgetTree *t, int id);
const UiWidget *widget_cget(const UiWidgetTree *t, int id);
// Разрешить rect в пиксели при окне W×H (см. правило якорей выше).
void widget_rect_px(const UiWidget *w, int W, int H, float *x, float *y, float *ww,
                    float *hh);
// Секция "widgets" в layout.json (ключи доков не трогаем; нет файла =
// пишем минимальный). 1 ок, 0 отказ (файл чужой/память — не портим).
int widget_json_save(const UiWidgetTree *t, const char *path);
int widget_json_load(UiWidgetTree *t, const char *path);

#endif // ENGINE_UI_WIDGET_H
