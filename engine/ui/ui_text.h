// Фаза 2: атлас глифов (stb_truetype, vendored public domain).
// ASCII 32..126, один размер (16px), R8-текстура. malloc только в init.
// Кадр: ui_text_quad() складывает квады в фикс.массив (без malloc),
// ui_text_flush() рисует своей программой поверх всего (бленд, без глубины).
#ifndef ENGINE_UI_TEXT_H
#define ENGINE_UI_TEXT_H

#define UI_TEXT_MAXQ 512 // квадов на кадр хватит с запасом

typedef struct UiText UiText;

UiText *ui_text_init(const char *fontPath, float px);
void ui_text_destroy(UiText *t);
// Текст влево от (x,y) — верх строки. y вниз (как виджеты).
// Возвращает ширину px (для layout), 0 если не влезло/нет атласа.
float ui_text_quad(UiText *t, float x, float y, const char *s, const float rgba[4]);
// Нарисовать накопленное в текущую цель W×H (Y вниз -> ortho сам флипает).
void ui_text_flush(UiText *t, int w, int h);
unsigned ui_text_tex(const UiText *t);
// Ширина строки в px БЕЗ отрисовки (каретка/центрирование). Printable ASCII —
// как ui_text_quad; остальное — полширины (в атласе нет глифа).
float ui_text_width(UiText *t, const char *s);

#endif // ENGINE_UI_TEXT_H
