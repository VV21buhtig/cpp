// Шов EngUi: движок <-> любая UI-реализация (свой код, RmlUi, ImGui).
// UI отдаёт ПРИМИТИВЫ (не пиксели): движок рисует их своим 2D-ядром.
// Совместимо 1:1 с RcVecShape (kind/a,b,c,d/rgba): kind 0=rect (Фаза 1),
// 1=circle, 2=line. uv/tex зарезервированы под глифы (Фаза 2).
// В цикле кадра malloc нет: UiFrame — фикс.массив внутри EngUi.
#ifndef ENGINE_UI_H
#define ENGINE_UI_H

#include "ui_bind.h" // UiReply/UiAttrF шва (по Slate: FReply + TAttribute)

// UiPrim/UiFrame/UiEvent — как было (см. ниже исходные комменты).

#define UI_MAX_PRIMS 256
#define UI_EV_MOVE 0
#define UI_EV_DOWN 1
#define UI_EV_UP 2
#define UI_EV_WHEEL 3
#define UI_EV_KEY 4

typedef struct {
    int kind; // 0 rect: x,y,w,h
    float a, b, c, d;
    float rgba[4];
} UiPrim;

typedef struct {
    UiPrim prims[UI_MAX_PRIMS];
    int n;
} UiFrame;

typedef struct {
    int kind; // UI_EV_* (мышь в пикселях окна, y вниз от верха)
    float x, y;
    int button; // 0 левая
    int key; // ASCII (Q/W/E/R/Z/Y) или код
    int mods; // биты как в GLFW: 1 shift, 2 ctrl, 4 alt
    float value; // wheel-дельта
} UiEvent;

typedef struct EngUi EngUi;

// build: собрать дерево (user) + вернуть кадр примитивов.
// Возвращает 1 если кадр ГРЯЗНЫЙ (надо перезалить в ядро), 0 если тот же.
EngUi *eng_ui_create(void);
void eng_ui_destroy(EngUi *ui);
// Событие ввода -> hit-test/state. Reply как FReply (handled+dirty+capture).
UiReply eng_ui_event(EngUi *ui, const UiEvent *e);
// Построить UiFrame (только если грязно, иначе вернуть прошлый).
// out принадлежит ui, не free(). Возвращает 1 если грязный.
int eng_ui_build(EngUi *ui, int w, int h, const UiFrame **out);

#endif // ENGINE_UI_H
