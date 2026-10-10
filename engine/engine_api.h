#ifndef ENGINE_API_H
#define ENGINE_API_H
// Движок-инструмент: слеп к контенту. Окно + ввод + цикл, ничего про
// воксели/чанки/текстуры. Игра (или редактор) даёт готовое ядро RenderCore*
// и два колбэка: чем кормить (feed) и откуда смотреть (view).
// Ядро движок не создаёт и не выбирает — это дело игры/редактора.
#include "render_api.h"

typedef struct EngEngine EngEngine;

// Состояние ввода на кадр (снимок, не события).
typedef struct {
    const unsigned char *keys; // GLFW keycode -> 1 нажата (размер GLFW_KEY_LAST+1)
    double mouseDX, mouseDY;   // дельта за кадр
    int mouseBtn[8];           // кнопки мыши
    double scroll;             // колесо за кадр (сбрасывается)
} EngInput;

typedef struct {
    void *ctx;
    // Заполнить вид (камера/свет/грейд) на этот кадр.
    void (*view)(void *ctx, RcView *v);
    // Залить данные в ядро (чанки/примитивы/пиксели — что ядро ест).
    void (*feed)(void *ctx, RenderCore *rc);
    // Прочитать ввод (вызывается после poll, до view).
    void (*input)(void *ctx, const EngInput *in);
} EngHandlers;

// Окно w×h создаётся здесь; хинты контекста — по бэкенду ядра (RC_BACKEND_*).
// rc должно жить дольше движка (создаёт игра). NULL-колбэки разрешены.
EngEngine *eng_create(RenderCore *rc, int w, int h, const char *title,
                      const EngHandlers *hh);
void eng_destroy(EngEngine *e);
// Один кадр: poll -> input -> view -> feed -> core->frame. 1 закрыли, 0 идём.
// Кап 60fps внутри (nanosleep).
int eng_frame(EngEngine *e);
// Цикл до закрытия окна. Возвращает число кадров.
int eng_run(EngEngine *e);
#endif
