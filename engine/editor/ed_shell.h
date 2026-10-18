// ed_shell: ОБОЛОЧКА РЕДАКТОРА (ХОСТ) — окно: доки, сплиттеры, вьюпорт и
// СЕМЕЙСТВО ПАНЕЛЕЙ-ИНСТРУМЕНТОВ (хром-дерево, порядок, отрыв от дока, персист).
//
// ПОЧЕМУ ОТДЕЛЬНЫМ ФАЙЛОМ: где стоят доки, где экран игры, сколько юзер тянет
// сплиттер, какая панель сверху — это ХОСТ, а не режим. Иначе оболочка врастает в
// режим копиями номеров и порогов (integration-notes §10: «оболочка снова врастает
// в режим», жалоба тестера 18.10). Режим СПРАШИВАЕТ геометрию — не считает её сам.
//
// ГРАНИЦА: механика окна и панелей. Содержимое панели рисует РЕЖИМ (его ряды), но
// геометрию панели даёт оболочка (ed_shell_panel_rect). Ни сцены, ни GL.
//
// ХРАНЕНИЕ: экземпляр (EdShell) держит ПРИЛОЖЕНИЕ и отдаёт сюда указателем — как
// кадровый буфер. Истина о раскладке и панелях — в функциях ниже.
#pragma once

#include <stdio.h> // FILE: свои поля режима пишутся в тот же файл состояния

#include "ui/ui_widget.h"        // UiWidgetTree: панель = виджет (self-host)
#include "editor/ed_workspace.h" // ED_PANEL_MAX (сколько панелей у режима)

// --- СЕТКА И ПОЛОСЫ ХРОМА: одна точка правды для хоста и режима ---
#define ED_PAD 8       // внутренние поля панели/полосы (сетка редактора)
#define ED_TREE_W 200  // ширина панели левого дока (от неё считают палитра/дерево)
#define ED_DOCK_W (ED_TREE_W + 2 * ED_PAD) // ширина полосы дока по умолчанию
#define ED_DOCKWIN_MIN 96 // уже не сужаем: панель перестаёт читаться
#define ED_DOCKH_MIN 60   // то же для НИЖНЕГО дока: меньше — не видно ни строки
#define ED_SPLIT_HIT 7    // зона захвата сплиттера у края дока (px)
#define ED_TITLE_H 22     // полоса ЗАГОЛОВКА панели (за неё панель тащат)
#define ED_PANEL_GAP 10   // зазор между панелями одного дока (стопка)
#define ED_SHELL_PATH "engine/assets/ed_panels.json"       // состояние инструментов
#define ED_SHELL_BENCH_PATH "engine/assets/ed_panels.bench.json" // то же для тест/бенч
#define ED_TOP_CHROME 0   // легаси-полос в окне редактора нет: их несёт лента
#define ED_TABS_H 26      // полоса заголовков лент
#define ED_RIBBON_H 40    // полоса кнопок активной ленты

// Что тянет сплиттер (у доков три края — один механизм, не три сплиттера).
enum { ED_SP_NONE = 0, ED_SP_LEFT = 1, ED_SP_RIGHT = 2, ED_SP_BOTTOM = 3 };

// Что РЕЖИМ обязан сообщить оболочке (без этого она не поставит панель в док и не
// сохранит его состояние). Всё остальное — у самого режима.
typedef struct EdShellMode {
    // Желаемый размер панели i: h = высота содержимого, minh = минимум (одна
    // строка). minh == 0 — режим сам следит за размером (оболочка не зажимает).
    void (*panel_size)(int i, float *h, float *minh);
    // Свои поля режима в ОБЩИЙ файл состояния (активная лента, инструмент сцены):
    // пишем ВНУТРЬ объекта (после полей хоста), читаем из строки файла.
    void (*state_write)(FILE *f);
    void (*state_read)(const char *json);
} EdShellMode;

// ОБОЛОЧКА. Кадр ЧИТАЕТ поля; менять — только через ed_shell_* (иначе раскладка
// разъезжается: полосы перестают совпадать с вьюпортом, порядок панелей — с z).
typedef struct EdShell {
    // --- РАСКЛАДКА ОКНА (числа) ---
    int dockWl, dockWr, dockH; // размеры доков (их тянет юзер сплиттером)
    int dockL, dockR, dockB;   // занятые полосы кадра (0 = дока нет)
    int vpx, vpy, vpw, vph;    // вьюпорт (экран игры) = окно минус доки
    int open[ED_PANEL_MAX];    // открытость панелей режима (0 = закрыта)
    int split;                 // ED_SP_*: что тянут прямо сейчас (0 = нет)
    double splitX0, splitY0;   // где взяли (для dx/dy)
    int splitW0;               // размер дока на момент захвата
    // --- ПАНЕЛИ-ИНСТРУМЕНТЫ (хром-дерево) ---
    // Свой UI правщика живёт ВТОРЫМ деревом: панель = виджет-окно, его рисует ТОТ
    // ЖЕ рантайм, что документ. Порядок корней = порядок картинки (верх = последний).
    UiWidgetTree wt;
    int panel[ED_PANEL_MAX];   // id виджетов-панелей (индекс = индекс режима)
    int moved[UI_MAX_WIDGETS]; // 1 = юзер оторвал от дока (дефолтом не перетираем)
    int order[ED_PANEL_MAX];   // порядок панелей: ПОСЛЕДНИЙ = верхний
    int grab;                  // какую панель тащат за заголовок (-1 = нет)
    float grabDX, grabDY;      // где взяли внутри панели
    // --- ДВЕРИ РЕЖИМА и герметичность ---
    EdShellMode mode;          // что РЕЖИМ сообщает оболочке (копия, не указатель)
    int no_save;               // 1 = файл состояния не писать (--selftest)
} EdShell;

// Режим отдаёт оболочке свои двери (зовётся ДО ed_shell_init).
void ed_shell_mode(EdShell *S, const EdShellMode *m);
// Герметичность: 1 = не писать файл состояния (тест/прогон юзера не пачкаем).
void ed_shell_no_save(EdShell *S, int v);
// Создать хром: дерево + виджеты-панели по таблице режима (имя/вид — из записи).
void ed_shell_init(EdShell *S);
// Расставить панели по их докам (закрытые = 0x0, оторванные не трогаем).
void ed_shell_sync(EdShell *S, int ww, int wh);
// «Домашнее» место панели в доке — БЕЗ движения виджета (сравнения/док-зона).
void ed_shell_panel_home(const EdShell *S, int ww, int wh, int i, float *x, float *y, float *w,
                         float *h);
// Панель не выезжает за окно ни одной стороной (таск + ресайз).
void ed_shell_panels_keep_inside(EdShell *S, int ww, int wh);
// Состояние инструментов: путь (в тест/сид-режиме — свой файл), запись, чтение.
const char *ed_shell_path(void);
void ed_shell_save(const EdShell *S);
void ed_shell_load(EdShell *S, int ww, int wh);

void ed_shell_reset(EdShell *S); // дефолты доков + открытость панелей из режима
int ed_shell_top(void);          // верх инструментов (под полосой лент)
// Пересчитать полосы доков и вьюпорт по тому, что открыто у режима.
void ed_shell_compute(EdShell *S, int ww, int wh);
// Поставить размер дока (1/2/3) — с клампом: панель не меньше минимума, а
// вьюпорт не схлопывается в ноль.
void ed_shell_dock_set(EdShell *S, int which, int v, int ww, int wh);
// Кламп ПАРЫ боковых доков: значения из файла недоверенные, и сумма не имеет
// права съесть минимальный вьюпорт.
void ed_shell_clamp_all(EdShell *S, int ww);
// Полоса захвата сплиттера: у боковых доков вертикальная (тянут по X), у
// нижнего горизонтальная (по Y).
void ed_shell_split_rect(const EdShell *S, int which, int ww, int wh, float *x, float *y,
                         float *w, float *h);
int ed_shell_split_hit(const EdShell *S, int ww, int wh, float px, float py, int *which);

// --- ПАНЕЛИ-ИНСТРУМЕНТЫ: одна дверь на порядок/геометрию/отрыв/возврат ---
// Открыта ли вкладка инструмента (закрытая не рисуется и не ловит клик).
int ed_shell_panel_open(const EdShell *S, int i);
// ЖИВАЯ панель: открыта И не вырождена (rect 0x0 — содержимое ОБЯЗАНО молчать).
int ed_shell_panel_live(const EdShell *S, int i);
// Пиксельный rect панели (окно нужно для якорей — своих копий g_WW/g_WH хост не держит).
void ed_shell_panel_rect(const EdShell *S, int ww, int wh, int i, float *x, float *y, float *w,
                         float *h);
// Оторвана от дока (юзер двигал: sync её больше не возвращает).
int ed_shell_panel_float(const EdShell *S, int i);
// Кнопка «в док» у оторванной панели — в правом конце её заголовка.
void ed_shell_float_chip(const EdShell *S, int ww, int wh, int i, float *cx, float *cy, float *cw,
                         float *ch);
// Верхняя панель в точке (порядок панелей: верхняя проверяется первой).
int ed_shell_panel_top(const EdShell *S, int ww, int wh, float px, float py);
// Полоса ЗАГОЛОВКА верхней панели: за неё панель тащат/отрывают (как за таб окна).
int ed_shell_panel_head_at(const EdShell *S, int ww, int wh, float px, float py, int *which);
// Поднять панель наверх: и в порядке, и в z дерева хрома (порядок корней = картинка).
void ed_shell_panel_raise(EdShell *S, int i);
// Вернуть панель в свой док (снять «оторвана») / поставить признак отрыва.
void ed_shell_panel_dock(EdShell *S, int i);
void ed_shell_panel_moved(EdShell *S, int i, int v);
