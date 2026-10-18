// ed_cmd: КОМАНДНОЕ ЯДРО РЕДАКТОРА (хост-приложение), а не «часть ui_edit».
//
// ОДНА таблица на все виды одного действия: кнопка ленты, строка палитры,
// клавиша, тултип, строка шпаргалки, тумблер панели. Как TCommands/FUICommandInfo
// во UE 4.22 (объявил действие один раз — оно само попадает в панели, меню и в
// список горячих клавиш) и FInputBindingManager.
//
// ГРАНИЦА (почему это отдельный файл и почему он в engine/editor/):
//   ядро НЕ знает ни виджетов, ни сцены. Оно знает «запись», «клавишу»,
//   «включена ли», «что исполнить». Исполнение приходит СНАРУЖИ — колбэками
//   EdCmdHost, которые ставит приложение (сегодня ui_edit, дальше редактор и
//   его режимы). Если здесь появится ui_widget.h/scene.h — граница порвана.
#pragma once

#include "rhi_gl.h" // GLFW_KEY_* / GLFW_MOD_* (коды клавиш — часть ввода хоста)

// Гейт canExecute: выключенное действие рисуется приглушённо и НЕ срабатывает —
// так честно помечено то, что ещё НЕ сделано (никаких кнопок-пустышек).
enum { ED_WHEN_ALWAYS = 0, ED_WHEN_SEL = 1, ED_WHEN_UNDO = 2, ED_WHEN_REDO = 3 };
// Вид исполнения записи. Ядро вид НЕ толкует: толкует тот, кто дал колбэк run.
enum { ED_K_CMD = 0, ED_K_SPAWN = 1, ED_K_TOGGLE = 2, ED_K_TOOL = 3 };
// ДОМЕН: чей это инструмент — сам редактор/интерфейс (FRONT) или игровой мир
// (SCENE). Нужен, чтобы в одном реестре мир и редактор не сваливались в кучу.
enum { ED_DM_FRONT = 0, ED_DM_SCENE = 1 };

// Где запись живёт СВОИМ видом (тогда в ленте её нет: одна команда — одна кнопка).
#define ED_V_PALETTE 1u // строка в панели-палитре
#define ED_V_DOCK 2u    // кнопка в шапке своей панели (например «Clear» консоли)

#define ED_CMD_MAX 128 // записей в реестре; больше — реестр откажет (и скажет)

#define ED_MOD_CTRL GLFW_MOD_CONTROL
#define ED_MOD_CTRLSHIFT (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)
#define ED_MOD_MASK (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)

typedef struct {
    const char *id;    // машинный id (тесты/персист), уникален
    const char *label; // человеческая метка (лента/палитра/тултип/шпаргалка)
    const char *group; // группа на ленте (подложка + подпись)
    unsigned char tab; // 0..ED_TABS_N-1 (какая лента показывает)
    unsigned char when;
    unsigned char kind;
    unsigned char views; // ED_V_PALETTE / ED_V_DOCK
    short arg;           // довод вида: spawn-тип / индекс панели / id встроенной команды
    int key, mods, key2, mods2; // аккорды (key2 — второй аккорд той же команды)
    unsigned char domain;       // ED_DM_*
} EdCmd;

// Сервисы, которые запись вызывает. Ставит приложение ОДИН раз: ядро само не
// решает, что такое «создать виджет» или «переключить панель».
typedef struct {
    int (*enabled)(int i); // canExecute (0 = приглушена и не срабатывает)
    int (*on)(int i);      // состояние тумблера/радио (галочка на ленте)
    int (*run)(int i);     // исполнение (ядро зовёт его ТОЛЬКО после гейта)
} EdCmdHost;

void ed_cmd_host(const EdCmdHost *h); // поставить сервисы (до первого вызова)
void ed_cmd_reset(void);              // очистить реестр (тесты/перезапуск)
int ed_cmd_add(const EdCmd *c);       // -> индекс записи или -1 (полно/пустой id)
int ed_cmd_n(void);                   // сколько записей
const EdCmd *ed_cmd_at(int i);        // запись по индексу (0 = нет)
const EdCmd *ed_cmd_all(void);        // весь реестр (виды читают его списком)
int ed_cmd_find(const char *id);      // -> индекс или -1

const char *ed_cmd_label_of(int kind, int arg); // метка записи по (вид, довод)
int ed_cmd_ribbon(int i);                       // рисуется ли запись в ленте
int ed_cmd_enabled(int i);                      // canExecute
int ed_cmd_on(int i);                           // состояние тумблера
int ed_cmd_run(int i);                          // выполнить (с гейтом)

int ed_cmd_by_key(int mods, int key); // клавиша -> запись (Ctrl/Shift значимы)
const char *ed_cmd_key_name(int key, int mods, char *buf, int cap);
void ed_cmd_keys(int i, char *buf, int cap); // все аккорды через " / "
