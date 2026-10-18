// ui_edit: РЕЖИМ «UI» + пока ещё и приложение (исправление 18.10: оболочка —
// редактора, режим — это он). Правщик фронта (§13): своё окно, без 3D.
// Разделение файлов: editor/ed_cmd.* = командное ядро ХОСТА (реестр/клавиши),
// editor/ed_shell.* (A3) = доки/панели/лента/консоль, mode_ui.* (A4) = режим.
// Файлы-обменники с игровым редактором: layout.json + theme.json
// (коммиты пишут сами панели: lay_commit / ui_theme_save).
// Запуск из корня репо (шейдеры относительные, как ui_test): ./engine/build/ui_edit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp (ручки теста: имя ленты)
#include <math.h>
#include <unistd.h> // F5h: access() — герметичность selftest (бэкап раскладки)
#include "rhi_gl.h" // ПЕРВЫМ: GLFW_INCLUDE_NONE + glad (иначе gl.h-конфликт)
#include "render_api.h"
#include "ui/ui_panels.h" // меню/тулбар/иерархия/статус + EDIT-правка
#include "ui/ui_widget.h" // дерево виджетов конструктора (§14)
#include "ui/ui_widget_undo.h" // §14 F4: кольцо истории документа
#include "ui/ui_widget_runtime.h" // §14 F3c: общий порядок/рендер/хит (игре и редактору)
#include "ui/ui_text.h" // подписи + хинт инструмента
#include "ui/ui_log.h" // Ш1c: кольцо строк консоли (данные; вид — панель)
#include "editor/ed_cmd.h" // командное ядро РЕДАКТОРА: реестр один на всё
#include "editor/ed_doc.h" // A2: документ как интерфейс ХОСТА
#include "editor/ed_workspace.h" // A3: режим редактора (ленты/панели/доки) — данные
#include "rtarget.h"
#include "comp_gl.h"
#include "comp.h"
#include "rhi_gl.h"

#define W 1280
#define H 720 // дефолт до первого замера
#define UI_LAYOUT_PATH "engine/assets/layout.json" // обменник с игровым редактором

static int g_WW = W, g_WH = H; // живой размер (фреймбуферные пиксели)
static float g_mouseKx = 1.0f, g_mouseKy = 1.0f;
static double g_mx = 0, g_my = 0;
static UiPanels *g_panels = 0; // НЕ рисуется в окне: только РЕЖИМ инструмента сцены
static RenderCore *g_vec = 0;
// Игровой мир (§14): то, на что смотрят инструменты ленты MODEL (Move/Rotate/Scale).
// Фронт-редактор его НЕ рисует — вьюпорт показывает ЭКРАН ИГРЫ (документ UI), а
// сам мир рисует игра/игровой редактор (там же живёт гизмо). Держим мир здесь как
// адрес правки, чтобы домены не смешивались: MODEL правит сцену, HOME/UI — фронт.
// Точка замены: вид сцены в редакторе (§14 F3c(1c) — игрок/мир в отдельном окне).
static ScWorld g_scene;
static GLFWcursor *g_curArrow = 0, *g_curResize = 0;
static int g_quit = 0;
static float g_fpsEma = 0;   // оверлей: живой FPS (жалоба «со временем деградирует»)
static double g_lastT = 0;
static int g_fullN = 0;      // счётчик полных edit_push (диагноз: full на MOVE = лаг)
// Бенч драга (UI_EDIT_BENCH=rect|full): синтетический драг K шагов, замер FPS.
#define BENCH_K 600
static int g_benchK = BENCH_K; // переопределяется UI_EDIT_BENCH_K
static int g_bench = 0, g_forceFull = 0, g_benchId = 0, g_benchNoEv = 0;
static int g_benchSpawn = 0, g_benchNSpawn = 10; // UI_EDIT_BENCH=spawn: залп спавнов
static int g_noDirty = 0; // UI_EDIT_FULL=1: всегда полный кадр (диагностика шлейфа)
static int g_benchResize = 0;
static int g_benchCorner = 0;
static int g_benchEvents = 1;  // MOVE-событий за кадр (реальная мышь)
static int g_benchNoCoal = 0;  // 1 = без коалесинга (старое поведение)
static float g_benchX = 0, g_benchY = 0;
static double g_benchT0 = 0;
// Rect плейсхолдера вьюпорта (из последнего push — для подписи).
static int g_vpx = 0, g_vpy = 0, g_vpw = 0, g_vph = 0;

// --- §14 F2a: дерево виджетов + палитра (tool chrome, не модель) ---
// Холст = всё окно (WYSIWYG: доки правятся тем же UiPanels). Палитра — поверх
// вьюпорта. Пока только в памяти: в layout.json не пишем (персист — след. шаг,
// иначе конфликт писателей layout.json с доками).
static UiWidgetTree g_wt;
static int g_wtSel = 0;   // выбранный виджет (id, 0 = нет)
static int g_palHot = -1; // под палитрой (для подсветки)
static int g_wtN[3] = {0, 0, 0}; // счётчики имён по типу

#define PAL_BW 104
#define PAL_BH 26
#define PAL_GAP 4
// Ряды палитры больше НЕ список-константа: палитра — ВИД реестра действий
// (ed_pal_n/ed_pal_act ниже). Числа строк здесь нет — оно вычисляется.
#define PAL_PAD 8    // внутренние поля хром-панели
#define PAL_TITLE 22 // строка заголовка панели
// Верх легаси-хрома редактора. БЫЛО 84 (меню 0..26 + тулбар 26..58 + строка
// коллапса 58..84 из ui_panels). Теперь 0: легаси-полос в окне редактора НЕТ —
// их функции несёт наша лента (HOME/UI/VIEW — фронт редактора, MODEL — сцена
// игры), поэтому окно начинается сразу с полосы заголовков лент. Тестер: «ты
// убрал нормальные и оставил легаси».
#define ED_TOP_CHROME 0
#define WT_HANDLE 8.0f // сторона нарисованного квадрата ручки ресайза
#define TREE_W 200     // ширина панели дока (палитра/дерево/инспектор считают от неё)
// §14 F5k: ПОЛОСА ВКЛАДОК-ИНСТРУМЕНТОВ (как View в Word / панели в Roblox
// Studio). Панели — это ИНСТРУМЕНТЫ: их ОТКРЫВАЮТ вкладкой, они стоят в доке,
// а «вынуть в отдельное окно» — отдельное действие (драг за заголовок панели).
#define ED_TABS_H 26
#define ED_TAB_W 96
#define ED_TAB_GAP 4
// Строки палитры и имена типов виджетов берутся ИЗ РЕЕСТРА ДЕЙСТВИЙ (§14
// F5l(2) ниже): ed_pal_* дают строку палитры, wt_type_name — имя типа. Раньше
// здесь лежал ВТОРОЙ список тех же строк («Panel/Button/Label/Delete») — дубль
// реестра, который мог с ним разойтись.
static int ed_pal_n(void);
static int ed_pal_act(int row);
static const char *ed_pal_label(int row);
static int ed_pal_is_action(int row);
static const char *wt_type_name(int type);
static void ed_host_init(void); // ХОСТ: реестр команд + документы

// --- §14 F3c(1b): ХРОМ РЕДАКТОРА = ВИДЖЕТЫ (self-host) ---
// Свой UI правщика живёт ВТОРЫМ деревом (g_edWt): палитра/дерево/инспектор — это
// виджеты, их геометрия берётся отсюда, рисует их ТОТ ЖЕ рантайм, что документ.
// F7 (env UI_EDIT_SELF=1) переключает, ЧТО правим: документ (g_wt, пишется в
// layout.json) или хром самого редактора (g_edWt, не пишется — это вид инструмента).
static UiWidgetTree g_edWt;
static int g_edPal = 0, g_edTree = 0, g_edIns = 0; // id хром-панелей
static int g_edMoved[UI_MAX_WIDGETS]; // 1 = юзер подвинул: дефолтом не перетираем
static int g_editSelf = 0;            // 1 = правим хром редактора
static UiWidgetTree *g_act = &g_wt;   // АКТИВНОЕ дерево (инспектор/драг/дерево)
// §14 F5j: ПЛАВАЮЩИЕ ПАНЕЛИ. Панель тащат за полосу ЗАГОЛОВКА: она отрывается от
// дока и висит ПОВЕРХ всего (как окно в оконном менеджере: и отрисовка, и клик
// идут сверху вниз по порядку). У оторванной в заголовке кнопка «dock», а бросок
// в СВОЁ док-место (док-зона) возвращает её назад.
// §14 Ш1c: ПАНЕЛИ-ИНСТРУМЕНТЫ режима — это ДАННЫЕ (таблица режима ниже,
// editor/ed_workspace.h): id, подпись, док, открытость. Здесь только ХРАНИЛИЩЕ:
// сколько панелей — ed_ws_panels(), а ED_PANEL_MAX — ЁМКОСТЬ массивов (на самый
// широкий режим), а не «сколько их у нас». Куда панель встаёт — ed_ws_dock(i).
static int g_edPanels[ED_PANEL_MAX];                           // id виджетов-панелей
static int g_edOrder[ED_PANEL_MAX] = {0, 1, 2, 3, 4, 5, 6, 7}; // ПОСЛЕДНИЙ = верхний
static int g_edCon = 0;                                        // id панели КОНСОЛИ (Ш1c)
static int g_edGrab = -1;                    // 0..2: панель тащат за заголовок
static float g_edGrabDX = 0, g_edGrabDY = 0; // где взяли внутри панели
// Панель оторвана от дока (двинул юзер — sync её больше не возвращает): по этому
// признаку рисуем рамку и кнопку «dock».
static int ed_panel_float(int i) {
    int id = (i >= 0 && i < ed_ws_panels()) ? g_edPanels[i] : 0;
    return id > 0 && id <= UI_MAX_WIDGETS && g_edMoved[id - 1];
}
static void ed_float_chip(int i, float *cx, float *cy, float *cw, float *ch); // ниже
// §14 F5k: инструмент ОТКРЫТ/ЗАКРЫТ. Закрытая панель не рисуется, не ловит клик и
// не занимает место в доке (раскладка перетекает). Состояние — редактора, а не
// документа: своё маленькое хранилище (layout.json делят §11-доки и §14-документ).
static int g_edOpen[ED_PANEL_MAX]; // открытость панелей (дефолты — из режима)
static int g_selftest = 0;          // 1 = прогон --selftest: файлы юзера не трогаем
static void edit_push(void); // определения ниже: инспектор/вкладки зовут их из wt_event
static void edit_push_dirtyN(const int (*rects)[4], int cnt);
// F5l(3): документ живёт в вьюпорте — координаты/клип (определения ниже, нужны
// уже хит-тесту и стопке клика).
static int doc_w(void);
static int doc_h(void);
static float doc_ox(void);
static float doc_oy(void);
static int doc_live(void);
static void doc_px(const UiWidget *w, float *x, float *y, float *ww, float *hh);
static void ed_emit_clip(RcVecShape *sh, int *n, float x, float y, float w, float h,
                         const float rgba[4], float round);
// §14 F5l(2): ЛЕНТА. Полоса сверху несёт ЗАГОЛОВКИ лент (как Home/Model/UI/
// Script/View в Roblox Studio и Word), а под ними — ГРУППА КНОПОК активной
// ленты. Кнопки строятся из РЕЕСТРА ДЕЙСТВИЙ (одна таблица = лента + шпаргалка
// клавиш + тултипы + горячие клавиши). В UE 4.22 это TCommands/FUICommandInfo:
// объявил действие один раз — получил команду, клавишу и подсказку.
// Сколько лент у режима и как они зовутся — у САМОГО режима (ed_ws_tabs/ed_ws_tab),
// а не константой здесь: вторая лента = строка таблицы, а не правка циклов.
#define ED_RIBBON_H 40      // высота полосы кнопок активной ленты
#define ED_RIBBON_ITEM_H 20 // высота кнопки ленты
#define ED_RIBBON_GAP 4
#define ED_RIBBON_SEP 8 // зазор между группами
// Ленты/вкладки. Разделение по ДОМЕНУ: HOME/VIEW — САМ РЕДАКТОР и интерфейс,
// который в нём строят (фронт); MODEL — инструменты РЕДАКТОРА СЦЕНЫ (игровой мир).
// Тестер: «для самого редактора отдельно ... разделить, но чтобы функционал
// сохранился». Пустых вкладок не держим: вкладки UI нет, потому что ВСЁ её
// содержимое (Panel/Button/Label) — строки палитры, а вторая кнопка для той же
// команды читается как дубль («нажимаю Panel, иду в UI — почему тут опять Panel?»).
// SCRIPT не заводим — запуска/скриптов ещё нет.
static int g_edTab = 0;      // активная лента (0..ed_ws_tabs()-1)
static int g_edShowKeys = 0; // F5l(2): шпаргалка клавиш (F1) поверх всего
static int g_rbHot = -1;     // id действия под курсором (тултип), -1 = нет
static int ed_panel_open(int i) { return i >= 0 && i < ed_ws_panels() && g_edOpen[i]; }
// §14 F5l(5): ЖИВАЯ панель = открыта вкладкой И не вырождена. Закрытая получает
// rect 0x0, и её содержимое ОБЯЗАНО молчать: ряды считают координаты от панели,
// поэтому из нулевого rect они рисуются в угол (0,0) — «зебры» полосами 200x20
// поверх палитры, «появляются при выделении» (баг тестера: закрытый инспектор
// рисовал заголовок + IF_N полей). Тот же инвариант, что «скрытая вкладка не
// рисуется» у Word/Blender: закрыто = нет ни пикселя, ни хитбокса.
static int ed_live(int i) {
    if (!ed_panel_open(i)) return 0;
    const UiWidget *p = widget_cget(&g_edWt, g_edPanels[i]);
    return p && p->rect.w > 1.0f && p->rect.h > 1.0f;
}

// §14 F5l(4): ПОЛОСЫ ДОКОВ. Инструмент В ДОКЕ занимает свою полосу — слева
// палитра+дерево, справа инспектор, — а вьюпорт (экран игры) это ОСТАТОК окна.
// Раньше панели лежали поверх экрана игры: тестер «инструменты не должны быть во
// вьюпорте, нужно окно, которое отвечает за редакцию». Оторванная (F5j) панель —
// исключение: она окно и плавает поверх всего, пока её не вернут в док.
#define ED_DOCK_W (TREE_W + 2 * PAL_PAD) // ширина полосы дока по умолчанию
// §14 F5l(4b): СПЛИТТЕРЫ ДОКОВ. Ширина дока не константа: её тянет юзер за край
// дока (как в VS Code/Blender/UE). Это СОСТОЯНИЕ ИНСТРУМЕНТА, не документ, и
// живёт в том же файле, что остальное состояние (ed_panels.json) — второго файла
// не заводим (правило «одно состояние — один файл»).
#define ED_DOCKWIN_MIN 96 // уже не сужаем: панель перестаёт читаться
#define ED_DOCKH_MIN 60   // то же для НИЖНЕГО дока: меньше — не видно ни строки
#define ED_SPLIT_HIT 7    // зона захвата сплиттера у края дока (px)
static int g_dockWl = ED_DOCK_W; // ширина панели ЛЕВОГО дока (палитра/дерево)
static int g_dockWr = ED_DOCK_W; // ширина панели ПРАВОГО дока (инспектор)
static int g_dockH = 150;        // высота панели НИЖНЕГО дока (консоль, Ш1c)
static int g_split = 0;          // 0 нет; 1/2/3 = левый/правый/нижний тянут
static double g_splitX0 = 0;     // где взяли (для dx)
static double g_splitY0 = 0;     // где взяли (для dy нижнего дока)
static int g_splitW0 = 0;        // ширина/высота на момент захвата
static int g_dockL = 0, g_dockR = 0, g_dockB = 0; // занятые полосы доков (кадр)
// §14 F5l(2): верх инструментов = хром + полоса заголовков лент + полоса кнопок
// активной ленты. Доки и вьюпорт начинаются ПОД ней (одна точка правды).
static int ed_top(void) { return ED_TOP_CHROME + ED_TABS_H + ED_RIBBON_H + PAL_PAD; }
static void ed_viewport_compute(void) {
    // Кто открыт в каком доке — из ОДНОГО места (ed_panel_side), а не из номеров
    // панелей по всему файлу: пятая панель = одна строка в ed_panel_side.
    int leftOpen = 0, rightOpen = 0, botOpen = 0;
    for (int i = 0; i < ed_ws_panels(); i++) {
        if (!g_edOpen[i]) continue;
        int d = ed_ws_dock(i);
        if (d == ED_DK_LEFT) leftOpen = 1;
        else if (d == ED_DK_RIGHT) rightOpen = 1;
        else botOpen = 1;
    }
    g_dockL = leftOpen ? g_dockWl + PAL_PAD : 0;
    g_dockR = rightOpen ? g_dockWr + PAL_PAD : 0;
    g_dockB = botOpen ? g_dockH + PAL_PAD : 0; // нижний док режет экран игры снизу
    int top = ed_top(); // под полосой лент
    g_vpx = g_dockL;
    g_vpy = top;
    g_vpw = g_WW - g_dockL - g_dockR;
    g_vph = g_WH - top - g_dockB;
    if (g_vpw < 64) g_vpw = 64; // тесное окно: вьюпорт не схлопываем в ноль
    if (g_vph < 64) g_vph = 64;
}

// §14 F5l(4b): арифметика сплиттеров доков (см. ED_DOCK_* выше).
static void ed_dock_clamp(int i, int *w) {
    // Держим ДВЕ границы: панель не меньше минимума, и вьюпорт не схлопывается.
    if (i == 3) { // нижний док меряем по ВЫСОТЕ (экран игры остаётся сверху)
        int minh = ED_DOCKH_MIN;
        int maxh = g_WH - ed_top() - 64 - PAL_PAD; // 64 = минимальный вьюпорт
        if (maxh < minh) maxh = minh;              // тесное окно: минимум важнее
        int v = *w;
        if (v < minh) v = minh;
        if (v > maxh) v = maxh;
        *w = v;
        return;
    }
    int minw = ED_DOCKWIN_MIN;
    int other = (i == 1) ? (g_edOpen[2] ? g_dockWr + PAL_PAD : 0)
                         : ((g_edOpen[0] || g_edOpen[1]) ? g_dockWl + PAL_PAD : 0);
    int maxw = g_WW - other - PAL_PAD - 64; // 64 = минимальный вьюпорт
    if (maxw < minw) maxw = minw;           // тесное окно: минимум важнее
    int v = *w;
    if (v < minw) v = minw;
    if (v > maxw) v = maxw;
    *w = v;
}
// Кламп ПАРЫ (чтение из файла): значения недоверенные, мало поровну сузить —
// надо ещё не дать СУММЕ съесть минимальный вьюпорт (иначе доки налезут друг на
// друга, и «вьюпорт = остаток» станет враньём).
static void ed_docks_clamp_pair(void) {
    int gl = (g_edOpen[0] || g_edOpen[1]) ? PAL_PAD : 0;
    int gr = g_edOpen[2] ? PAL_PAD : 0;
    int room = g_WW - gl - gr - 64; // всё, что можно отдать докам
    if (room < 2 * ED_DOCKWIN_MIN) room = 2 * ED_DOCKWIN_MIN;
    int maxL = room - ED_DOCKWIN_MIN;
    if (maxL < ED_DOCKWIN_MIN) maxL = ED_DOCKWIN_MIN;
    if (g_dockWl < ED_DOCKWIN_MIN) g_dockWl = ED_DOCKWIN_MIN;
    if (g_dockWl > maxL) g_dockWl = maxL;
    int maxR = room - g_dockWl;
    if (g_dockWr < ED_DOCKWIN_MIN) g_dockWr = ED_DOCKWIN_MIN;
    if (g_dockWr > maxR) g_dockWr = maxR;
}
static void ed_dock_set(int i, int w) {
    ed_dock_clamp(i, &w);
    if (i == 1) g_dockWl = w;
    else if (i == 2) g_dockWr = w;
    else g_dockH = w; // нижний док (Ш1c)
}
// Полоса захвата сплиттера: у боковых доков ВЕРТИКАЛЬНАЯ (тянем по X), у
// нижнего ГОРИЗОНТАЛЬНАЯ (по Y). Один механизм на три дока — не третий сплиттер.
static void split_rect(int i, float *x, float *y, float *w, float *h) {
    if (i == 3) { // нижний док: по его ВЕРХНЕМУ краю, во всю ширину вьюпорта
        *h = (float)ED_SPLIT_HIT;
        *y = (float)(g_WH - g_dockB) - *h * 0.5f;
        *x = (float)g_dockL;
        *w = (float)(g_WW - g_dockL - g_dockR);
        if (*w < 1.0f) { // боковых доков нет — тянем во всю ширину окна
            *x = 0.0f;
            *w = (float)g_WW;
        }
        return;
    }
    *w = (float)ED_SPLIT_HIT;
    // Боковой док кончается НАД нижним доком: линия не режет консоль насквозь.
    *h = (float)(g_WH - g_dockB - ed_top());
    if (*h < 1.0f) *h = 1.0f;
    *y = (float)ed_top();
    if (i == 1) *x = (float)g_dockL - *w * 0.5f;    // левый док: его ПРАВЫЙ край
    else *x = (float)(g_WW - g_dockR) - *w * 0.5f;  // правый док: его ЛЕВЫЙ край
}
static int split_hit(float px, float py, int *which) {
    if (py < (float)ed_top()) return 0; // сплиттеры живут под лентой
    for (int i = 1; i <= 3; i++) {
        if (i == 1 && g_dockL <= 0) continue; // дока нет — тянуть нечего
        if (i == 2 && g_dockR <= 0) continue;
        if (i == 3 && g_dockB <= 0) continue;
        float x, y, w, h;
        split_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            if (which) *which = i;
            return 1;
        }
    }
    return 0;
}

// Полоса вкладок-инструментов — во всю ширину ОКНА (это лента редактора, не
// вьюпорт: доки начинаются ниже неё).
static void tabs_rect(float *x, float *y, float *w, float *h) {
    float w0 = (float)g_WW;
    if (w0 < (float)ED_TAB_W + 2 * PAL_PAD) w0 = (float)ED_TAB_W + 2 * PAL_PAD;
    *x = 0.0f;
    *y = (float)ED_TOP_CHROME;
    *w = w0;
    *h = (float)ED_TABS_H;
}
static void tab_rect(int i, float *x, float *y, float *w, float *h) {
    float bx, by, bw, bh;
    tabs_rect(&bx, &by, &bw, &bh);
    (void)bw;
    (void)bh;
    *x = bx + PAL_PAD + (float)i * (ED_TAB_W + ED_TAB_GAP);
    *y = by + 3.0f;
    *w = (float)ED_TAB_W;
    *h = (float)(ED_TABS_H - 6);
}
static int tabs_hit(float px, float py, int *which) { // вкладка под курсором
    for (int i = 0; i < ed_ws_tabs(); i++) {
        float x, y, w, h;
        tab_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            if (which) *which = i;
            return 1;
        }
    }
    return 0;
}
static void tabs_toggle(int i); // ниже: нужны sync/persist

// Пиксельный rect хром-панели (то же правило якорей, что у виджетов документа).
static void ed_rect(int id, float *x, float *y, float *w, float *h) {
    const UiWidget *p = widget_cget(&g_edWt, id);
    if (!p) {
        *x = *y = *w = *h = 0;
        return;
    }
    widget_rect_px(p, g_WW, g_WH, x, y, w, h);
}

static void ed_chrome_init(void) {
    ed_host_init(); // ХОСТ: реестр команд (одна таблица) + документы
    widget_tree_init(&g_edWt);
    memset(g_edMoved, 0, sizeof g_edMoved);
    UiWidgetStyle s, s2;
    memset(&s, 0, sizeof s);
    s.alpha = 1.0f;
    s.rounding = 6.0f;
    s.fontSize = 14.0f;
    s.bg[0] = 0.11f;
    s.bg[1] = 0.12f;
    s.bg[2] = 0.16f;
    s.bg[3] = 1.0f;
    s2 = s;
    s2.bg[0] = 0.09f;
    s2.bg[1] = 0.10f;
    s2.bg[2] = 0.13f;
    UiWidgetRect r;
    memset(&r, 0, sizeof r);
    r.anchors = UI_A_LEFT | UI_A_TOP;
    g_edPal = widget_add(&g_edWt, UI_W_PANEL, "EdPalette", 0, &r, &s);
    g_edTree = widget_add(&g_edWt, UI_W_PANEL, "EdHierarchy", 0, &r, &s2);
    g_edIns = widget_add(&g_edWt, UI_W_PANEL, "EdInspector", 0, &r, &s2);
    g_edCon = widget_add(&g_edWt, UI_W_PANEL, "EdConsole", 0, &r, &s2);
    g_edPanels[0] = g_edPal; // F5j: панели как окна — порядок/таск за заголовок
    g_edPanels[1] = g_edTree;
    g_edPanels[2] = g_edIns;
    g_edPanels[3] = g_edCon;
    for (int i = 0; i < ed_ws_panels(); i++) g_edOrder[i] = i;
    g_edGrab = -1;
}

static void pal_rect(int i, float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edPal, &px, &py, &pw, &ph);
    (void)ph;
    *x = px + PAL_PAD;
    *y = py + PAL_PAD + PAL_TITLE + (float)(i * (PAL_BH + PAL_GAP));
    *w = pw - 2 * PAL_PAD; // кнопки палитры — во всю ширину панели
    *h = (float)PAL_BH;
}

static int pal_hit(float px, float py) {
    if (!ed_live(0)) return -1; // закрытая панель не ловит клик (rect 0x0)
    for (int i = 0; i < ed_pal_n(); i++) {
        float x, y, w, h;
        pal_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}

// Прямоугольник спавна: виджет рождается НА ЭКРАНЕ ИГРЫ (вьюпорте), а не
// «правее палитры в пикселях окна». Тестер в тесном окне получал виджет ЗА
// границей экрана («улетел»), поэтому центр вьюпорта + каскад, с зажимом внутрь
// его рамки (Word/UE/Unity: новый элемент появляется там, где на него смотрят).
static UiWidgetRect wt_spawn_rect(int type) {
    UiWidgetRect r;
    memset(&r, 0, sizeof r);
    r.anchors = UI_A_LEFT | UI_A_TOP;
    if (type == UI_W_PANEL) {
        r.w = 240;
        r.h = 130;
    } else if (type == UI_W_BUTTON) {
        r.w = 120;
        r.h = 32;
    } else {
        r.w = 150;
        r.h = 22;
    }
    int k = g_wt.count % 5;
    float Wpx = (float)doc_w(), Hpx = (float)doc_h();
    // Новый виджет не больше экрана игры: в тесном окне панель 240px не влезала
    // в 192px экрана и «улетала» за границу (как макетный фрейм в Figma/UE —
    // рождается по размеру видимой области).
    if (r.w > Wpx - 16.0f) r.w = Wpx - 16.0f;
    if (r.h > Hpx - 16.0f) r.h = Hpx - 16.0f;
    if (r.w < 8.0f) r.w = 8.0f;
    if (r.h < 8.0f) r.h = 8.0f;
    r.x = (Wpx - r.w) * 0.5f + (float)(k * 24) - 48.0f;
    r.y = (Hpx - r.h) * 0.5f + (float)(k * 20) - 40.0f;
    float mx = Wpx - r.w - 8.0f, my = Hpx - r.h - 8.0f;
    if (mx < 8.0f) mx = 8.0f;
    if (my < 8.0f) my = 8.0f;
    if (r.x < 8.0f) r.x = 8.0f;
    if (r.y < 8.0f) r.y = 8.0f;
    if (r.x > mx) r.x = mx;
    if (r.y > my) r.y = my;
    return r;
}

static UiWidgetStyle wt_spawn_style(int type) {
    UiWidgetStyle s;
    memset(&s, 0, sizeof s);
    s.alpha = 1.0f;
    s.fontSize = 14.0f;
    if (type == UI_W_PANEL) {
        s.bg[0] = 0.13f;
        s.bg[1] = 0.15f;
        s.bg[2] = 0.19f;
        s.bg[3] = 1.0f;
        s.rounding = 6.0f;
    } else if (type == UI_W_BUTTON) {
        s.bg[0] = 0.22f;
        s.bg[1] = 0.34f;
        s.bg[2] = 0.62f;
        s.bg[3] = 1.0f;
        s.rounding = 4.0f;
    } else { // label: подпись без плашки
        s.bg[3] = 0.0f;
    }
    return s;
}

static int wt_spawn(int type) {
    int k = type == UI_W_PANEL ? 0 : type == UI_W_BUTTON ? 1 : 2;
    // ИМЯ — МЕТКА, ИДЕНТИЧНОСТЬ — id (§14 F1). Но метки НЕ должны повторяться:
    // тестер: «в дереве пять строк Panel1 — не понять, какую правлю, они же
    // разные». Счётчик стартует с нуля каждый запуск, поэтому берём ПЕРВЫЙ
    // свободный номер (как Figma «Frame 12» / UMG): занятые (в т.ч. из
    // сохранённого документа) пропускаем.
    const char *base = k == 0 ? "Panel" : k == 1 ? "Button" : "Label";
    char nm[UI_WIDGET_NAME];
    do {
        g_wtN[k]++;
        snprintf(nm, sizeof nm, "%s%d", base, g_wtN[k]);
    } while (widget_find(&g_wt, nm) != 0 && g_wtN[k] < 10000);
    UiWidgetRect r = wt_spawn_rect(type);
    UiWidgetStyle s = wt_spawn_style(type);
    int id = widget_add(&g_wt, type, nm, 0, &r, &s);
    if (id) g_wtSel = id;
    // Ш1c: журнал пишется ТУТ — единственной точке создания виджета (палитра,
    // лента, сид, скрипты). Иначе каждый путь пришлось бы логировать отдельно.
    if (id) ui_log_add("add %s \"%s\"", wt_type_name(type), nm);
    return id;
}

// §14 F3c: порядок слоёв, хит и отрисовка виджетов — из ОБЩЕГО рантайма
// (ui_widget_runtime). Здесь только тонкие обёртки над деревом редактора g_wt,
// чтобы игра и редактор гарантированно видели одинаковый порядок/верх.
static int wt_sib_sorted(int parent, int *out) {
    return ui_widget_sib_sorted(g_act, parent, out);
}

static int wt_hit(float px, float py) {
    if (doc_live()) { // документ: мышь -> координаты ЭКРАНА ИГРЫ, вне вьюпорта нет
        if (px < (float)g_vpx || px >= (float)(g_vpx + g_vpw) || py < (float)g_vpy ||
            py >= (float)(g_vpy + g_vph))
            return 0;
        return ui_widget_hit(g_act, doc_w(), doc_h(), px - doc_ox(), py - doc_oy());
    }
    return ui_widget_hit(g_act, g_WW, g_WH, px, py);
}

// --- §14 F5d: стопка под курсором (порядок ОТРИСОВКИ, низ -> верх) ---
// Нужна для «выбрать сквозь верхний» (Figma deep select / Blender alt-click):
// без неё нижний виджет в перекрытии мышью не достать вообще.
static void wt_order_rec(int parent, int *out, int *n, int cap) {
    int kids[UI_MAX_WIDGETS];
    int k = wt_sib_sorted(parent, kids);
    for (int i = 0; i < k && *n < cap; i++) {
        out[(*n)++] = kids[i];
        wt_order_rec(kids[i], out, n, cap);
    }
}
static int wt_stack_at(float px, float py, int *out, int cap) {
    int order[UI_MAX_WIDGETS];
    int n = 0;
    wt_order_rec(0, order, &n, UI_MAX_WIDGETS);
    if (doc_live()) { // F5l(3): стопка документа — в координатах экрана игры
        if (px < (float)g_vpx || px >= (float)(g_vpx + g_vpw) || py < (float)g_vpy ||
            py >= (float)(g_vpy + g_vph))
            return 0;
        px -= doc_ox();
        py -= doc_oy();
    }
    int m = 0;
    for (int i = 0; i < n && m < cap; i++) {
        const UiWidget *w = widget_cget(g_act, order[i]);
        if (!w) continue;
        float x, y, ww, hh;
        if (g_act == &g_wt) widget_rect_px(w, doc_w(), doc_h(), &x, &y, &ww, &hh);
        else widget_rect_px(w, g_WW, g_WH, &x, &y, &ww, &hh);
        if (px >= x && px < x + ww && py >= y && py < y + hh) out[m++] = order[i];
    }
    return m;
}
// Ctrl+клик: следующий НИЖЕ текущего в стопке (cur = 0 -> на один ниже верхнего).
// Дошли до низа — снова верхний (цикл), как в Figma/Blender.
static int wt_pick_deep(float px, float py, int cur) {
    int stk[UI_MAX_WIDGETS];
    int m = wt_stack_at(px, py, stk, UI_MAX_WIDGETS);
    if (m <= 0) return 0;
    int i = m - 1; // без текущего в стопке — верхний (первый Ctrl+клик)
    for (int k = m - 1; k >= 0; k--)
        if (stk[k] == cur) {
            i = k - 1;
            break;
        }
    if (i < 0) i = m - 1; // ниже некуда -> верхний (цикл)
    return stk[i];
}

static void wt_emit(RcVecShape *sh, int *n, float x, float y, float w, float h,
                    const float rgba[4], float round) {
    if (*n >= UI_MAX_PRIMS) return;
    RcVecShape *s = &sh[(*n)++];
    s->kind = 0;
    s->a = x;
    s->b = y;
    s->c = w;
    s->d = h;
    s->e = round;
    s->f = 0;
    memcpy(s->rgba, rgba, sizeof s->rgba);
}

// §14 F5l(3): ДОКУМЕНТ ЖИВЁТ ВО ВЬЮПОРТЕ (экран игры). Координаты виджетов
// отсчитываются от его левого верхнего угла, а якоря L/R/T/B — от РАЗМЕРА ЭКРАНА
// ИГРЫ (как в самой игре: предпросмотр = то, что увидит игрок). За границей
// вьюпорта документа НЕТ (клип) — там хром редактора.
static int doc_w(void) { return g_vpw > 0 ? g_vpw : g_WW; }
static int doc_h(void) { return g_vph > 0 ? g_vph : g_WH; }
static float doc_ox(void) { return g_vpw > 0 ? (float)g_vpx : 0.0f; }
static float doc_oy(void) { return g_vpw > 0 ? (float)g_vpy : 0.0f; }
// Активен ли документ (в self-режиме правим хром: он в пикселях окна).
static int doc_live(void) { return g_act == &g_wt && g_vpw > 0; }
// Пиксельный rect виджета в координатах ЭКРАНА: хит, грязь, рамка выбора.
static void doc_px(const UiWidget *w, float *x, float *y, float *ww, float *hh) {
    widget_rect_px(w, doc_w(), doc_h(), x, y, ww, hh);
    *x += doc_ox();
    *y += doc_oy();
}
// Обрезать фигуру редактора вьюпортом: документа за его границей не бывает
// (иначе «виджет улетел» — тестер видел фигуру за экраном игры).
static void ed_emit_clip(RcVecShape *sh, int *n, float x, float y, float w, float h,
                         const float rgba[4], float round) {
    if (doc_live()) {
        float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
        float cx0 = (float)g_vpx, cy0 = (float)g_vpy;
        float cx1 = cx0 + (float)g_vpw, cy1 = cy0 + (float)g_vph;
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        if (x1 <= x0 || y1 <= y0) return;
        wt_emit(sh, n, x0, y0, x1 - x0, y1 - y0, rgba, round);
        return;
    }
    wt_emit(sh, n, x, y, w, h, rgba, round);
}

// §14 F3c: заливки виджетов — из ОБЩЕГО рантайма (то же, что увидит игра).
static void wt_draw_all(RcVecShape *sh, int *n) {
    RcVecShape tmp[UI_MAX_PRIMS];
    int m = ui_widget_build(&g_wt, doc_w(), doc_h(), tmp, UI_MAX_PRIMS);
    for (int i = 0; i < m && *n < UI_MAX_PRIMS; i++) {
        RcVecShape s = tmp[i];
        s.a += doc_ox();
        s.b += doc_oy();
        if (doc_live() && s.kind == 0) { // F5l(3): за вьюпортом документа нет
            float x0 = s.a, y0 = s.b, x1 = s.a + s.c, y1 = s.b + s.d;
            float cx0 = (float)g_vpx, cy0 = (float)g_vpy;
            float cx1 = cx0 + (float)g_vpw, cy1 = cy0 + (float)g_vph;
            if (x0 < cx0) x0 = cx0;
            if (y0 < cy0) y0 = cy0;
            if (x1 > cx1) x1 = cx1;
            if (y1 > cy1) y1 = cy1;
            s.a = x0;
            s.b = y0;
            s.c = x1 - x0;
            s.d = y1 - y0;
            if (s.c <= 0.0f || s.d <= 0.0f) continue;
        }
        sh[(*n)++] = s;
    }
}

// Хром редактора — тем же рантаймом (и это же дерево правится в self-режиме).
static void chrome_draw(RcVecShape *sh, int *n) {
    RcVecShape tmp[UI_MAX_PRIMS];
    int m = ui_widget_build(&g_edWt, g_WW, g_WH, tmp, UI_MAX_PRIMS);
    for (int i = 0; i < m && *n < UI_MAX_PRIMS; i++) sh[(*n)++] = tmp[i];
}

// §14 F5c: полоса ЗАГОЛОВКА панели (как в Unity/Figma). Без неё заголовок
// «висит» на фоне панели и кажется съехавшим (жалоба тестера). Рисуем после
// фона панели, но ДО рядов и до текста (текст — отдельный проход).
// §14 F5j: порядок = g_edOrder (верхняя панель — последняя), у ОТОРВАННОЙ панели
// дополнительно рамка «я поверх всего» и кнопка «dock» в правом конце заголовка.
// §14 F5l(4b): линия сплиттера на краю дока. Без неё «за что тянуть» не видно
// (жалоба тестера на невидимые ручки — та же грабля, что с 8 ручками ресайза).
static void split_draw(RcVecShape *sh, int *n) {
    static const float col[4] = {0.28f, 0.31f, 0.38f, 1.0f};
    static const float hot[4] = {0.35f, 0.62f, 1.0f, 1.0f}; // под курсором/тащат
    int which = 0;
    int over = split_hit((float)g_mx, (float)g_my, &which);
    for (int i = 1; i <= 3; i++) {
        if (i == 1 && g_dockL <= 0) continue;
        if (i == 2 && g_dockR <= 0) continue;
        if (i == 3 && g_dockB <= 0) continue;
        float x, y, w, h;
        split_rect(i, &x, &y, &w, &h);
        int on = (g_split == i) || (over && which == i);
        if (i == 3) // нижний док: горизонтальная линия по верху дока
            wt_emit(sh, n, x, y + h * 0.5f - 1.0f, w, 2.0f, on ? hot : col, 0.0f);
        else
            wt_emit(sh, n, x + w * 0.5f - 1.0f, y, 2.0f, h, on ? hot : col, 0.0f);
    }
}

static void chrome_heads_draw(RcVecShape *sh, int *n) {
    float head[4] = {0.19f, 0.21f, 0.27f, 1.0f};
    float line[4] = {0.30f, 0.33f, 0.41f, 1.0f};
    float edge[4] = {0.38f, 0.44f, 0.58f, 1.0f}; // оторвана: рамка окна
    float chip[4] = {0.26f, 0.31f, 0.42f, 1.0f}; // кнопка «dock»
    for (int k = 0; k < ed_ws_panels(); k++) {
        int i = g_edOrder[k];
        float px, py, pw, ph;
        ed_rect(g_edPanels[i], &px, &py, &pw, &ph);
        if (pw <= 2 || ph <= (float)PAL_TITLE) continue;
        wt_emit(sh, n, px + 1, py + 1, pw - 2, (float)PAL_TITLE - 1, head, 5.0f);
        wt_emit(sh, n, px + 1, py + (float)PAL_TITLE - 1, pw - 2, 1.0f, line, 0.0f);
        if (ed_panel_float(i)) { // панель оторвана от дока — она «окно»
            wt_emit(sh, n, px, py, 2.0f, ph, edge, 0.0f);
            wt_emit(sh, n, px + pw - 2, py, 2.0f, ph, edge, 0.0f);
            wt_emit(sh, n, px, py + ph - 2, pw, 2.0f, edge, 0.0f);
            float cx, cy, cw, ch;
            ed_float_chip(i, &cx, &cy, &cw, &ch);
            wt_emit(sh, n, cx, cy, cw, ch, chip, 3.0f);
        }
    }
}


// §14 Ш1c: КОНСОЛЬ — ВИД кольца ui_log (данные — в ui/ui_log.c, ОДНА реализация).
// Панель лишь читает: строки идут сверху вниз, хвост журнала внизу (как в
// терминале VS Code), колесо прокручивает вверх, кнопка Clear в шапке чистит.
// Текст строк идёт через ed_text_vis(3,..): под другой панелью он молчит.
#define CON_RH 16      // высота строки журнала
#define CON_CLEAR_W 54 // кнопка Clear в шапке консоли
#define CON_CLEAR_H 16
static int g_conScroll = 0; // на сколько строк вверх прокручен журнал (0 = хвост)

// Сколько строк влезает в панель — ОДНА точка правды для рисования и для клампа
// прокрутки (иначе они разъедутся, и «хвост» станет врать).
static int con_rows(void) {
    const UiWidget *p = widget_cget(&g_edWt, g_edCon);
    if (!p || p->rect.h <= (float)PAL_TITLE) return 0;
    float area = p->rect.h - (float)PAL_TITLE - (float)PAL_PAD;
    int r = (int)(area / (float)CON_RH);
    return r > 0 ? r : 0;
}
// Видимый срез: start..start+rows. Прокрутку клампим ЗДЕСЬ (и рисование, и
// колесо зовут эту функцию) — «прокрутил в пустоту» не бывает. start/rows — можно
// передать 0 (нужен только кламп).
static void con_view(int *start, int *rows) {
    int ln = ui_log_n();
    int r = con_rows();
    int maxs = ln - r;
    if (maxs < 0) maxs = 0;
    if (g_conScroll < 0) g_conScroll = 0;
    if (g_conScroll > maxs) g_conScroll = maxs;
    int s = ln - r - g_conScroll;
    if (s < 0) s = 0;
    if (start) *start = s;
    if (rows) *rows = r;
}
// Верхний левый угол строки r панели консоли (строки от шапки вниз).
static void con_row_rect(int r, float *x, float *y) {
    float px, py, pw, ph;
    ed_rect(g_edCon, &px, &py, &pw, &ph);
    (void)pw;
    (void)ph;
    *x = px + (float)PAL_PAD;
    *y = py + (float)PAL_TITLE + 2.0f + (float)r * (float)CON_RH;
}
// Кнопка Clear — в правом конце шапки консоли. ЕЁ ЕДИНСТВЕННОЕ место: команда
// console.clear имеет вид ED_V_DOCK, поэтому в ленте/палитре не рисуется.
static void con_clear_chip(float *cx, float *cy, float *cw, float *ch) {
    float x, y, w, h;
    ed_rect(g_edCon, &x, &y, &w, &h);
    (void)h;
    *cw = (float)CON_CLEAR_W;
    *ch = (float)CON_CLEAR_H;
    *cx = x + w - (float)PAL_PAD - *cw;
    *cy = y + 4.0f;
}
static int con_clear_hit(float px, float py) {
    if (!ed_live(3)) return 0;
    float cx, cy, cw, ch;
    con_clear_chip(&cx, &cy, &cw, &ch);
    return px >= cx && px < cx + cw && py >= cy && py < cy + ch;
}
static void con_draw(RcVecShape *sh, int *n) {
    if (!ed_live(3)) return;
    float cx, cy, cw, ch;
    con_clear_chip(&cx, &cy, &cw, &ch);
    static const float chip[4] = {0.20f, 0.23f, 0.30f, 1.0f};
    wt_emit(sh, n, cx, cy, cw, ch, chip, 3.0f);
}

// Рамка выбора + ручки — это ХРОМ РЕДАКТОРА, потому локально и ПОВЕРХ всего
// (в self-режиме обводка ложится на панель хрома, а не тонет под ней).
static void wt_draw_sel(RcVecShape *sh, int *n) {
    const UiWidget *s = widget_cget(g_act, g_wtSel);
    if (s) {
        float x, y, ww, hh;
        if (g_act == &g_wt) doc_px(s, &x, &y, &ww, &hh);
        else widget_rect_px(s, g_WW, g_WH, &x, &y, &ww, &hh);
        static const float sel[4] = {0.25f, 0.55f, 1.0f, 1.0f};
        ed_emit_clip(sh, n, x - 1, y - 1, ww + 2, 2, sel, 0);
        ed_emit_clip(sh, n, x - 1, y + hh - 1, ww + 2, 2, sel, 0);
        ed_emit_clip(sh, n, x - 1, y, 2, hh, sel, 0);
        ed_emit_clip(sh, n, x + ww - 1, y, 2, hh, sel, 0);
        // 8 ручек ресайза (визуал «за что тянуть» — юзер просил).
        float hxs[3] = {x, x + ww * 0.5f, x + ww};
        float hys[3] = {y, y + hh * 0.5f, y + hh};
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                if (i == 1 && j == 1) continue; // центр — не ручка
                ed_emit_clip(sh, n, hxs[i] - WT_HANDLE / 2, hys[j] - WT_HANDLE / 2,
                             WT_HANDLE, WT_HANDLE, sel, 0);
            }
    }
}

static void pal_draw(RcVecShape *sh, int *n) {
    if (!ed_live(0)) return; // F5l(5): закрытая палитра — ни одной фигуры
    for (int i = 0; i < ed_pal_n(); i++) {
        float x, y, w, h;
        pal_rect(i, &x, &y, &w, &h);
        float c[4] = {0.20f, 0.22f, 0.27f, 1.0f};
        if (ed_pal_is_action(i)) { // строка-ДЕЙСТВИЕ (Delete) — краснее
            c[0] = 0.34f;
            c[1] = 0.16f;
            c[2] = 0.19f;
        }
        if (i == g_palHot) {
            c[0] = 0.30f;
            c[1] = 0.45f;
            c[2] = 0.75f;
        }
        wt_emit(sh, n, x, y, w, h, c, 4.0f);
    }
}

// --- F3: дерево виджетов (иерархия + выбор кликом) ---
// Строки идут в том же порядке, что и отрисовка (корни -> дети, в глубину).
#define TREE_RH 20
// F5d: строка «хлебных крошек» (путь до выбранного) под заголовком панели.
#define TREE_HDR 22
#define TREE_PATH 18
static void tree_path_rect(float *x, float *y, float *w, float *h); // F5d (ниже)
static float tree_seg_w(const char *s); // F5h: ширина призрака (ниже)
static int g_treeN = 0;
static int g_treeId[UI_MAX_WIDGETS];
static int g_treeDepth[UI_MAX_WIDGETS];
static int g_treeScroll = 0; // первая видимая строка: панель — окно со скроллом
// --- §14 F5h: drag&drop строк дерева (reparent/порядок) ---
// Тестер: «не могу добавить/вытащить ветку из дерева — дерево бесполезно».
static int g_mouseDown = 0;   // ЛКМ нажата (MOVE приходит без состояния кнопки)
static int g_treePress = 0;   // строка под ЛКМ: ждём смещения, чтобы начать драг
static float g_treePressY = 0;
static int g_treeDrag = 0;    // id перетаскиваемой ветки
static int g_treeDropId = -1; // строка-цель броска (-1 = мимо)
static int g_treeDropMode = 0; // 0 = ВНУТРЬ, 1 = ПЕРЕД строкой, 2 = ПОСЛЕ
static float g_treeDragX = 0, g_treeDragY = 0; // курсор (призрак подписи)

// Сколько строк влезает в панель дерева (высота — из хром-виджета).
static int tree_rows_fit(void) {
    float px, py, pw, ph;
    ed_rect(g_edTree, &px, &py, &pw, &ph);
    (void)px;
    (void)py;
    (void)pw;
    int rows = (int)((ph - TREE_HDR - TREE_PATH - PAL_PAD) / TREE_RH);
    return rows < 1 ? 1 : rows;
}

static void tree_collect(int id, int depth) {
    if (g_treeN >= UI_MAX_WIDGETS) return;
    const UiWidget *w = widget_cget(g_act, id);
    if (!w) return;
    g_treeId[g_treeN] = id;
    g_treeDepth[g_treeN] = depth;
    g_treeN++;
    int kids[UI_MAX_WIDGETS];
    int nk = wt_sib_sorted(id, kids); // порядок = слои (как рисуем)
    for (int i = 0; i < nk; i++) tree_collect(kids[i], depth + 1);
}
static void tree_build(void) {
    g_treeN = 0;
    int roots[UI_MAX_WIDGETS];
    int nr = wt_sib_sorted(0, roots);
    for (int i = 0; i < nr; i++) tree_collect(roots[i], 0);
    // Прокрутка: выбранного держим в кадре, за края списка не вылезаем.
    int rows = tree_rows_fit();
    for (int i = 0; i < g_treeN; i++)
        if (g_treeId[i] == g_wtSel) {
            if (i < g_treeScroll) g_treeScroll = i;
            if (i >= g_treeScroll + rows) g_treeScroll = i - rows + 1;
        }
    int maxs = g_treeN - rows;
    if (maxs < 0) maxs = 0;
    if (g_treeScroll > maxs) g_treeScroll = maxs;
    if (g_treeScroll < 0) g_treeScroll = 0;
}
// row — индекс ВИДИМОЙ строки (0..rows-1): ширина = внутренняя ширина панели
// минус место под индикатор прокрутки, так строки не вылезают за хром-виджет.
static void tree_rect(int row, float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edTree, &px, &py, &pw, &ph);
    (void)ph;
    *x = px + PAL_PAD;
    *y = py + TREE_HDR + TREE_PATH + (float)(row * TREE_RH);
    *w = pw - 2 * PAL_PAD - 4;
    *h = (float)TREE_RH;
}
static int tree_hit(float px, float py) {
    if (!ed_live(1)) return 0; // закрытое дерево не ловит клик
    int rows = tree_rows_fit();
    for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
        float x, y, w, h;
        tree_rect(i - g_treeScroll, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return g_treeId[i];
    }
    return 0;
}
static void type_col(int type, float out[4]) {
    if (type == UI_W_BUTTON) { out[0] = 0.35f; out[1] = 0.65f; out[2] = 0.90f; }
    else if (type == UI_W_LABEL) { out[0] = 0.80f; out[1] = 0.75f; out[2] = 0.40f; }
    else { out[0] = 0.55f; out[1] = 0.60f; out[2] = 0.70f; }
    out[3] = 1.0f;
}
static void tree_draw(RcVecShape *sh, int *n) {
    if (!ed_live(1)) return; // F5l(5): закрытое дерево — ни одной фигуры
    tree_build();
    int rows = tree_rows_fit();
    { // F5d: полоса крошек (под заголовком): «где я» отдельно от списка
        float x, y, w, h;
        tree_path_rect(&x, &y, &w, &h);
        float c[4] = {0.13f, 0.145f, 0.18f, 1.0f};
        wt_emit(sh, n, x - 2, y, w + 4, h, c, 0.0f);
    }
    for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
        float x, y, w, h;
        tree_rect(i - g_treeScroll, &x, &y, &w, &h);
        const UiWidget *wd = widget_cget(g_act, g_treeId[i]);
        if (!wd) continue;
        int sel = g_treeId[i] == g_wtSel;
        float c[4] = {0.145f, 0.155f, 0.195f, 1.0f};
        if ((i - g_treeScroll) & 1) { // ЗЕБРА: ветви/строки не сливаются в кашу
            c[0] = 0.175f;
            c[1] = 0.19f;
            c[2] = 0.235f;
        }
        if (sel) { c[0] = 0.22f; c[1] = 0.34f; c[2] = 0.62f; }
        wt_emit(sh, n, x, y, w, h, c, 3.0f);
        // НАПРАВЛЯЮЩИЕ отступов: видно, какая строка чей ребёнок (Unity/VS Code).
        for (int d = 0; d < g_treeDepth[i]; d++) {
            float gc[4] = {0.34f, 0.37f, 0.45f, 0.65f};
            wt_emit(sh, n, x + 8 + (float)(d * 10), y, 1.0f, h, gc, 0.0f);
        }
        float tc[4];
        type_col(wd->type, tc);
        wt_emit(sh, n, x + 4 + (float)(g_treeDepth[i] * 10), y + 4, 4, h - 8, tc, 0);
    }
    // F5h: индикатор БРОСКА — рамка («внутрь») или линия вставки (перед/после).
    if (g_treeDrag && g_treeDropId >= 0) {
        float cc[4] = {0.95f, 0.75f, 0.25f, 1.0f};
        if (g_treeDropId == 0) { // крошка Scene = бросить в корень
            float x, y, w, h;
            tree_path_rect(&x, &y, &w, &h);
            wt_emit(sh, n, x - 2, y, w + 4, 2.0f, cc, 0.0f);
            wt_emit(sh, n, x - 2, y + h - 2, w + 4, 2.0f, cc, 0.0f);
        } else {
            int row = -1;
            for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++)
                if (g_treeId[i] == g_treeDropId) {
                    row = i;
                    break;
                }
            if (row >= 0) {
                float x, y, w, h;
                tree_rect(row - g_treeScroll, &x, &y, &w, &h);
                if (g_treeDropMode == 0) {
                    wt_emit(sh, n, x, y, w, 2.0f, cc, 0.0f);
                    wt_emit(sh, n, x, y + h - 2, w, 2.0f, cc, 0.0f);
                    wt_emit(sh, n, x, y, 2.0f, h, cc, 0.0f);
                    wt_emit(sh, n, x + w - 2, y, 2.0f, h, cc, 0.0f);
                } else {
                    float ly = g_treeDropMode == 1 ? y : y + h - 2;
                    wt_emit(sh, n, x, ly, w, 2.0f, cc, 0.0f);
                }
            }
        }
    }
    // F5h: ПРИЗРАК подписи под курсором (что именно тащим).
    if (g_treeDrag) {
        const UiWidget *dw = widget_cget(g_act, g_treeDrag);
        if (dw) {
            float gw = tree_seg_w(dw->name) + 12.0f;
            float bg[4] = {0.17f, 0.21f, 0.36f, 0.95f};
            wt_emit(sh, n, g_treeDragX + 10, g_treeDragY + 2, gw, 20.0f, bg, 3.0f);
        }
    }
    // Индикатор прокрутки (UE/Blender): сразу видно, что список — окно и где мы
    // в нём. Без него «не листается» неотличимо от «листать нечего».
    if (g_treeN > rows) {
        float px, py, pw, ph;
        ed_rect(g_edTree, &px, &py, &pw, &ph);
        float track[4] = {0.16f, 0.18f, 0.22f, 0.85f};
        float thumb[4] = {0.45f, 0.52f, 0.62f, 1.0f};
        float barW = 3.0f;
        float bx = px + pw - PAL_PAD + 1.0f;
        float ty = py + TREE_HDR + TREE_PATH;
        float thh = ph - TREE_HDR - TREE_PATH - PAL_PAD;
        int maxs = g_treeN - rows;
        float frac = maxs > 0 ? (float)g_treeScroll / (float)maxs : 0.0f;
        float bh = thh * (float)rows / (float)g_treeN;
        if (bh < 14.0f) bh = 14.0f;
        float by = ty + (thh - bh) * (frac < 0 ? 0 : frac > 1 ? 1 : frac);
        wt_emit(sh, n, bx, ty, barW, thh, track, 1.5f);
        wt_emit(sh, n, bx, by, barW, bh, thumb, 1.5f);
    }
}

// --- F3b: инспектор (правка выбранного) ---
// Справа: строки полей. Числа тянутся мышью влево/вправо (scrub, как в
// Blender/UE); Lay +/- меняет слой среди соседей. Пишем на коммит (UP).
#define INS_W 200
#define INS_RH 20
#define INS_GAP 2
#define INS_HEAD 20 // строка заголовка (имя выбранного)
enum {
    IF_X, IF_Y, IF_W, IF_H, IF_ROUND, IF_ALPHA, IF_FONT,
    IF_BG_R, IF_BG_G, IF_BG_B, IF_UP, IF_DOWN, IF_N
};
static const char *INS_LABEL[IF_N] = {"x",   "y",     "w",     "h",     "round",
                                      "alpha", "font", "bg.r", "bg.g", "bg.b",
                                      "Lay +", "Lay -"};
static int g_insField = -1;      // какой ряд тянем (scrub), -1 = нет
static float g_insDownX = 0;     // мышь на DOWN
static float g_insDownVal = 0;   // значение на DOWN
static int g_insPrevPx[4];       // прошлый пиксельный rect (dirty-rect)

// --- §14 F5a: состояние правки ТЕКСТА (имя выбранного виджета) ---
// Объявлено здесь (не рядом с функциями), потому что ins_draw выше по файлу:
// каретка рисуется в том же проходе, что и строки инспектора.
static UiText *g_text = 0; // атлас: ширина каретки
static int g_nameEdit = 0; // 1 = идёт правка имени выбранного
static char g_nameBuf[UI_WIDGET_NAME];
static int g_nameLen = 0, g_nameCaret = 0;
static int g_nameAnchor = 0; // второе плечо выделения (anchor..caret)
static int g_caretOn = 1;      // фаза мигания каретки
static double g_nameBlink = 0; // 0 = фазу перезапустить (ставит цикл кадра)
static int name_has_sel(void); // выделение (определения ниже: ins_draw выше них)
static int name_sel_lo(void);
static int name_sel_hi(void);

static void ins_rect(int i, float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edIns, &px, &py, &pw, &ph);
    (void)pw;
    (void)ph;
    // Строка ЗАГОЛОВКА панели (PAL_TITLE) — отдельная: иначе «INSPECTOR» лезло на
    // имя выбранного (тестер: «все стоит криво»). Контент идёт ниже заголовка.
    *x = px + PAL_PAD;
    *y = py + PAL_PAD + PAL_TITLE +
         (i < 0 ? 0.0f : INS_HEAD + (float)(i * (INS_RH + INS_GAP)));
    *w = (float)INS_W;
    *h = (i < 0 ? (float)(INS_HEAD - 4) : (float)INS_RH);
}

// Раскладка хрома по умолчанию: панели привязаны к вьюпорту (L|T-якоря и оффсеты
// окна) — двинулся док, двинулся хром. Правку юзера (g_edMoved) не перетираем.
// §14 F5j: «домашнее» место панели в доке (панели соседствуют, не пересекаются).
static void ed_panel_home(int i, float *ox, float *oy, float *ow, float *oh) {
    // §14 F5l(4): панели стоят в СВОИХ полосах дока, а не поверх экрана игры:
    // левый док — от левого края ОКНА, правый — от правого, вьюпорт = середина.
    float palW = (float)g_dockWl;
    float palH =
        (float)(PAL_PAD * 2 + PAL_TITLE + ed_pal_n() * (PAL_BH + PAL_GAP) - PAL_GAP);
    float x0 = (float)PAL_PAD;
    float y0 = (float)ed_top(); // под полосой лент (F5l(2): одна точка правды)
    // Ш1c: нижний док (консоль) съедает низ окна. Боковые панели НЕ имеют права
    // залезть под него — иначе «вьюпорт = остаток» врёт, а консоль накрыта деревом.
    float roomH = (float)(g_WH - g_dockB - PAL_PAD) - y0;
    if (palH > roomH) palH = roomH;
    if (i == 0) { // палитра: верх левого дока
        *ox = x0;
        *oy = y0;
        *ow = palW;
        *oh = palH;
        return;
    }
    if (i == 1) { // дерево: под палитрой (или на её месте, если палитра закрыта)
        float ty = g_edOpen[0] ? y0 + palH + 10 : y0;
        *ox = x0;
        *oy = ty;
        *ow = palW;
        {
            float h = (float)(PAL_PAD * 2 + TREE_HDR + TREE_PATH + TREE_RH * 10);
            float avail = (float)(g_WH - g_dockB) - ty - PAL_PAD;
            float one = (float)(PAL_PAD * 2 + TREE_HDR + TREE_PATH + TREE_RH);
            if (h > avail) h = avail;
            if (h < one) h = one;
            *oh = h;
        }
        return;
    }
    if (ed_ws_dock(i) == ED_DK_BOTTOM) { // Ш1c: КОНСОЛЬ — нижний док окна (VS Code)
        *ox = (float)PAL_PAD;
        *oy = (float)(g_WH - g_dockH - PAL_PAD);
        *ow = (float)(g_WW - 2 * PAL_PAD);
        *oh = (float)g_dockH;
        return;
    }
    *ow = (float)(g_dockWr + 2 * PAL_PAD); // инспектор: правый док (ширина тянется)
    *ox = (float)(g_WW - g_dockWr - PAL_PAD);
    *oy = y0;
    *oh = (float)(PAL_PAD * 2 + PAL_TITLE + INS_HEAD + IF_N * (INS_RH + INS_GAP));
}
// Раскладка хрома: панели, которых юзер НЕ двигал, ставим на их док-места.
// Оторванную (g_edMoved) не трогаем — она живёт там, куда её положил юзер.
static void ed_chrome_sync(void) {
    ed_viewport_compute(); // F5l(4): вьюпорт = окно минус полосы доков
    for (int i = 0; i < ed_ws_panels(); i++) {
        UiWidget *p = widget_get(&g_edWt, g_edPanels[i]);
        if (!p) continue;
        if (!g_edOpen[i]) { // F5k: инструмент закрыт -> виджет нулевого размера:
            // рантайм его не рисует, хит/ряды (ширины считаются от панели) — тоже.
            p->rect.anchors = UI_A_LEFT | UI_A_TOP;
            p->rect.x = p->rect.y = 0;
            p->rect.w = p->rect.h = 0;
            continue;
        }
        if (g_edMoved[g_edPanels[i] - 1]) continue; // F5j: оторванную не трогаем
        float x, y, w, h;
        ed_panel_home(i, &x, &y, &w, &h);
        p->rect.anchors = UI_A_LEFT | UI_A_TOP;
        p->rect.x = x;
        p->rect.y = y;
        p->rect.w = w;
        p->rect.h = h;
    }
}
// §14 F5k: панель не выезжает за окно ни одной стороной (тестер: «вверх невидимая
// полоса, а влево/вправо/вниз уползает за край»). Тянем при таске и при ресайзе.
static void ed_panels_clamp(void) {
    for (int i = 0; i < ed_ws_panels(); i++) {
        if (!g_edOpen[i]) continue;
        UiWidget *p = widget_get(&g_edWt, g_edPanels[i]);
        if (!p) continue;
        float mx = (float)g_WW - p->rect.w, my = (float)g_WH - p->rect.h;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (p->rect.x > mx) p->rect.x = mx;
        if (p->rect.x < 0) p->rect.x = 0;
        if (p->rect.y > my) p->rect.y = my;
        if (p->rect.y < 0) p->rect.y = 0;
    }
}
// §14 F5k: хранилище состояния инструментов. layout.json делят доки (§11) и
// документ (§14) — хром редактора туда не лезет, у него свой маленький файл.
// Герметичность: в тест/сид/бенч-режимах путь уводится в …bench.json.
#define ED_PANELS_PATH "engine/assets/ed_panels.json"
#define ED_PANELS_BENCH "engine/assets/ed_panels.bench.json"
static const char *ed_panels_path(void) {
    if (getenv("UI_EDIT_BENCH") || getenv("UI_EDIT_SEED")) return ED_PANELS_BENCH;
    return ED_PANELS_PATH;
}
static void ed_panels_save(void) {
    if (g_selftest && !getenv("UI_EDIT_BENCH")) return; // тест файл юзера не пачкает
    const char *path = ed_panels_path();
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f,
            "{\n  \"_note\": \"ui_edit tools (#14 F5k): 1 = panel open, "
            "edTab = active ribbon, edTool = scene tool mode, dockL/dockR/dockH = "
            "dock pane sizes. Editor state, not a document.\",\n  \"edOpen\": [");
    for (int i = 0; i < ed_ws_panels(); i++)
        fprintf(f, "%s%d", i ? ", " : "", g_edOpen[i]);
    fprintf(f, "],\n  \"edTab\": %d,\n  \"edTool\": %d,\n"
               "  \"dockL\": %d,\n  \"dockR\": %d,\n  \"dockH\": %d\n}\n",
            g_edTab, g_panels ? ui_panels_tool(g_panels) : 0, g_dockWl, g_dockWr, g_dockH);
    fclose(f);
}
static void ed_panels_load(void) {
    const char *path = ed_panels_path();
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char s[512];
    size_t n = fread(s, 1, sizeof s - 1, f);
    fclose(f);
    s[n] = 0;
    { // режим инструмента сцены (лента MODEL) — состояние движка, не документа
        const char *tt = strstr(s, "\"edTool\"");
        if (tt) {
            int tv = -1;
            tt = strchr(tt, ':');
            if (tt && sscanf(tt + 1, "%d", &tv) == 1 && tv >= 0 && tv <= 3)
                ui_panels_set_tool(g_panels, tv);
        }
    }
    const char *q = strstr(s, "\"edOpen\"");
    if (!q) return;
    q = strchr(q, '[');
    if (!q) return;
    int v[ED_PANEL_MAX], got = 0;
    for (int i = 0; i < ED_PANEL_MAX; i++) v[i] = -1;
    {
        const char *r = q + 1; // сразу за '['
        while (got < ED_PANEL_MAX && sscanf(r, "%d", &v[got]) == 1) {
            got++;
            r = strchr(r, ',');
            if (!r) break;
            r++;
        }
    }
    if (got >= 3) { // старый файл (три значения) читается как есть: консоль закрыта
        for (int i = 0; i < ed_ws_panels(); i++) g_edOpen[i] = (i < got && v[i]) ? 1 : 0;
    }
    { // F5l(4b): ширина доков (кламп при чтении: битый файл панель не сносит)
        const char *dl = strstr(s, "\"dockL\"");
        const char *dr = strstr(s, "\"dockR\"");
        if (dl) {
            int v = -1;
            dl = strchr(dl, ':');
            if (dl && sscanf(dl + 1, "%d", &v) == 1 && v > 0) g_dockWl = v;
        }
        if (dr) {
            int v = -1;
            dr = strchr(dr, ':');
            if (dr && sscanf(dr + 1, "%d", &v) == 1 && v > 0) g_dockWr = v;
        }
        { // высота нижнего дока: кламп при чтении (битый файл экран не съест)
            const char *db = strstr(s, "\"dockH\"");
            if (db) {
                int v = -1;
                db = strchr(db, ':');
                if (db && sscanf(db + 1, "%d", &v) == 1 && v > 0) g_dockH = v;
            }
            ed_dock_clamp(3, &g_dockH);
        }
        ed_docks_clamp_pair();
        ed_viewport_compute();
    }
    { // F5l(2): активная лента — тоже состояние инструмента (переживает запуск)
        const char *t = strstr(s, "\"edTab\"");
        if (t) {
            int tv = -1;
            t = strchr(t, ':');
            if (t && sscanf(t + 1, "%d", &tv) == 1 && tv >= 0 && tv < ed_ws_tabs()) g_edTab = tv;
        }
    }
}
// §14 F5k: вкладка = тумблер инструмента. Открываем В ДОК (как Word/Roblox:
// панель возвращается на своё место), закрываем — место перетекает соседям.
static void tabs_toggle(int i) {
    if (i < 0 || i >= ed_ws_panels()) return;
    g_edOpen[i] = !g_edOpen[i];
    if (g_edOpen[i]) {
        int id = g_edPanels[i];
        if (id > 0 && id <= UI_MAX_WIDGETS) g_edMoved[id - 1] = 0;
    } else if (g_edGrab == i) {
        g_edGrab = -1;
    }
    ed_chrome_sync();
    ed_panels_save();
    edit_push();
}
// ============================================================================
// §14 F5l(2): РЕЕСТР ДЕЙСТВИЙ + ЛЕНТА + ШПАРГАЛКА.
// ОДНА таблица на все виды одного действия: кнопка ленты, клавиша, тултип,
// строка шпаргалки. Как TCommands/FUICommandInfo во UE 4.22 (объявил действие
// один раз — оно само попадает в меню/тулбар и в список горячих клавиш) и
// FInputBindingManager. Гейт when = canExecute: выключенное действие рисуется
// приглушённо и НЕ срабатывает — так честно помечено то, что ещё НЕ сделано
// (никаких «работающих» кнопок-пустышек). key2 — второй аккорд той же команды
// (Ctrl+Shift+Z для Redo), поэтому клавиша и команда не разъезжаются.
// ============================================================================
// ED_WHEN_*/ED_K_*/ED_V_*/ED_DM_*/ED_MOD_* и сама запись EdCmd — в командном
// ядре (editor/ed_cmd.h): реестр принадлежит РЕДАКТОРУ, а не этому режиму.
enum {
    ED_CMD_RENAME = 1,
    ED_CMD_DELETE = 2,
    ED_CMD_UNDO = 3,
    ED_CMD_REDO = 4,
    ED_CMD_KEYS = 5,
    ED_CMD_LOGCLR = 6 // Ш1c: очистить журнал консоли
};
// arg тумблера: 0..ed_ws_panels()-1 = панель, ED_TOG_SELF = «правим хром редактора».
// Раньше тут было 3 — и это столкнулось бы с панелью консоли (индекс 3).
#define ED_TOG_SELF 99
// Порядок строк = порядок кнопок на ленте (смена группы = новая подложка) И
// порядок строк ПАЛИТРЫ (у кого вид ED_V_PALETTE). Здесь только то, что РАБОТАЕТ:
// заглушек-пустышек нет — действие попадает сюда вместе со своим исполнением.
static const EdCmd ED_MODE_ACTIONS[] = {
    // Панель «PALETTE»: чем правим интерфейс — здесь и ТОЛЬКО здесь (строки
    // Panel/Button/Label/Delete). tab у этих записей не используется: в ленте они
    // не рисуются (правило «одна кнопка на действие»).
    {"ui.spawn.panel", "Panel", "Create", 0, ED_WHEN_ALWAYS, ED_K_SPAWN, ED_V_PALETTE,
     UI_W_PANEL, 0, 0, 0, 0, ED_DM_FRONT},
    {"ui.spawn.button", "Button", "Create", 0, ED_WHEN_ALWAYS, ED_K_SPAWN, ED_V_PALETTE,
     UI_W_BUTTON, 0, 0, 0, 0, ED_DM_FRONT},
    {"ui.spawn.label", "Label", "Create", 0, ED_WHEN_ALWAYS, ED_K_SPAWN, ED_V_PALETTE,
     UI_W_LABEL, 0, 0, 0, 0, ED_DM_FRONT},
    // HOME: правка выделения + история + инструменты
    {"edit.rename", "Rename", "Edit", 0, ED_WHEN_SEL, ED_K_CMD, 0, ED_CMD_RENAME, GLFW_KEY_F2,
     0, 0, 0, ED_DM_FRONT},
    {"edit.delete", "Delete", "Edit", 0, ED_WHEN_SEL, ED_K_CMD, ED_V_PALETTE, ED_CMD_DELETE,
     GLFW_KEY_DELETE, 0, 0, 0, ED_DM_FRONT}, // строка палитры: в ленте не рисуется
    {"hist.undo", "Undo", "History", 0, ED_WHEN_UNDO, ED_K_CMD, 0, ED_CMD_UNDO, GLFW_KEY_Z,
     ED_MOD_CTRL, 0, 0, ED_DM_FRONT},
    {"hist.redo", "Redo", "History", 0, ED_WHEN_REDO, ED_K_CMD, 0, ED_CMD_REDO, GLFW_KEY_Y,
     ED_MOD_CTRL, GLFW_KEY_Z, ED_MOD_CTRLSHIFT, ED_DM_FRONT},
    {"tools.self", "Editor UI", "Tools", 0, ED_WHEN_ALWAYS, ED_K_TOGGLE, 0, ED_TOG_SELF,
     GLFW_KEY_F7, 0, 0, 0, ED_DM_FRONT},
    {"tools.keys", "Cheatsheet", "Tools", 0, ED_WHEN_ALWAYS, ED_K_CMD, 0, ED_CMD_KEYS,
     GLFW_KEY_F1, 0, 0, 0, ED_DM_FRONT},
    // VIEW: галочки инструментов (View в Word / панели в Roblox Studio)
    {"view.panel.palette", "Palette", "Panels", 2, ED_WHEN_ALWAYS, ED_K_TOGGLE, 0, 0,
     GLFW_KEY_1, ED_MOD_CTRL, 0, 0, ED_DM_FRONT},
    {"view.panel.tree", "Tree", "Panels", 2, ED_WHEN_ALWAYS, ED_K_TOGGLE, 0, 1, GLFW_KEY_2,
     ED_MOD_CTRL, 0, 0, ED_DM_FRONT},
    {"view.panel.inspector", "Inspector", "Panels", 2, ED_WHEN_ALWAYS, ED_K_TOGGLE, 0, 2,
     GLFW_KEY_3, ED_MOD_CTRL, 0, 0, ED_DM_FRONT},
    // Ш1c: консоль — нижний док. В ленте VIEW её кнопка (Ctrl+4), как у соседей.
    {"view.panel.console", "Console", "Panels", 2, ED_WHEN_ALWAYS, ED_K_TOGGLE, 0, 3,
     GLFW_KEY_4, ED_MOD_CTRL, 0, 0, ED_DM_FRONT},
    // Очистка журнала: ЕДИНСТВЕННОЕ видимое место — кнопка «Clear» в шапке
    // консоли (вид ED_V_DOCK), поэтому в ленте и палитре её нет: одна команда —
    // одна кнопка. Клавиши нет (как в VS Code: чистят кнопкой).
    {"console.clear", "Clear", "Log", 2, ED_WHEN_ALWAYS, ED_K_CMD, ED_V_DOCK, ED_CMD_LOGCLR,
     0, 0, 0, 0, ED_DM_FRONT},
    // MODEL: инструменты РЕДАКТОРА СЦЕНЫ = режим инструмента (radio). Домен SCENE.
    // Эти же четыре режима раньше дублировал легаси-тулбар ui_panels (Q:Sel/W:Move/
    // E:Rot/R:Scale) — теперь источник ОДИН (реестр), а легаси-полос в окне нет.
    // Режим — состояние движка (ui_panels_set_tool): его читает гизмо в игре.
    // Snap здесь НЕ стоит: сетки/шага ещё нет (F5i) — кнопок-пустышек не заводим.
    {"scene.select", "Select", "Tool", 1, ED_WHEN_ALWAYS, ED_K_TOOL, 0, 0, GLFW_KEY_Q, 0, 0, 0,
     ED_DM_SCENE},
    {"scene.move", "Move", "Tool", 1, ED_WHEN_ALWAYS, ED_K_TOOL, 0, 1, GLFW_KEY_W, 0, 0, 0,
     ED_DM_SCENE},
    {"scene.rotate", "Rotate", "Tool", 1, ED_WHEN_ALWAYS, ED_K_TOOL, 0, 2, GLFW_KEY_E, 0, 0, 0,
     ED_DM_SCENE},
    {"scene.scale", "Scale", "Tool", 1, ED_WHEN_ALWAYS, ED_K_TOOL, 0, 3, GLFW_KEY_R, 0, 0, 0,
     ED_DM_SCENE},
};
#define ED_MODE_N ((int)(sizeof ED_MODE_ACTIONS / sizeof ED_MODE_ACTIONS[0]))
// Записи режима уходят в ОБЩИЙ реестр ядра: своей копии списка действий здесь
// больше нет, все виды (лента/палитра/шпаргалка/клавиши) читают РЕЕСТР.
static void ed_cmd_register_mode(void) {
    for (int i = 0; i < ED_MODE_N; i++) ed_cmd_add(&ED_MODE_ACTIONS[i]);
}
// ED_ACT — это РЕЕСТР (все записи: хоста и режимов), а не локальная таблица.
#define ED_ACT ed_cmd_all()

static void wt_persist(void); // определены НИЖЕ по файлу
static void undo_commit(const char *what);
static int wt_undo_apply(int redo);
static void name_edit_begin(void);
static int wt_delete_sel(void);
static void ed_self_toggle(int on);
static void edit_push(void);
// История документа: нужна гейту canExecute (есть ли что откатывать).
static UiWidgetUndo g_undo;

// --- A2: ДОКУМЕНТ как ИНТЕРФЕЙС ХОСТА ---------------------------------------
// Хост-команды (hist.undo/redo, edit.delete/rename) спрашивают ДОКУМЕНТ, а не
// виджеты: что открыто, есть ли что откатить, есть ли выделение. Что внутри —
// дело ЭТОГО режима; для хоста это просто «записи активного документа».
static int ui_doc_dirty(void) {
    // Режим пишет layout.json на каждую правку — несохранённого не бывает.
    return 0;
}
static int ui_doc_save(void) {
    wt_persist();
    return 1;
}
static int ui_doc_can_undo(void) { return ui_wundo_depth(&g_undo) > 0; }
static int ui_doc_can_redo(void) { return ui_wundo_redo_depth(&g_undo) > 0; }
static int ui_doc_undo(void) { return wt_undo_apply(0); }
static int ui_doc_redo(void) { return wt_undo_apply(1); }
static int ui_doc_has_sel(void) { return g_wtSel > 0 && widget_cget(g_act, g_wtSel) != 0; }
static int ui_doc_del_sel(void) { return wt_delete_sel(); }
static int ui_doc_rename_sel(void) {
    name_edit_begin();
    return 1;
}
// Документ режима «UI»: здесь только описание СЕБЯ (хост не знает, что внутри).
static const EdDoc UI_DOC = {"ui", "UI", ui_doc_dirty, ui_doc_save, ui_doc_can_undo,
                             ui_doc_can_redo, ui_doc_undo, ui_doc_redo, ui_doc_has_sel,
                             ui_doc_del_sel, ui_doc_rename_sel};

// Ширина строки — тот же advance, что у текста (иначе подпись и кнопка разъедутся).
static float ed_txt_w(const char *s) {
    return g_text ? ui_text_width(g_text, s) : (float)(8 * (int)strlen(s));
}
// --- палитра = ВИД реестра (второго списка строк больше нет) ----------------
static int ed_pal_n(void) {
    int n = 0;
    for (int i = 0; i < ed_cmd_n(); i++)
        if (ED_ACT[i].views & ED_V_PALETTE) n++;
    return n;
}
static int ed_pal_act(int row) {
    if (row < 0) return -1;
    for (int i = 0, k = 0; i < ed_cmd_n(); i++)
        if (ED_ACT[i].views & ED_V_PALETTE) {
            if (k == row) return i;
            k++;
        }
    return -1;
}
static const char *ed_pal_label(int row) {
    int a = ed_pal_act(row);
    return a >= 0 ? ED_ACT[a].label : "";
}
static int ed_pal_is_action(int row) {
    int a = ed_pal_act(row);
    return a >= 0 && ED_ACT[a].kind != ED_K_SPAWN; // строка-ДЕЙСТВИЕ (не создание)
}
// Имя типа виджета — из ТОГО ЖЕ реестра (у записи-создания arg = тип). Третьей
// копии строк «Panel»/«Button»/«Label» (дерево/инспектор) больше нет.
static const char *wt_type_name(int type) {
    return ed_cmd_label_of(ED_K_SPAWN, type); // имя типа — из той же записи реестра
}
// Тумблер включён? — одна точка правды для галочки на ленте.
static int act_impl_on(int i) {
    const EdCmd *a = ed_cmd_at(i);
    if (!a) return 0;
    if (a->kind == ED_K_TOOL) // режим сцены: включён ровно один
        return g_panels && a->arg >= 0 && ui_panels_tool(g_panels) == a->arg;
    if (a->kind != ED_K_TOGGLE) return 0;
    if (a->arg == ED_TOG_SELF) return g_editSelf;
    if (a->arg >= 0 && a->arg < ed_ws_panels()) return g_edOpen[a->arg];
    return 0;
}
// canExecute: ОДНА точка правды «действие сейчас доступно».
static int act_impl_enabled(int i) {
    const EdCmd *a = ed_cmd_at(i);
    if (!a) return 0;
    switch (a->when) {
    case ED_WHEN_ALWAYS: return 1;
    // Гейты спрашивают АКТИВНЫЙ ДОКУМЕНТ: ядру нечего знать про виджеты.
    case ED_WHEN_SEL: return ed_doc_has_sel();
    case ED_WHEN_UNDO: return ed_doc_can_undo();
    case ED_WHEN_REDO: return ed_doc_can_redo();
    default: return 0; // ED_WHEN_NEVER: ещё не реализовано
    }
}
static int cmd_builtin(int cmd) {
    switch (cmd) {
    // Команды ДОКУМЕНТА: хост не знает, что за документ открыт.
    case ED_CMD_RENAME: return ed_doc_rename_sel();
    case ED_CMD_DELETE: return ed_doc_del_sel();
    case ED_CMD_UNDO: return ed_doc_undo();
    case ED_CMD_REDO: return ed_doc_redo();
    // Команды ХОСТА: к документу не относятся.
    case ED_CMD_KEYS:
        g_edShowKeys = !g_edShowKeys;
        edit_push();
        return 1;
    case ED_CMD_LOGCLR:
        ui_log_clear(); // «Clear» = чистый журнал, без строки-отметки (как Clear в VS Code)
        edit_push();
        return 1;
    default: return 0;
    }
}
// Выполнить действие. Ядро зовёт это ТОЛЬКО после гейта (ed_cmd_run), поэтому
// здесь нет проверки «а можно ли». 0 = отказалось — вызывающий сам решает,
// съесть клавишу или отдать дальше (undo/redo делят историю с доками).
static int act_impl_run(int i) {
    const EdCmd *a = ed_cmd_at(i);
    if (!a) return 0;
    if (a->kind == ED_K_SPAWN) {
        int id = wt_spawn(a->arg);
        if (!id) return 0;
        wt_persist();
        undo_commit("add");
        edit_push();
        return 1;
    }
    if (a->kind == ED_K_TOOL) { // режим инструмента сцены — в движок (его читает гизмо)
        if (!g_panels) return 0;
        ui_panels_set_tool(g_panels, a->arg);
        ui_log_add("scene tool: %s", a->label);
        ed_panels_save();
        edit_push();
        return 1;
    }
    if (a->kind == ED_K_TOGGLE) {
        if (a->arg == ED_TOG_SELF) ed_self_toggle(!g_editSelf);
        else tabs_toggle(a->arg);
        // Ш1c: журнал показывает, что инструменты переключались (ярлык — из
        // реестра, состояние — из ed_cmd_on: ни то, ни другое не копируем).
        if (a->arg != ED_TOG_SELF)
            ui_log_add("panel %s: %s", a->label, ed_cmd_on(i) ? "on" : "off");
        return 1;
    }
    return cmd_builtin(a->arg);
}
// Сервисы ядра: РЕЕСТР принадлежит редактору, а ИСПОЛНЕНИЕ пока здешнее
// (A3/A4 разделят его на хост-часть и часть режима UI). Зовётся из ed_chrome_init:
// так реестр есть и в приложении, и в selftest, и не регистрируется дважды.
static void ed_cmd_init(void) {
    if (ed_cmd_n() > 0) return;
    static const EdCmdHost HOST = {act_impl_enabled, act_impl_on, act_impl_run};
    ed_cmd_host(&HOST);
    ed_cmd_register_mode();
}
// РЕЖИМ РАБОТЫ «UI» — это данные: ленты и панели-инструменты. Одна запись даёт
// и ленту (подписи вкладок), и доки, и подписи шапок, и что открыто при старте.
// Пятая панель = строка ЗДЕСЬ, а не охота за «3»/«4» по всему файлу.
static const EdWorkspace UI_WORKSPACE = {
    "ui",
    "UI",
    {"HOME", "MODEL", "VIEW"},
    3,
    {
        {"palette", "PALETTE", ED_DK_LEFT, 1},
        {"tree", "WIDGETS", ED_DK_LEFT, 1},
        {"inspector", "INSPECTOR", ED_DK_RIGHT, 1},
        {"console", "CONSOLE", ED_DK_BOTTOM, 0}, // Ш1c: консоль закрыта по умолчанию
    },
    4,
};
// Инициализация ХОСТА: реестр команд + документы + таблица режимов. Зовётся из
// ed_chrome_init — так и приложение, и selftest получают одно и то же и не
// регистрируют дважды. На A3 переедет в editor_main.c.
static void ed_host_init(void) {
    ed_cmd_init();
    if (ed_doc_n() == 0) {
        ed_doc_add(&UI_DOC);
        ed_doc_active_set(0);
    }
    if (ed_ws_n() == 0) { // режим заводится ОДИН раз (ресет в тестах — заново)
        ed_ws_add(&UI_WORKSPACE);
        ed_ws_active_set(0);
        // Открытость панелей при старте — из таблицы режима (не литералы здесь).
        for (int i = 0; i < ed_ws_panels(); i++) g_edOpen[i] = ed_ws_open_default(i);
    }
}
// --- геометрия ленты -------------------------------------------------------
static void ed_ribbon_band(float *x, float *y, float *w, float *h) {
    *x = 0.0f;
    *y = (float)(ED_TOP_CHROME + ED_TABS_H);
    *w = (float)g_WW;
    *h = (float)ED_RIBBON_H;
}
static float ed_ribbon_item_w(const EdCmd *a) {
    float tw = ed_txt_w(a->label);
    if (a->kind == ED_K_TOGGLE || a->kind == ED_K_TOOL)
        return tw + 34.0f; // метка-галочка/радио + поля
    return tw + 22.0f;
}
#define ED_RB_MAX 64
static int g_rbAct[ED_RB_MAX]; // слот -> индекс действия
static int g_rbGrpSlot[ED_RB_MAX];
static const char *g_rbGrp[ED_RB_MAX];
static float g_rbX[ED_RB_MAX], g_rbW[ED_RB_MAX];
static int g_rbN = 0, g_rbGroupN = 0;
// Кнопки активной ленты. ОДНА функция на рисование/хит/текст/тултип — иначе
// подпись и клик разъезжаются (как с крошками дерева).
static void ed_ribbon_layout(void) {
    g_rbN = 0;
    g_rbGroupN = 0;
    const char *grp = 0;
    float x = (float)PAL_PAD;
    for (int i = 0; i < ed_cmd_n() && g_rbN < ED_RB_MAX; i++) {
        if (!ed_cmd_ribbon(i)) continue; // у действия своя строка в палитре
        if ((int)ED_ACT[i].tab != g_edTab) continue;
        if (!grp || strcmp(grp, ED_ACT[i].group) != 0) {
            if (grp) x += (float)ED_RIBBON_SEP; // зазор между группами
            grp = ED_ACT[i].group;
            g_rbGroupN++;
        }
        g_rbAct[g_rbN] = i;
        g_rbGrpSlot[g_rbN] = g_rbGroupN - 1;
        g_rbGrp[g_rbN] = ED_ACT[i].group;
        g_rbX[g_rbN] = x;
        g_rbW[g_rbN] = ed_ribbon_item_w(&ED_ACT[i]);
        x += g_rbW[g_rbN] + (float)ED_RIBBON_GAP;
        g_rbN++;
    }
}
static void ed_ribbon_item_rect(int act, float *x, float *y, float *w, float *h) {
    float bx, by, bw, bh;
    ed_ribbon_band(&bx, &by, &bw, &bh);
    (void)bx;
    (void)bw;
    ed_ribbon_layout();
    for (int s = 0; s < g_rbN; s++) {
        if (g_rbAct[s] != act) continue;
        *x = g_rbX[s];
        *y = by + 4.0f;
        *w = g_rbW[s];
        *h = (float)ED_RIBBON_ITEM_H;
        return;
    }
    *x = *y = *w = *h = 0;
}
// Подложка группы g — по границам её кнопок.
static void ed_ribbon_group_rect(int g, float *x, float *y, float *w, float *h) {
    float bx, by, bw, bh;
    ed_ribbon_band(&bx, &by, &bw, &bh);
    (void)bx;
    (void)bw;
    ed_ribbon_layout();
    float x0 = 0, x1 = 0;
    int got = 0;
    for (int s = 0; s < g_rbN; s++) {
        if (g_rbGrpSlot[s] != g) continue;
        if (!got) {
            x0 = g_rbX[s] - 3.0f;
            got = 1;
        }
        x1 = g_rbX[s] + g_rbW[s] + 3.0f;
    }
    *x = x0;
    *y = by + 1.0f;
    *w = x1 - x0;
    *h = bh - 3.0f;
}
static int ed_ribbon_hit(float px, float py, int *slot) {
    float bx, by, bw, bh;
    ed_ribbon_band(&bx, &by, &bw, &bh);
    (void)bx;
    (void)bw;
    (void)bh;
    ed_ribbon_layout();
    for (int s = 0; s < g_rbN; s++) {
        float x = g_rbX[s], y = by + 4.0f, w = g_rbW[s];
        if (px >= x && px < x + w && py >= y && py < y + (float)ED_RIBBON_ITEM_H) {
            if (slot) *slot = s;
            return 1;
        }
    }
    return 0;
}
// Текст тултипа: "Delete  (Del)" / "... [off]" у нереализованного.
static void ed_ribbon_tip_text(int act, char *buf, int cap) {
    char kb[48];
    ed_cmd_keys(act, kb, sizeof kb);
    const EdCmd *a = &ED_ACT[act];
    if (!ed_cmd_enabled(act)) snprintf(buf, cap, "%s [off]", a->label);
    else if (kb[0]) snprintf(buf, cap, "%s  (%s)", a->label, kb);
    else snprintf(buf, cap, "%s", a->label);
}
static void ed_ribbon_tip_rect(float *x, float *y, float *w, float *h) {
    *x = *y = *w = *h = 0;
    if (g_rbHot < 0) return;
    float bx, by, bw, bh;
    ed_ribbon_band(&bx, &by, &bw, &bh);
    (void)bx;
    (void)bw;
    ed_ribbon_layout();
    int s = -1;
    for (int k = 0; k < g_rbN; k++)
        if (g_rbAct[k] == g_rbHot) s = k;
    if (s < 0) return;
    char tip[160];
    ed_ribbon_tip_text(g_rbHot, tip, sizeof tip);
    float tw = ed_txt_w(tip) + 14.0f;
    *w = tw;
    *h = 20.0f;
    *x = g_rbX[s];
    if (*x + tw > (float)g_WW) *x = (float)g_WW - tw;
    if (*x < 0) *x = 0;
    *y = by + bh + 2.0f;
}
// --- шпаргалка клавиш (как Keybind-оверлей: одна таблица — один список) -----
#define ED_KEYS_W 380
#define ED_KEYS_RH 14
static void ed_keys_rect(float *x, float *y, float *w, float *h) {
    float bx, by, bw, bh;
    ed_ribbon_band(&bx, &by, &bw, &bh);
    (void)bx;
    (void)bw;
    *w = (float)ED_KEYS_W;
    *h = 26.0f + (float)ed_cmd_n() * (float)ED_KEYS_RH + 8.0f;
    *x = (float)(g_vpx > 0 ? g_vpx : ED_DOCK_W + PAL_PAD) + 8.0f;
    *y = by + bh + 6.0f;
    if (*x + *w > (float)g_WW) *x = (float)g_WW - *w;
    if (*x < 0) *x = 0;
    if (*y + *h > (float)g_WH) *y = (float)g_WH - *h;
    if (*y < 0) *y = 0;
}
static int ed_keys_hit(float px, float py) {
    float x, y, w, h;
    ed_keys_rect(&x, &y, &w, &h);
    return px >= x && px < x + w && py >= y && py < y + h;
}
// §14 F5l(2): полоса кнопок АКТИВНОЙ ленты — группы на подложках с подписью.
// Выключенное действие (гейт canExecute) приглушено, тумблер панели — с галочкой.
static void ed_ribbon_draw(RcVecShape *sh, int *n) {
    float bx, by, bw, bh;
    ed_ribbon_band(&bx, &by, &bw, &bh);
    static const float band[4] = {0.08f, 0.09f, 0.12f, 1.0f};
    static const float grp[4] = {0.125f, 0.14f, 0.18f, 1.0f};
    static const float btn[4] = {0.19f, 0.215f, 0.27f, 1.0f};
    static const float hot[4] = {0.27f, 0.32f, 0.43f, 1.0f};
    static const float dim[4] = {0.125f, 0.13f, 0.155f, 1.0f};
    static const float ton[4] = {0.22f, 0.38f, 0.62f, 1.0f};
    static const float mark[4] = {0.35f, 0.62f, 0.95f, 1.0f};
    static const float edge[4] = {0.26f, 0.29f, 0.36f, 1.0f};
    wt_emit(sh, n, bx, by, bw, bh, band, 0.0f);
    wt_emit(sh, n, bx, by + bh - 1.0f, bw, 1.0f, edge, 0.0f);
    ed_ribbon_layout();
    for (int g = 0; g < g_rbGroupN; g++) {
        float x, y, w, h;
        ed_ribbon_group_rect(g, &x, &y, &w, &h);
        if (w > 0) wt_emit(sh, n, x, y, w, h, grp, 4.0f);
    }
    for (int s = 0; s < g_rbN; s++) {
        const EdCmd *a = &ED_ACT[g_rbAct[s]];
        float x = g_rbX[s], w = g_rbW[s], y = by + 4.0f, h = (float)ED_RIBBON_ITEM_H;
        int en = ed_cmd_enabled(g_rbAct[s]);
        int on = ed_cmd_on(g_rbAct[s]);
        const float *col = !en ? dim : (on ? ton : (g_rbHot == g_rbAct[s] ? hot : btn));
        wt_emit(sh, n, x, y, w, h, col, 3.0f);
        if (a->kind == ED_K_TOGGLE || a->kind == ED_K_TOOL) {
            // Тумблер — квадрат-галочка, режим сцены — КРУГ (radio): сразу видно,
            // что включён ровно один инструмент, а не набор независимых галочек.
            int tool = a->kind == ED_K_TOOL;
            float cx = x + 7.0f, cy = y + (h - 9.0f) * 0.5f;
            wt_emit(sh, n, cx, cy, 9.0f, 9.0f, btn, tool ? 4.5f : 1.0f);
            if (on) wt_emit(sh, n, cx + 2.0f, cy + 2.0f, 5.0f, 5.0f, mark, tool ? 2.5f : 1.0f);
        }
    }
}
// Тултип и шпаргалка — ПОВЕРХ всего: окна панелей их не кроют (иначе подсказка
// пропадала бы под доком, как текст под панелью в F5l(5)).
static void ed_ribbon_top_draw(RcVecShape *sh, int *n) {
    static const float tipbg[4] = {0.05f, 0.06f, 0.09f, 0.96f};
    static const float tiped[4] = {0.35f, 0.5f, 0.75f, 1.0f};
    static const float kbg[4] = {0.07f, 0.08f, 0.11f, 0.97f};
    static const float ked[4] = {0.35f, 0.5f, 0.75f, 1.0f};
    if (g_rbHot >= 0) {
        float x, y, w, h;
        ed_ribbon_tip_rect(&x, &y, &w, &h);
        if (w > 0) {
            wt_emit(sh, n, x, y, w, h, tipbg, 3.0f);
            wt_emit(sh, n, x, y, w, 1.0f, tiped, 0.0f);
        }
    }
    if (g_edShowKeys) {
        float x, y, w, h;
        ed_keys_rect(&x, &y, &w, &h);
        wt_emit(sh, n, x, y, w, h, kbg, 5.0f);
        wt_emit(sh, n, x, y, w, 1.0f, ked, 0.0f);
    }
}
static float *ins_slot(UiWidget *w, int f) {
    switch (f) {
    case IF_X: return &w->rect.x;
    case IF_Y: return &w->rect.y;
    case IF_W: return &w->rect.w;
    case IF_H: return &w->rect.h;
    case IF_ROUND: return &w->style.rounding;
    case IF_ALPHA: return &w->style.alpha;
    case IF_FONT: return &w->style.fontSize;
    case IF_BG_R: return &w->style.bg[0];
    case IF_BG_G: return &w->style.bg[1];
    case IF_BG_B: return &w->style.bg[2];
    }
    return 0;
}
static float ins_step(int f) {
    if (f == IF_ROUND) return 0.25f;
    if (f == IF_ALPHA || (f >= IF_BG_R && f <= IF_BG_B)) return 0.01f;
    return 1.0f;
}
static void ins_clamp(int f, float *v) {
    if (f == IF_W || f == IF_H) {
        if (*v < 8.0f) *v = 8.0f;
    } else if (f == IF_ROUND) {
        if (*v < 0) *v = 0;
    } else if (f == IF_ALPHA || (f >= IF_BG_R && f <= IF_BG_B)) {
        if (*v < 0) *v = 0;
        if (*v > 1) *v = 1;
    } else if (f == IF_FONT) {
        if (*v < 6) *v = 6;
        if (*v > 48) *v = 48;
    }
}
static int ins_hit(float px, float py) {
    if (!g_wtSel || !ed_live(2)) return -1;
    for (int i = 0; i < IF_N; i++) {
        float x, y, w, h;
        ins_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}
static void ins_px(int *out) {
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    if (!w) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    float x, y, ww, hh;
    if (g_act == &g_wt) doc_px(w, &x, &y, &ww, &hh);
    else widget_rect_px(w, g_WW, g_WH, &x, &y, &ww, &hh);
    out[0] = (int)x;
    out[1] = (int)y;
    out[2] = (int)(ww + 0.5f);
    out[3] = (int)(hh + 0.5f);
}
static void ins_draw(RcVecShape *sh, int *n) {
    // F5l(5): закрытая панель получает rect 0x0 — из него ряды уезжали в угол (0,0).
    if (!g_wtSel || !ed_live(2)) return;
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    if (!w) return;
    float tx, ty, tw, th;
    ins_rect(-1, &tx, &ty, &tw, &th); // заголовок внутри панели хрома
    float hc[4] = {0.22f, 0.24f, 0.30f, 1.0f};
    wt_emit(sh, n, tx, ty, tw, th, hc, 3.0f);
    // F5a: поле имени — выделение и каретка. Ширина считается ТОЙ ЖЕ метрикой,
    // что у текста (ui_text_width), иначе каретка «плыла» бы относительно букв.
    if (g_nameEdit && g_text) {
        char pre[UI_WIDGET_NAME];
        if (name_has_sel()) {
            int lo = name_sel_lo(), hi = name_sel_hi();
            int k = lo < UI_WIDGET_NAME - 1 ? lo : UI_WIDGET_NAME - 1;
            memcpy(pre, g_nameBuf, (size_t)k);
            pre[k] = 0;
            int sl = hi - lo;
            if (sl > UI_WIDGET_NAME - 1) sl = UI_WIDGET_NAME - 1;
            char sel[UI_WIDGET_NAME];
            memcpy(sel, g_nameBuf + lo, (size_t)sl);
            sel[sl] = 0;
            float sc[4] = {0.26f, 0.38f, 0.66f, 1.0f};
            wt_emit(sh, n, tx + 6 + ui_text_width(g_text, pre), ty + 2,
                    ui_text_width(g_text, sel), th - 4, sc, 2.0f);
        }
        if (g_caretOn) {
            int k = g_nameCaret < UI_WIDGET_NAME - 1 ? g_nameCaret : UI_WIDGET_NAME - 1;
            memcpy(pre, g_nameBuf, (size_t)k);
            pre[k] = 0;
            float cc[4] = {0.95f, 0.97f, 1.0f, 1.0f};
            wt_emit(sh, n, tx + 6 + ui_text_width(g_text, pre), ty + 2, 1.5f, th - 4, cc,
                    0.0f);
        }
    }
    for (int i = 0; i < IF_N; i++) {
        float x, y, ww, hh;
        ins_rect(i, &x, &y, &ww, &hh);
        float c[4] = {0.16f, 0.17f, 0.21f, 1.0f};
        if (i == g_insField) { c[0] = 0.30f; c[1] = 0.45f; c[2] = 0.75f; }
        wt_emit(sh, n, x, y, ww, hh, c, 3.0f);
        if (i >= IF_BG_R && i <= IF_BG_B) {
            float sc[4] = {w->style.bg[0], w->style.bg[1], w->style.bg[2], 1.0f};
            wt_emit(sh, n, x + ww - 18, y + 3, 14, hh - 6, sc, 2.0f);
        }
    }
}

// --- F2b: драг/ресайз (frozen-frame как гизмо: якорь на DOWN) ---
// Хэндл = битмаска краёв: L=1 R=2 T=4 B=8; 0 = перемещение.
static int g_dragId = 0;
static int g_dragHandle = 0;
static float g_dragMX = 0, g_dragMY = 0;             // мышь на DOWN (px окна)
static float g_dragX = 0, g_dragY = 0, g_dragW = 0, g_dragH = 0; // rect на DOWN
static float g_prevX = 0, g_prevY = 0, g_prevW = 0, g_prevH = 0; // прошлый шаг
// Dirty-регионы шага драга: список rect (x,y,w,h). Перемещение -> старый+новый;
// ресайз -> полосы дельты (что реально изменилось), а не весь виджет — иначе
// цена растёт вместе с размером (это и был «лаг»).
// ВАЖНО: массив и предел ОБЯЗАНЫ совпадать. Раньше было g_dirtyR[8][4], а макрос
// писал до 12 — запись МИМО массива (затирало g_dirtyN и соседние статики: грязь
// превращалась в мусорные регионы, на экране полосы). Теперь предел = размер, а
// переполнение честно откатывает кадр в ПОЛНЫЙ (как UE: регионов много -> всё).
#define WT_MAX_DIRTY 16
static int g_part = 0;
static int g_dirtyR[WT_MAX_DIRTY][4];
static int g_dirtyN = 0;
static int g_dirtyOver = 0; // 1 = регионов больше, чем влезает: рисуем кадр целиком

#define WT_GRAB 9.0f
#define WT_MIN 8.0f

// Хэндл ресайза под точкой (у выбранного). 0 = не хэндл (тело/мимо).
static int wt_handle_hit(float px, float py) {
    const UiWidget *s = widget_cget(g_act, g_wtSel);
    if (!s) return 0;
    float x, y, w, h;
    if (doc_live()) { // ручки документа — только внутри экрана игры
        if (px < (float)g_vpx || px >= (float)(g_vpx + g_vpw) || py < (float)g_vpy ||
            py >= (float)(g_vpy + g_vph))
            return 0;
        px -= doc_ox();
        py -= doc_oy();
        widget_rect_px(s, doc_w(), doc_h(), &x, &y, &w, &h);
    } else {
        widget_rect_px(s, g_WW, g_WH, &x, &y, &w, &h);
    }
    if (px < x - 3 || px > x + w + 3 || py < y - 3 || py > y + h + 3) return 0;
    // Малый виджет: полоса захвата не больше трети стороны — иначе весь
    // прямоугольник станет «ручкой» и тело не подвинуть (баг «ресайз вместо move»).
    float gx = WT_GRAB, gy = WT_GRAB;
    if (w < 3 * WT_GRAB) gx = w / 3;
    if (h < 3 * WT_GRAB) gy = h / 3;
    int hb = 0;
    if (px <= x + gx) hb |= 1;
    if (px >= x + w - gx) hb |= 2;
    if (py <= y + gy) hb |= 4;
    if (py >= y + h - gy) hb |= 8;
    return hb;
}

static void wt_drag_begin(int id, int handle, float mx, float my) {
    const UiWidget *w = widget_cget(g_act, id);
    if (!w) return;
    if (g_editSelf) g_edMoved[id - 1] = 1; // правка юзера: раскладка больше не вернёт
    g_dragId = id;
    g_dragHandle = handle;
    g_dragMX = mx;
    g_dragMY = my;
    // F5l(3): геометрия драга — в координатах ЭКРАНА ИГРЫ (модель документа).
    if (g_act == &g_wt) widget_rect_px(w, doc_w(), doc_h(), &g_dragX, &g_dragY, &g_dragW, &g_dragH);
    else widget_rect_px(w, g_WW, g_WH, &g_dragX, &g_dragY, &g_dragW, &g_dragH);
    g_prevX = g_dragX;
    g_prevY = g_dragY;
    g_prevW = g_dragW;
    g_prevH = g_dragH;
    g_part = 0;
}

// Обратно из пикселей в якоря (инверсия widget_rect_px).
static void pt_to_anchor(UiWidget *w, float px, float py, float pw, float ph, int Wpx,
                         int Hpx) {
    if (!w) return;
    w->rect.w = pw;
    w->rect.h = ph;
    if (w->rect.anchors & UI_A_LEFT) w->rect.x = px;
    else if (w->rect.anchors & UI_A_RIGHT) w->rect.x = (float)Wpx - px - pw;
    else w->rect.x = px - ((float)Wpx - pw) * 0.5f;
    if (w->rect.anchors & UI_A_TOP) w->rect.y = py;
    else if (w->rect.anchors & UI_A_BOTTOM) w->rect.y = (float)Hpx - py - ph;
    else w->rect.y = py - ((float)Hpx - ph) * 0.5f;
}

// --- §14 F5l: ГРУППА ЕДЕТ ЗА РОДИТЕЛЕМ ---
// Модель координат АБСОЛЮТНАЯ (якоря к окну), поэтому при драге родителя дети
// сами не ехали — и «виджет разваливался пополам» (тестер: сдвинул панель, а
// подписи остались на месте). Figma/UMG/WPF тянут ПОДДЕРЕВО за родителем; здесь
// то же, но без смены модели: каждого потомка сдвигаем на ту же дельту в
// ПИКСЕЛЯХ (у якоря R/B знак оффсета обратный — иначе ребёнок уедет не туда).
static void wt_shift_px(UiWidget *w, float sdx, float sdy) {
    if (!w) return;
    if (w->rect.anchors & UI_A_RIGHT) w->rect.x -= sdx;
    else w->rect.x += sdx; // LEFT и центр двигаются тем же знаком
    if (w->rect.anchors & UI_A_BOTTOM) w->rect.y -= sdy;
    else w->rect.y += sdy;
}
static void wt_subtree_walk(int id, float sdx, float sdy) {
    UiWidget *w = widget_get(g_act, id);
    if (!w) return;
    wt_shift_px(w, sdx, sdy);
    for (int i = 0; i < w->nchildren; i++) wt_subtree_walk(w->children[i], sdx, sdy);
}
// Сдвиг ПОДДЕРЕВА (родитель + потомки). Дети ходят за родителем.
static void wt_subtree_shift(int id, float sdx, float sdy) {
    if (sdx == 0.0f && sdy == 0.0f) return;
    wt_subtree_walk(id, sdx, sdy);
}
// Сдвиг только ПОТОМКОВ (родителя правит вызывающий — поле инспектора).
static void wt_subtree_shift_kids(int id, float sdx, float sdy) {
    const UiWidget *w = widget_cget(g_act, id);
    if (!w || (sdx == 0.0f && sdy == 0.0f)) return;
    for (int i = 0; i < w->nchildren; i++) wt_subtree_walk(w->children[i], sdx, sdy);
}
static void wt_bounds_walk(int id, float *x0, float *y0, float *x1, float *y1, int *n) {
    const UiWidget *w = widget_cget(g_act, id);
    if (!w) return;
    float x, y, ww, hh;
    if (g_act == &g_wt) widget_rect_px(w, doc_w(), doc_h(), &x, &y, &ww, &hh);
    else widget_rect_px(w, g_WW, g_WH, &x, &y, &ww, &hh);
    if (x < *x0) *x0 = x;
    if (y < *y0) *y0 = y;
    if (x + ww > *x1) *x1 = x + ww;
    if (y + hh > *y1) *y1 = y + hh;
    (*n)++;
    for (int i = 0; i < w->nchildren; i++)
        wt_bounds_walk(w->children[i], x0, y0, x1, y1, n);
}
// Границы ПОДДЕРЕВА в пикселях. Возвращает число виджетов (0 = нет; 1 = детей нет).
static int wt_subtree_bounds(int id, float *ox, float *oy, float *ow, float *oh) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int n = 0;
    wt_bounds_walk(id, &x0, &y0, &x1, &y1, &n);
    if (n <= 0) return 0;
    *ox = x0;
    *oy = y0;
    *ow = x1 - x0;
    *oh = y1 - y0;
    return n;
}
static int wt_has_kids(int id) {
    float a, b, c, d;
    return wt_subtree_bounds(id, &a, &b, &c, &d) > 1;
}

static void wt_drag_apply(float mx, float my) {
    UiWidget *w = widget_get(g_act, g_dragId);
    if (!w) {
        g_dragId = 0;
        return;
    }
    float dx = mx - g_dragMX, dy = my - g_dragMY;
    float nx = g_dragX, ny = g_dragY, nw = g_dragW, nh = g_dragH;
    float obx = 0, oby = 0, obw = 0, obh = 0;
    int okids = wt_subtree_bounds(g_dragId, &obx, &oby, &obw, &obh) > 1; // F5l
    if (g_dragHandle == 0) {
        nx += dx;
        ny += dy;
    } else {
        if (g_dragHandle & 1) {
            nx += dx;
            nw -= dx;
        }
        if (g_dragHandle & 2) nw += dx;
        if (g_dragHandle & 4) {
            ny += dy;
            nh -= dy;
        }
        if (g_dragHandle & 8) nh += dy;
        if (nw < WT_MIN) { // не даём вывернуться: фиксируем левый/верхний край
            if (g_dragHandle & 1) nx = g_dragX + g_dragW - WT_MIN;
            nw = WT_MIN;
        }
        if (nh < WT_MIN) {
            if (g_dragHandle & 4) ny = g_dragY + g_dragH - WT_MIN;
            nh = WT_MIN;
        }
    }
    // Dirty-регионы = СИММЕТРИЧЕСКАЯ РАЗНОСТЬ старого и нового rect (полосы по
    // сдвинутым краям) + срединные ручки, съезжающие ВНУТРИ неизменной середины.
    // Цена ~ ДЕЛЬТЕ драга, не размеру виджета. Единый путь для move и resize:
    // move = поехали все 4 края, resize = один-два. Если дельта огромная (флин),
    // полосы дороже — откат на объединение/два rect'а (честный выбор по площади).
    // F5l: у ГРУППЫ (есть потомки) середина НЕ неизменна — дети едут вместе с
    // родителем, поэтому полосы не годятся: регион = объединение СТАРЫХ и НОВЫХ
    // границ всего поддерева (иначе на экране остаются следы уехавших детей).
    if (g_dragHandle == 0 && okids) wt_subtree_shift(g_dragId, nx - g_prevX, ny - g_prevY);
    {
        const float M = 12.0f;
        float oL = g_prevX, oT = g_prevY, oR = g_prevX + g_prevW, oB = g_prevY + g_prevH;
        float nL = nx, nT = ny, nR = nx + nw, nB = ny + nh;
        float uL = oL < nL ? oL : nL, uT = oT < nT ? oT : nT;
        float uR = oR > nR ? oR : nR, uB = oB > nB ? oB : nB;
        float uh = uB - uT, uw = uR - uL;
        float dL = fabsf(oL - nL), dR = fabsf(oR - nR);
        float dT = fabsf(oT - nT), dB = fabsf(oB - nB);
        float oCx = (oL + oR) * 0.5f, nCx = (nL + nR) * 0.5f;
        float oCy = (oT + oB) * 0.5f, nCy = (nT + nB) * 0.5f;
        float stripA = (dL + dR) * (uh + 2 * M) + (dT + dB) * (uw + 2 * M);
        float twoA = (g_prevW + 2 * M) * (g_prevH + 2 * M) + (nw + 2 * M) * (nh + 2 * M);
        float uniA = (uw + 2 * M) * (uh + 2 * M);
        g_dirtyN = 0;
        g_dirtyOver = 0;
#define WT_ADD_RECT(X, Y, W, H)                                                        \
    do {                                                                               \
        if (g_dirtyN < WT_MAX_DIRTY) {                                                 \
            g_dirtyR[g_dirtyN][0] = (int)((X) + (g_act == &g_wt ? doc_ox() : 0.0f));   \
            g_dirtyR[g_dirtyN][1] = (int)((Y) + (g_act == &g_wt ? doc_oy() : 0.0f));   \
            g_dirtyR[g_dirtyN][2] = (int)(W);                                          \
            g_dirtyR[g_dirtyN][3] = (int)(H);                                          \
            g_dirtyN++;                                                                \
        } else {                                                                       \
            g_dirtyOver = 1; /* не влезло -> полный кадр, а не «потеряли регион» */     \
        }                                                                              \
    } while (0)
        if (okids) { // F5l: группа — объединение старых и новых границ поддерева
            float nbx = 0, nby = 0, nbw = 0, nbh = 0;
            wt_subtree_bounds(g_dragId, &nbx, &nby, &nbw, &nbh);
            float gL = obx < nbx ? obx : nbx, gT = oby < nby ? oby : nby;
            float gR = obx + obw > nbx + nbw ? obx + obw : nbx + nbw;
            float gB = oby + obh > nby + nbh ? oby + obh : nby + nbh;
            WT_ADD_RECT(gL - M, gT - M, gR - gL + 2 * M, gB - gT + 2 * M);
        } else if (stripA <= twoA && stripA <= uniA) { // основной путь: полосы дельты
            if (dL > 0.5f) WT_ADD_RECT(uL - M, uT - M, dL + 2 * M, uh + 2 * M);
            if (dR > 0.5f) WT_ADD_RECT(uR - dR - M, uT - M, dR + 2 * M, uh + 2 * M);
            if (dT > 0.5f) WT_ADD_RECT(uL - M, uT - M, uw + 2 * M, dT + 2 * M);
            if (dB > 0.5f) WT_ADD_RECT(uL - M, uB - dB - M, uw + 2 * M, dB + 2 * M);
            if (fabsf(nCx - oCx) > 0.5f) { // верх/низ срединные ручки съехали по X
                float cx = (oCx < nCx ? oCx : nCx) - WT_HANDLE;
                float cw = fabsf(nCx - oCx) + 2 * WT_HANDLE;
                WT_ADD_RECT(cx, uT - M, cw, dT + 2 * M);
                WT_ADD_RECT(cx, uB - dB - M, cw, dB + 2 * M);
            }
            if (fabsf(nCy - oCy) > 0.5f) { // лево/право срединные ручки съехали по Y
                float cy = (oCy < nCy ? oCy : nCy) - WT_HANDLE;
                float ch = fabsf(nCy - oCy) + 2 * WT_HANDLE;
                WT_ADD_RECT(uL - M, cy, dL + 2 * M, ch);
                WT_ADD_RECT(uR - dR - M, cy, dR + 2 * M, ch);
            }
            if (g_dirtyN == 0) // ничего не сдвинулось (клик без движения)
                WT_ADD_RECT(nL - M, nT - M, nw + 2 * M, nh + 2 * M);
        } else if (uniA <= twoA) { // флин: объединение одним rect'ом
            WT_ADD_RECT(uL - M, uT - M, uw + 2 * M, uh + 2 * M);
        } else { // флин: два rect'а (старый+новый)
            WT_ADD_RECT(oL - M, oT - M, g_prevW + 2 * M, g_prevH + 2 * M);
            WT_ADD_RECT(nL - M, nT - M, nw + 2 * M, nh + 2 * M);
        }
#undef WT_ADD_RECT
        g_part = 1;
        g_prevX = nx;
        g_prevY = ny;
        g_prevW = nw;
        g_prevH = nh;
    }
    if (g_act == &g_wt) pt_to_anchor(w, nx, ny, nw, nh, doc_w(), doc_h());
    else pt_to_anchor(w, nx, ny, nw, nh, g_WW, g_WH);
}

// Персист §14 (F2a2): пишем виджеты в ТОТ ЖЕ layout.json, что и доки (§11).// widget_json_save заменяет только свою секцию, доки не трогает — и наоборот
// (lay_save переносит "widgets"). Пишем на КОММИТ (конец драга/спавн), не на
// каждый шаг. В seed/bench/скриншот-режимах файл не пачкаем (герметичность).
// Куда персист виджетов пишет/читает. В бенчах/сид-режимах — НИКОГДА в документ
// пользователя: уводим в отдельный файл (герметичность тестов). UI_EDIT_PERSIST=1
// оставлен, чтобы мерить ЧЕСТНО (вместе с записью файла), но не по документу.
// Тест-ручки §14 (синтетический ввод/скрипты воспроизведения жалоб). Пока задана
// ЛЮБАЯ из них — документ юзера не читаем и не пишем: иначе прогон «а что если
// кликнуть сюда» тихо дописывает его раскладку (своими руками уже раз так сделал:
// 5 панелей из теста остались в его файле).
static int ed_test_knobs(void) {
    return getenv("UI_EDIT_BENCH") || getenv("UI_EDIT_SEED") || getenv("UI_EDIT_SCRIPT") ||
           getenv("UI_EDIT_DRAG") || getenv("UI_EDIT_HOLD") || getenv("UI_EDIT_CLICK") ||
           getenv("UI_EDIT_CLICK2") || getenv("UI_EDIT_TDRAG") || getenv("UI_EDIT_TDROP") ||
           getenv("UI_EDIT_RENAME") || getenv("UI_EDIT_SPAWN");
}
static const char *wt_path(void) {
    if (ed_test_knobs()) return "engine/assets/layout.bench.json";
    return UI_LAYOUT_PATH;
}
static int wt_persist_ok(void) {
    // UI_EDIT_PERSIST=1 — меряем ЧЕСТНО, с записью файла (бенч залпа спавнов).
    const char *fp = getenv("UI_EDIT_PERSIST");
    if (fp && fp[0] == '1') return 1;
    return !ed_test_knobs();
}
static void wt_persist(void) {
    if (g_editSelf) return; // хром редактора — вид инструмента, в layout.json не пишем
    if (wt_persist_ok()) widget_json_save(&g_wt, wt_path());
}

// --- §14 F4: история документа (undo/redo) ---
// Пишем ТОЛЬКО документ: хром редактора — вид инструмента (как и в layout.json),
// его правка в историю документа не попадает. Коммит — на КОНЕЦ операции.
static void undo_commit(const char *what) {
    if (g_editSelf) return;
    ui_wundo_commit(&g_undo, &g_wt, what);
}
// Ctrl+Z / Ctrl+Y (redo = +Shift). Возврат 0 = у виджетов откатывать нечего
// (тогда Z/Y уходят докам: у них своя история — клавиши не «съедаем»).
static int wt_undo_apply(int redo) {
    if (g_editSelf) return 0;
    int ok = redo ? ui_wundo_redo(&g_undo, &g_wt) : ui_wundo_undo(&g_undo, &g_wt);
    if (!ok) return 0;
    if (g_wtSel && !widget_cget(&g_wt, g_wtSel)) g_wtSel = 0; // выбранного могло не стать
    g_dragId = 0;
    g_dragHandle = 0;
    g_insField = -1;
    g_palHot = -1;
    edit_push();
    wt_persist();
    return 1;
}

// --- §14 F5a: ТЕКСТ — правка ИМЕНИ виджета (просьба тестера «невозможно
// переименовать»). Имя = и строка дерева, и подпись на холсте (у button/label),
// поэтому переименование = правка текста. Мини-текстбокс v1 (однострочный, как
// WPF TextBox/UMG EditableText): каретка, Left/Right/Home/End, Backspace/Delete
// ПО КАРЕТКЕ, Enter/Tab = коммит, Esc = отмена, клик мимо поля = коммит.
// Полноценный бокс (выделение, Ctrl+C/V, IME, прокрутка длинной строки, двойной
// клик по слову) — F5a2: точка замены = все name_edit_* ниже, состояние одно.
// Символы '"' '\\' '{' '}' '[' ']' не принимаем ВООБЩЕ (widget_add их тоже
// запрещает: имя пишется в JSON сырым текстом — иначе файл побьётся).
static void name_edit_redraw(void) {
    float hx, hy, hw, hh;
    ins_rect(-1, &hx, &hy, &hw, &hh);
    const int r[1][4] = {{(int)hx - 4, (int)hy - 4, (int)hw + 8, (int)hh + 8}};
    edit_push_dirtyN(r, 1); // только строка имени: полный кадр на букву — жирно
}
static int name_bad_char(char c) {
    return c == '"' || c == '\\' || c == '{' || c == '}' || c == '[' || c == ']';
}
static void name_edit_blink_reset(void) {
    g_caretOn = 1;
    g_nameBlink = 0;
}
// Выделение (anchor..caret) — как в любой текстовой правке: F2 выделяет имя
// целиком, поэтому первая же буква ЗАМЕНЯЕТ имя (UE/Unity/Blender), а не
// приклеивается в конец. Shift+стрелки тянут выделение, Ctrl+A — всё.
static int name_sel_lo(void) {
    return g_nameCaret < g_nameAnchor ? g_nameCaret : g_nameAnchor;
}
static int name_sel_hi(void) {
    return g_nameCaret < g_nameAnchor ? g_nameAnchor : g_nameCaret;
}
static int name_has_sel(void) {
    return g_nameCaret != g_nameAnchor;
}
static void name_sel_all(void) {
    g_nameAnchor = 0;
    g_nameCaret = g_nameLen;
}
static void name_del_sel(void) {
    if (!name_has_sel()) return;
    int lo = name_sel_lo(), hi = name_sel_hi();
    memmove(g_nameBuf + lo, g_nameBuf + hi, (size_t)(g_nameLen - hi) + 1);
    g_nameLen -= hi - lo;
    g_nameCaret = lo;
    g_nameAnchor = lo;
}
static void name_ins(char c) {
    if (name_has_sel()) name_del_sel();
    if (g_nameLen >= UI_WIDGET_NAME - 1) return;
    memmove(g_nameBuf + g_nameCaret + 1, g_nameBuf + g_nameCaret,
            (size_t)(g_nameLen - g_nameCaret) + 1);
    g_nameBuf[g_nameCaret++] = c;
    g_nameLen++;
    g_nameAnchor = g_nameCaret;
}
static void name_edit_end(int commit) {
    if (!g_nameEdit) return;
    g_nameEdit = 0;
    if (commit) {
        const UiWidget *cw = widget_cget(g_act, g_wtSel);
        if (cw && g_nameLen > 0 && strcmp(g_nameBuf, cw->name) != 0) {
            char nm[UI_WIDGET_NAME];
            snprintf(nm, sizeof nm, "%s", g_nameBuf);
            widget_name_unique(g_act, g_wtSel, nm); // занято -> суффикс (как UE)
            UiWidget *w = widget_get(g_act, g_wtSel);
            if (w && strcmp(nm, w->name) != 0) {
                ui_log_add("rename \"%s\" -> \"%s\"", w->name, nm); // Ш1c
                snprintf(w->name, sizeof w->name, "%s", nm);
                wt_persist();
                undo_commit("rename");
            }
        }
    }
    edit_push();
}
static void name_edit_begin(void) {
    if (g_editSelf) return; // текст правим в документе (хром — вид инструмента)
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    if (!w) return;
    g_nameEdit = 1;
    snprintf(g_nameBuf, sizeof g_nameBuf, "%s", w->name);
    g_nameLen = (int)strlen(g_nameBuf);
    g_nameCaret = g_nameLen;
    name_sel_all(); // F2/клик по имени: имя выделено целиком -> буква заменяет его
    name_edit_blink_reset();
    edit_push(); // поле в фокусе (каретка видна сразу)
}
static void name_edit_char(unsigned cp) {
    if (!g_nameEdit) return;
    if (cp < 32 || cp > 126) return; // атлас = printable ASCII
    char c = (char)cp;
    if (name_bad_char(c)) return;
    name_ins(c);
    name_edit_blink_reset();
    name_edit_redraw();
}
// 1 = клавиша ушла в поле (в приложение не летит, пока поле в фокусе).
static int name_edit_key(int key, int mods) {
    if (!g_nameEdit) return 0;
    int shift = (mods & GLFW_MOD_SHIFT) != 0;
    int ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    switch (key) {
    case GLFW_KEY_ESCAPE: name_edit_end(0); return 1;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
    case GLFW_KEY_TAB: name_edit_end(1); return 1;
    case GLFW_KEY_BACKSPACE:
        if (name_has_sel()) {
            name_del_sel();
        } else if (g_nameCaret > 0) {
            memmove(g_nameBuf + g_nameCaret - 1, g_nameBuf + g_nameCaret,
                    (size_t)(g_nameLen - g_nameCaret) + 1);
            g_nameCaret--;
            g_nameLen--;
        }
        g_nameAnchor = g_nameCaret;
        break;
    case GLFW_KEY_DELETE:
        if (name_has_sel()) {
            name_del_sel();
        } else if (g_nameCaret < g_nameLen) {
            memmove(g_nameBuf + g_nameCaret, g_nameBuf + g_nameCaret + 1,
                    (size_t)(g_nameLen - g_nameCaret));
            g_nameLen--;
        }
        g_nameAnchor = g_nameCaret;
        break;
    case GLFW_KEY_LEFT:
        if (g_nameCaret > 0) g_nameCaret--;
        if (!shift) g_nameAnchor = g_nameCaret;
        break;
    case GLFW_KEY_RIGHT:
        if (g_nameCaret < g_nameLen) g_nameCaret++;
        if (!shift) g_nameAnchor = g_nameCaret;
        break;
    case GLFW_KEY_HOME:
        g_nameCaret = 0;
        if (!shift) g_nameAnchor = 0;
        break;
    case GLFW_KEY_END:
        g_nameCaret = g_nameLen;
        if (!shift) g_nameAnchor = g_nameLen;
        break;
    case GLFW_KEY_A:
        if (ctrl) name_sel_all(); // Ctrl+A — выделить всё (как везде)
        break;
    default: return 1; // фокус: прочие клавиши не улетают в приложение
    }
    name_edit_blink_reset();
    name_edit_redraw();
    return 1;
}
// Клик по заголовку инспектора (имя выбранного) = начать правку.
static int ins_head_hit(float px, float py) {
    if (!g_wtSel || !ed_live(2)) return 0;
    float x, y, w, h;
    ins_rect(-1, &x, &y, &w, &h);
    return px >= x && px < x + w && py >= y && py < y + h;
}

// --- §14 F5d: ХЛЕБНЫЕ КРОШКИ — цепочка предков выбранного ---
// Тестер: «нажать на объект и видеть, В КОМ он лежит; переключиться на родителей».
// Ряд под заголовком дерева: Scene > Panel > Button. Клик по сегменту выбирает
// этого предка (как цепочка слоёв в Figma). Дети — в дереве ниже: иерархия всегда
// развёрнута, поэтому «перейти к детям» = клик по ребёнку.
static int g_pathN = 0;               // сегментов, [0] = Scene (id 0)
static int g_pathId[16];
static int g_pathFrom = 0;            // с какого сегмента рисуем (обрезка слева)
static float g_pathX[16], g_pathW[16]; // геометрия сегментов (текст И хит)
static void tree_path_rect(float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edTree, &px, &py, &pw, &ph);
    (void)ph;
    *x = px + PAL_PAD;
    *y = py + TREE_HDR;
    *w = pw - 2 * PAL_PAD - 4;
    *h = (float)TREE_PATH;
}
static void tree_path_build(void) {
    int chain[15];
    int n = 0;
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    while (w && n < 15) {
        chain[n++] = w->id;
        w = widget_cget(g_act, w->parent);
    }
    g_pathN = 0;
    g_pathId[g_pathN++] = 0; // Scene: клик = снять выбор
    for (int i = n - 1; i >= 0; i--) g_pathId[g_pathN++] = chain[i];
}
static const char *tree_path_label(int i, char *buf) {
    if (i <= 0) return "Scene";
    const UiWidget *w = widget_cget(g_act, g_pathId[i]);
    if (!w) return "?";
    snprintf(buf, UI_WIDGET_NAME, "%s", w->name);
    return buf;
}
static float tree_seg_w(const char *s) {
    return g_text ? ui_text_width(g_text, s) : (float)(8 * (int)strlen(s));
}
// Геометрию сегментов считает ОДНА функция — и текст, и хит (иначе подпись и
// клик разъезжаются). Не влезло в панель — обрезаем СЛЕВА (ближние предки
// важнее): выделенный сегмент виден всегда.
static void tree_path_layout(void) {
    tree_path_build();
    float rx, ry, rw, rh;
    tree_path_rect(&rx, &ry, &rw, &rh);
    float sep = tree_seg_w("/");
    float ell = tree_seg_w("...");
    // «Scene» — корень документа, он подразумевается: его выкидываем БЕЗ «...»,
    // поэтому цепочка предков влезает заметно чаще (тестер: «видеть, в ком лежит»).
    int s = 0;
    for (;;) {
        float tot = (s >= 2) ? ell + 6.0f : 0.0f;
        for (int i = s; i < g_pathN; i++) {
            char b[UI_WIDGET_NAME];
            tot += tree_seg_w(tree_path_label(i, b)) + (i > s ? sep : 0.0f);
        }
        if (tot <= rw || s >= g_pathN - 1) break;
        s++;
    }
    g_pathFrom = s;
    float cx = rx + ((s >= 2) ? ell + 6.0f : 0.0f);
    for (int i = s; i < g_pathN; i++) {
        char b[UI_WIDGET_NAME];
        const char *str = tree_path_label(i, b);
        if (i > s) cx += sep;
        g_pathX[i] = cx;
        g_pathW[i] = tree_seg_w(str);
        cx += g_pathW[i];
    }
}
// Возврат: id предка (0 = Scene/снять выбор) или -1 = мимо строки крошек.
static int tree_path_hit_at(float px, float py) {
    float x, y, w, h;
    tree_path_rect(&x, &y, &w, &h);
    if (px < x || px >= x + w || py < y || py >= y + h) return -1;
    tree_path_layout();
    for (int i = g_pathFrom; i < g_pathN; i++)
        if (px >= g_pathX[i] - 3 && px < g_pathX[i] + g_pathW[i] + 3) return g_pathId[i];
    return -1;
}

// --- §14 F5h: цель броска строки дерева ---
// mode: 0 = ВНУТРЬ (стать ребёнком), 1 = ПЕРЕД строкой, 2 = ПОСЛЕ. Возврат: id
// цели, 0 = «в корень» (брошено на крошку Scene), -1 = мимо (бросок отменяется).
static int tree_drop_at(float px, float py, int *mode) {
    float tx, ty, tw, th;
    tree_path_rect(&tx, &ty, &tw, &th);
    if (px >= tx - 2 && px < tx + tw + 2 && py >= ty && py < ty + th) {
        *mode = 0;
        return 0; // Scene = бросить в КОРЕНЬ (id 0)
    }
    float px0, py0, pw0, ph0;
    ed_rect(g_edTree, &px0, &py0, &pw0, &ph0);
    if (px < px0 || px >= px0 + pw0 || py < py0 || py >= py0 + ph0) return -1;
    tree_build();
    int rows = tree_rows_fit();
    for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
        float x, y, w, h;
        tree_rect(i - g_treeScroll, &x, &y, &w, &h);
        if (py < y || py >= y + h) continue;
        float rel = (py - y) / (h > 0 ? h : 1.0f); // треть сверху/снизу = порядок
        *mode = rel < 0.3f ? 1 : rel > 0.7f ? 2 : 0;
        return g_treeId[i];
    }
    return -1;
}
// Применить бросок: reparent ИЛИ порядок — одним вызовом модели (Move To).
static int tree_drop_apply(int drag, int tgt, int mode) {
    if (!drag || drag == tgt) return 0;
    if (tgt == 0) return widget_move_before(g_act, drag, 0, 0); // Scene: вынуть в корень
    if (mode == 0) return widget_move_before(g_act, drag, tgt, 0); // внутрь, в конец
    const UiWidget *tw = widget_cget(g_act, tgt);
    if (!tw) return 0;
    if (mode == 1) return widget_move_before(g_act, drag, tw->parent, tgt); // перед
    int sib[UI_MAX_WIDGETS];
    int n = wt_sib_sorted(tw->parent, sib);
    int next = 0;
    for (int i = 0; i < n; i++)
        if (sib[i] == tgt) {
            next = (i + 1 < n) ? sib[i + 1] : 0;
            break;
        }
    return widget_move_before(g_act, drag, tw->parent, next); // после
}

// Удаление выбранного (вместе с поддеревом). Выбор уходит на родителя — как в
// UE/Unity: после удаления «активен» контейнер, а не пустота. Хром редактора не
// удаляем: панели EdPalette/EdHierarchy/EdInspector — сам инструмент, не документ.
static int wt_delete_sel(void) {
    if (g_editSelf || !g_wtSel) return 0;
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    int par = w ? w->parent : 0;
    char delName[UI_WIDGET_NAME];
    snprintf(delName, sizeof delName, "%s", w ? w->name : "?");
    if (!widget_remove(g_act, g_wtSel)) return 0;
    ui_log_add("delete \"%s\"", delName); // Ш1c: след в журнале (w уже мертв)
    g_wtSel = widget_cget(g_act, par) ? par : 0;
    g_dragId = 0;    // удалили под курсором — драг снимаем
    g_dragHandle = 0;
    g_insField = -1; // и правку поля инспектора
    g_palHot = -1;
    edit_push();
    wt_persist(); // коммит: удаление
    undo_commit("delete");
    return 1;
}

// Событие в холст/палитру. 1 = наше (доки не трогаем). Спавн/выбор/драг — тут.
// §14 F5j: панели — как окна. Верхняя панель в точке (порядок g_edOrder, верхняя
// проверяется первой): она забирает клик и рисуется последней.
static int ed_panel_top(float px, float py) {
    for (int k = ed_ws_panels() - 1; k >= 0; k--) {
        int i = g_edOrder[k];
        if (!g_edOpen[i]) continue; // F5k: закрытый инструмент не ловит клик
        float x, y, w, h;
        ed_rect(g_edPanels[i], &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}
// §14 F5j: текст — отдельный проход ПОВЕРХ всех фигур. Без этой проверки подпись,
// накрытая панелью, ПРОСТУПАЕТ сквозь неё (панель = окно). own: 0/1/2 — своя
// панель (её текст виден, если она верхняя), -1 — документ/призрак.
static int ed_text_vis(int own, float x, float y) {
    int t = ed_panel_top(x, y);
    return t < 0 || t == own;
}
// Полоса ЗАГОЛОВКА верхней панели: за неё панель тащат/отрывают (как за таб окна).
static int ed_panel_head_at(float px, float py, int *which) {
    int i = ed_panel_top(px, py);
    if (i < 0) return 0;
    float x, y, w, h;
    ed_rect(g_edPanels[i], &x, &y, &w, &h);
    if (py > y + (float)PAL_TITLE) return 0;
    if (which) *which = i;
    return 1;
}
// Кнопка «в док» у ОТОРВАННОЙ панели — в правом конце её заголовка.
static void ed_float_chip(int i, float *cx, float *cy, float *cw, float *ch) {
    float x, y, w, h;
    ed_rect(g_edPanels[i], &x, &y, &w, &h);
    (void)h;
    *cw = 34.0f;
    *ch = 15.0f;
    *cx = x + w - PAL_PAD - *cw;
    *cy = y + 4.0f;
}
// Поднять панель наверх: и в порядке хита/отрисовки, и в z дерева хрома (его
// рисует общий рантайм — значит порядок корней = порядок картинки).
static void ed_panel_raise(int i) {
    int k = 0;
    while (k < ed_ws_panels() && g_edOrder[k] != i) k++;
    if (k >= ed_ws_panels() - 1) return; // уже верхняя
    for (; k < ed_ws_panels() - 1; k++) g_edOrder[k] = g_edOrder[k + 1];
    g_edOrder[ed_ws_panels() - 1] = i;
    widget_move_before(&g_edWt, g_edPanels[i], 0, 0); // в конец корней = верх
}
// Вернуть панель в свой док (снять «двинул юзер»).
static void ed_panel_dock(int i) {
    int id = g_edPanels[i];
    if (id > 0 && id <= UI_MAX_WIDGETS) g_edMoved[id - 1] = 0;
    g_edGrab = -1;
    ed_chrome_sync();
    edit_push();
}

// §14 F5k: полоса вкладок-инструментов. Вкладка = тумблер «инструмент открыт»
// (галочки View в Word / панели в Roblox Studio). Открытая подсвечена, и у неё
// снизу маркер — видно, какие инструменты сейчас есть на экране.
static void tabs_draw(RcVecShape *sh, int *n) {
    float bx, by, bw, bh;
    tabs_rect(&bx, &by, &bw, &bh);
    static const float bar[4] = {0.10f, 0.11f, 0.14f, 1.0f};
    static const float on[4] = {0.19f, 0.24f, 0.33f, 1.0f};
    static const float off[4] = {0.13f, 0.14f, 0.18f, 1.0f};
    static const float edge[4] = {0.26f, 0.29f, 0.36f, 1.0f};
    static const float mark[4] = {0.35f, 0.62f, 0.95f, 1.0f};
    wt_emit(sh, n, bx, by, bw, bh, bar, 0.0f);
    wt_emit(sh, n, bx, by + bh - 1.0f, bw, 1.0f, edge, 0.0f);
    for (int i = 0; i < ed_ws_tabs(); i++) {
        float x, y, w, h;
        tab_rect(i, &x, &y, &w, &h);
        wt_emit(sh, n, x, y, w, h, i == g_edTab ? on : off, 4.0f);
        if (i == g_edTab) wt_emit(sh, n, x + 2, y + h - 3, w - 4, 2.0f, mark, 1.0f);
    }
}

// §14 F5h2: панели хрома (палитра/дерево/инспектор) лежат ПОВЕРХ холста, значит
// клик по САМОЙ панели (полоса заголовка, пустое место под строками) не должен ни
// выбирать виджет под ней, ни ронять выбор. Тестер: клик по полосе «INSPECTOR»
// схлопывал все поля (выбор сбрасывался как на пустом холсте) и они «не
// возвращались», пока не выберешь виджет заново в дереве.
static int ed_chrome_covers(float px, float py) {
    { // F5k: полоса вкладок-инструментов — тоже хром (клик её, не холст)
        float bx, by, bw, bh;
        tabs_rect(&bx, &by, &bw, &bh);
        if (px >= bx && px < bx + bw && py >= by && py < by + bh) return 1;
    }
    { // F5l(2): полоса кнопок ленты — тоже хром
        float bx, by, bw, bh;
        ed_ribbon_band(&bx, &by, &bw, &bh);
        if (px >= bx && px < bx + bw && py >= by && py < by + bh) return 1;
    }
    if (g_edShowKeys && ed_keys_hit(px, py)) return 1; // F5l(2): шпаргалка = хром
    for (int i = 0; i < ed_ws_panels(); i++) {
        if (!g_edOpen[i]) continue;
        float x, y, w, h;
        ed_rect(g_edPanels[i], &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return 1;
    }
    return 0;
}

static int wt_event(const UiEvent *e) {
    if (e->kind == UI_EV_WHEEL) { // скролл списка над панелью дерева
        float px, py, pw, ph;
        ed_rect(g_edTree, &px, &py, &pw, &ph);
        if (e->x >= px && e->x < px + pw && e->y >= py && e->y < py + ph) {
            int step = e->value > 0 ? -3 : e->value < 0 ? 3 : 0;
            g_treeScroll += step;
            tree_build(); // клампы: по длине списка и видимости выбранного
            edit_push();
            return 1;
        }
        ed_rect(g_edCon, &px, &py, &pw, &ph); // Ш1c: над консолью крутится журнал
        if (e->x >= px && e->x < px + pw && e->y >= py && e->y < py + ph) {
            g_conScroll += e->value > 0 ? 3 : e->value < 0 ? -3 : 0;
            con_view(0, 0); // кламп прокрутки — одна точка правды (как tree_build)
            edit_push();
            return 1;
        }
        return 0;
    }
    if (e->kind == UI_EV_MOVE) {
        if (g_edGrab >= 0 && g_mouseDown) { // F5j: тащим ПАНЕЛЬ за заголовок
            UiWidget *p = widget_get(&g_edWt, g_edPanels[g_edGrab]);
            if (p) {
                p->rect.anchors = UI_A_LEFT | UI_A_TOP;
                p->rect.x = e->x - g_edGrabDX;
                p->rect.y = e->y - g_edGrabDY;
                ed_panels_clamp(); // F5k: панель не выезжает за окно ни одной стороной
                g_edMoved[g_edPanels[g_edGrab] - 1] = 1; // оторвана от дока
                edit_push(); // панель тащат редко: полный кадр честнее полос
            }
            return 1;
        }
        if (g_treeDrag) { // F5h: тянем строку — цель/индикатор броска под курсором
            g_treeDragX = e->x;
            g_treeDragY = e->y;
            g_treeDropId = tree_drop_at(e->x, e->y, &g_treeDropMode);
            edit_push(); // призрак + линия вставки (драг короткий — полный кадр)
            return 1;
        }
        if (g_treePress && g_mouseDown &&
            (e->y - g_treePressY > 5.0f || g_treePressY - e->y > 5.0f)) {
            g_treeDrag = g_treePress; // F5h: порог пройден — тащим ветку
            g_treeDragX = e->x;
            g_treeDragY = e->y;
            g_treeDropId = tree_drop_at(e->x, e->y, &g_treeDropMode);
            edit_push();
            return 1;
        }
        if (g_insField >= 0) { // F3b: scrub поля инспектора (правит АКТИВНОЕ дерево)
            UiWidget *w = widget_get(g_act, g_wtSel);
            float *slot = w ? ins_slot(w, g_insField) : 0;
            if (slot) {
                float v = g_insDownVal + (e->x - g_insDownX) * ins_step(g_insField);
                ins_clamp(g_insField, &v);
                // §14 F5l: x/y выбранного родителя тянет ПОДДЕРЕВО (та же грабля,
                // что в драге: иначе «виджет разваливается»). Считаем до правки.
                int fx = g_insField == IF_X, fy = g_insField == IF_Y;
                float obx = 0, oby = 0, obw = 0, obh = 0;
                int wk = 0;
                if ((fx || fy) && w) {
                    float b0, b1, b2, b3;
                    wk = wt_subtree_bounds(g_wtSel, &b0, &b1, &b2, &b3) > 1;
                    obx = b0;
                    oby = b1;
                    obw = b2;
                    obh = b3;
                }
                float oldv = *slot;
                *slot = v;
                if (wk) {
                    float d = v - oldv;
                    float dpx = fx ? ((w->rect.anchors & UI_A_RIGHT) ? -d : d) : 0.0f;
                    float dpy = fy ? ((w->rect.anchors & UI_A_BOTTOM) ? -d : d) : 0.0f;
                    wt_subtree_shift_kids(g_wtSel, dpx, dpy);
                }
                int now[4], u[4];
                ins_px(now);
                u[0] = g_insPrevPx[0] < now[0] ? g_insPrevPx[0] : now[0];
                u[1] = g_insPrevPx[1] < now[1] ? g_insPrevPx[1] : now[1];
                int rx = g_insPrevPx[0] + g_insPrevPx[2] > now[0] + now[2]
                             ? g_insPrevPx[0] + g_insPrevPx[2]
                             : now[0] + now[2];
                int ry = g_insPrevPx[1] + g_insPrevPx[3] > now[1] + now[3]
                             ? g_insPrevPx[1] + g_insPrevPx[3]
                             : now[1] + now[3];
                u[2] = rx - u[0];
                u[3] = ry - u[1];
                if (wk) { // группа: старые+новые границы поддерева тоже регион
                    float n0 = 0, n1 = 0, n2 = 0, n3 = 0;
                    wt_subtree_bounds(g_wtSel, &n0, &n1, &n2, &n3);
                    int ax = (int)((obx < n0 ? obx : n0) - 2.0f);
                    int ay = (int)((oby < n1 ? oby : n1) - 2.0f);
                    int ar = (int)((obx + obw > n0 + n2 ? obx + obw : n0 + n2) + 2.0f);
                    int ab = (int)((oby + obh > n1 + n3 ? oby + obh : n1 + n3) + 2.0f);
                    int ur = u[0] + u[2] > ar ? u[0] + u[2] : ar;
                    int ub = u[1] + u[3] > ab ? u[1] + u[3] : ab;
                    if (ax < u[0]) u[0] = ax;
                    if (ay < u[1]) u[1] = ay;
                    u[2] = ur - u[0];
                    u[3] = ub - u[1];
                }
                int one[1][4] = {{u[0], u[1], u[2], u[3]}};
                edit_push_dirtyN(one, 1); // растр только региона
                for (int k = 0; k < 4; k++) g_insPrevPx[k] = now[k];
            }
            return 1;
        }
        if (g_dragId) {
            wt_drag_apply(e->x, e->y);
            return 1;
        }
        int h = pal_hit(e->x, e->y);
        if (h != g_palHot) {
            g_palHot = h;
            return 1;
        }
        { // F5l(2): кнопка ленты под курсором -> тултип (метка + клавиша)
            int rs = -1;
            int hot = ed_ribbon_hit(e->x, e->y, &rs) ? g_rbAct[rs] : -1;
            if (hot != g_rbHot) {
                g_rbHot = hot;
                edit_push();
                return 1;
            }
        }
        return 0;
    }
    if (e->kind == UI_EV_UP) {
        if (g_edGrab >= 0) { // F5j: бросили панель — близко к дому = назад в док
            int i = g_edGrab;
            g_edGrab = -1;
            {
                float hx, hy, hw, hh, x, y, w, h;
                ed_panel_home(i, &hx, &hy, &hw, &hh);
                ed_rect(g_edPanels[i], &x, &y, &w, &h);
                float mx = x + w * 0.5f, my = y + (float)PAL_TITLE * 0.5f;
                if (mx > hx - 40.0f && mx < hx + hw + 40.0f && my > hy - 40.0f &&
                    my < hy + hh + 40.0f) {
                    ed_panel_dock(i); // бросили на своё док-место
                    return 1;
                }
            }
            edit_push();
            return 1;
        }
        if (g_treeDrag) { // F5h: БРОСОК ветки — reparent/порядок одним действием
            int drag = g_treeDrag, tgt = g_treeDropId, mode = g_treeDropMode;
            g_treeDrag = 0;
            g_treePress = 0;
            g_treeDropId = -1;
            const UiWidget *dw = widget_cget(g_act, drag);
            int oldp = dw ? dw->parent : 0;
            if (tgt >= 0 && drag && drag != tgt && tree_drop_apply(drag, tgt, mode)) {
                const UiWidget *nw = widget_cget(g_act, drag);
                g_wtSel = drag; // тащим — значит выбрали его же
                wt_persist();
                undo_commit((nw ? nw->parent : 0) != oldp ? "reparent" : "reorder");
            }
            edit_push();
            return 1;
        }
        g_treePress = 0; // клик без движения = просто выбор
        if (g_insField >= 0) { // F3b: конец scrub
            g_insField = -1;
            wt_persist();
            undo_commit("field");
            edit_push(); // снять подсветку ряда
            return 1;
        }
        if (g_dragId) {
            int wasResize = g_dragHandle > 0;
            g_dragId = 0;
            g_dragHandle = 0;
            wt_persist(); // коммит: конец перемещения/ресайза
            undo_commit(wasResize ? "resize" : "move");
            return 1;
        }
        return 0;
    }
    if (e->kind != UI_EV_DOWN) return 0;
    g_treePress = 0; // F5h: пресс живёт только до этого DOWN (иначе залипнет)
    // F5a: клик ВНЕ поля имени = коммит правки текста (как в любом редакторе),
    // после чего клик обрабатывается как обычно (выбор/драг по месту).
    if (g_nameEdit && !ins_head_hit(e->x, e->y)) name_edit_end(1);
    // §14 F3c: правим ХРОМ редактора — палитра/дерево/инспектор не работают
    // (это инструмент документа), клик/драг идёт по панелям хрома.
    if (g_editSelf) {
        int sh = wt_handle_hit(e->x, e->y);
        if (sh > 0 && g_wtSel) {
            wt_drag_begin(g_wtSel, sh, e->x, e->y);
            return 1;
        }
        int cid = wt_hit(e->x, e->y);
        if (cid) {
            g_wtSel = cid;
            wt_drag_begin(cid, 0, e->x, e->y);
            return 1;
        }
        if (e->x >= g_vpx && e->x < g_vpx + g_vpw && e->y >= g_vpy &&
            e->y < g_vpy + g_vph) {
            g_wtSel = 0;
            return 1;
        }
        return 0;
    }
    int topP = ed_panel_top(e->x, e->y); // F5j: клик берёт ВЕРХНЯЯ панель
    if (g_edShowKeys && ed_keys_hit(e->x, e->y)) { // F5l(2): шпаргалка поверх всего
        g_edShowKeys = 0;
        edit_push();
        return 1;
    }
    { // F5l(2): ЗАГОЛОВКИ ЛЕНТ — переключение активной ленты (не тумблер панели)
        int tw = -1;
        if (topP < 0 && tabs_hit(e->x, e->y, &tw)) {
            if (g_edTab != tw) {
                g_edTab = tw;
                ed_panels_save();
                edit_push();
            }
            return 1;
        }
    }
    { // F5l(2): КНОПКА ЛЕНТЫ = действие из реестра (spawn/тумблер/команда)
        int rs = -1;
        if (topP < 0 && ed_ribbon_hit(e->x, e->y, &rs)) {
            ed_cmd_run(g_rbAct[rs]);
            return 1; // клик по ленте холст не трогает (как у панелей)
        }
    }
    { // F5j: заголовок панели = таск/отрыв; у оторванной — кнопка «в док»
        int pw = -1;
        if (ed_panel_head_at(e->x, e->y, &pw)) {
            ed_panel_raise(pw); // взялся за неё — она наверх (как окно в WM)
            if (pw == 3 && con_clear_hit(e->x, e->y)) { // Ш1c: Clear в шапке консоли
                ed_cmd_run(ed_cmd_find("console.clear"));
                return 1;
            }
            if (ed_panel_float(pw)) {
                float cx, cy, cw, ch;
                ed_float_chip(pw, &cx, &cy, &cw, &ch);
                if (e->x >= cx && e->x < cx + cw && e->y >= cy && e->y < cy + ch) {
                    ed_panel_dock(pw); // «dock»: назад в док
                    return 1;
                }
            }
            float px, py, pwd, phd;
            ed_rect(g_edPanels[pw], &px, &py, &pwd, &phd);
            g_edGrab = pw;
            g_edGrabDX = e->x - px;
            g_edGrabDY = e->y - py;
            edit_push(); // панель поднялась
            return 1;
        }
    }
    int pi = ed_panel_open(0) ? pal_hit(e->x, e->y) : -1;
    if (pi >= 0 && topP == 0) { // палитра = ВИД реестра: строка -> запись реестра
        int ai = ed_pal_act(pi);
        if (ai >= 0) ed_cmd_run(ai); // создание/действие — ТОТ ЖЕ путь, что у ленты
        return 1;
    }
    tree_build();
    int pth = (topP == 1 && ed_panel_open(1)) ? tree_path_hit_at(e->x, e->y) : -1;
    if (pth >= 0) { // F5d: клик по крошке = выбрать ПРЕДКА (Scene = снять выбор)
        g_wtSel = pth;
        return 1;
    }
    int ti = (topP == 1 && ed_panel_open(1)) ? tree_hit(e->x, e->y) : 0;
    if (ti) { // F3: выбор виджета из дерева
        g_wtSel = ti;
        g_treePress = ti; // F5h: если мышь поедет — начнётся драг ветки
        g_treePressY = e->y;
        return 1;
    }
    if (topP == 2 && ed_panel_open(2) && ins_head_hit(e->x, e->y)) { // F5a: поле текста
        ed_doc_rename_sel(); // вид зовёт КОМАНДУ, а не функцию режима
        return 1;
    }
    int ii = (topP == 2 && ed_panel_open(2)) ? ins_hit(e->x, e->y) : -1;
    if (ii >= 0) { // F3b: инспектор
        if (ii == IF_UP || ii == IF_DOWN) {
            if (widget_layer(g_act, g_wtSel, ii == IF_UP ? 1 : -1)) {
                wt_persist();
                undo_commit("layer");
                edit_push(); // порядок слоёв изменился — полный кадр
            }
        } else {
            UiWidget *w = widget_get(g_act, g_wtSel);
            float *slot = w ? ins_slot(w, ii) : 0;
            if (slot) {
                g_insField = ii;
                g_insDownX = e->x;
                g_insDownVal = *slot;
                ins_px(g_insPrevPx);
                edit_push(); // подсветка ряда
            }
        }
        return 1;
    }
    if (ed_chrome_covers(e->x, e->y)) return 1; // панель РИСУЕТСЯ выше всего: клик её
    int hb = wt_handle_hit(e->x, e->y);
    if (hb > 0 && g_wtSel) { // ручки выбранного — ресайз
        wt_drag_begin(g_wtSel, hb, e->x, e->y);
        return 1;
    }
    // F5d: Ctrl+клик — выбрать СЛЕДУЮЩЕГО НИЖЕ в стопке (Figma deep select):
    // иначе верхний виджет «загораживает» нижнего и его не достать мышью.
    int deep = (e->mods & GLFW_MOD_CONTROL) != 0;
    int id = deep ? wt_pick_deep(e->x, e->y, g_wtSel) : wt_hit(e->x, e->y);
    if (id) { // выбор + сразу готов к драгу (deep — только выбор, без драга)
        g_wtSel = id;
        if (!deep) wt_drag_begin(id, 0, e->x, e->y);
        return 1;
    }
    int inVp = e->x >= g_vpx && e->x < g_vpx + g_vpw && e->y >= g_vpy &&
               e->y < g_vpy + g_vph;
    if (inVp) {
        g_wtSel = 0;
        return 1;
    }
    return 0; // доки/меню
}

// Заливка панелей в vec-ядро (полная, без гизмо/dirty-rect: стрелок нет,
// панели дёшевы). Плейсхолдер вьюпорта — тёмной плашкой поверх пустоты.
// Собрать кадр примитивов (панели -> плейсхолдер -> виджеты -> палитра).
static int build_shapes(RcVecShape *shapes) {
    int n = 0;
    ed_viewport_compute(); // F5l(4): доки решают, где экран игры (до всего)
    // §14 F5l(3): вокруг экрана игры — фон ОКНА РЕДАКТОРА. Раньше там была
    // чёрная пустота, и это читалось как поломка («инструменты во вьюпорте»).
    {
        static const float chromeCol[4] = {0.10f, 0.11f, 0.14f, 1.0f};
        wt_emit(shapes, &n, 0.0f, 0.0f, (float)g_WW, (float)g_WH, chromeCol, 0.0f);
    }
    // Легаси-хром (menu/toolbar/collapse/hierarchy/status из ui_panels) в окно
    // редактора НЕ собираем: он был ВТОРЫМ способом задать те же Edit/View и тот же
    // режим инструмента (тестер: «оставил легаси»). Вьюпорт считают НАШИ доки.
    if (n < UI_MAX_PRIMS && g_vpw > 0 && g_vph > 0) {
        // Плашка вместо 3D (игровой редактор подставит сюда frame_to).
        static const float voidCol[4] = {0.03f, 0.04f, 0.07f, 1.0f};
        shapes[n].kind = 0;
        shapes[n].a = (float)g_vpx;
        shapes[n].b = (float)g_vpy;
        shapes[n].c = (float)g_vpw;
        shapes[n].d = (float)g_vph;
        shapes[n].e = shapes[n].f = 0;
        memcpy(shapes[n].rgba, voidCol, sizeof shapes[n].rgba);
        n++;
    }
    ed_chrome_sync(); // §14 F3c: раскладка панелей хрома (док мог поменяться)
    wt_draw_all(shapes, &n); // документ — тем же рантаймом, что и игра
    // F5j: рамка/ручки активного — над документом, но ПОД панелями: панель это
    // окно, она перекрывает и холст, и рамку (что видно, то и кликается).
    wt_draw_sel(shapes, &n);
    // F5j: ПАНЕЛИ — окна поверх всего. Порядок внутри: полоса вкладок (она под
    // панелью, если панель на неё затащили) -> фон панели -> полосы заголовков
    // (и рамка/кнопка оторванной) -> содержимое панели (ряды).
    tabs_draw(shapes, &n); // F5k: заголовки лент (F5l(2))
    ed_ribbon_draw(shapes, &n); // F5l(2): кнопки активной ленты (реестр действий)
    chrome_draw(shapes, &n);
    chrome_heads_draw(shapes, &n);
    split_draw(shapes, &n); // F5l(4b): линии сплиттеров доков
    con_draw(shapes, &n);    // Ш1c: кнопка Clear в шапке консоли
    tree_draw(shapes, &n);   // F3: дерево
    ins_draw(shapes, &n);    // F3b: инспектор (справа)
    pal_draw(shapes, &n);
    // F5l(2): тултип и шпаргалка — ПОВЕРХ окон панелей (иначе подсказка тонула бы
    // под доком, как текст под панелью в F5l(5)).
    ed_ribbon_top_draw(shapes, &n);
    return n;
}

// UI_EDIT_SHAPES=1: печать всех фигур кадра (диагноз «полосы/зебра»: ищем фигуру,
// которая вылезла за вьюпорт). Печатает только подозрительные при =1, все при =2.
static void shapes_dump(const RcVecShape *s, int n) {
    const char *e = getenv("UI_EDIT_SHAPES");
    if (!e || !e[0]) return;
    int all = e[0] >= '2';
    printf("ui_edit: shapes n=%d vp=%d,%d %dx%d win=%dx%d\n", n, g_vpx, g_vpy, g_vpw, g_vph,
           g_WW, g_WH);
    for (int i = 0; i < n; i++) {
        float x = s[i].a, y = s[i].b, w = s[i].c, h = s[i].d;
        int bad = (y < (float)g_vpy - 0.5f && y + h > (float)g_vpy - 0.5f) || x < 0.0f ||
                  y < 0.0f || x + w > (float)g_WW + 0.5f || y + h > (float)g_WH + 0.5f ||
                  w > (float)g_vpw + 0.5f;
        if (all || bad)
            printf("  [%d] kind=%d x=%.1f y=%.1f w=%.1f h=%.1f %s a=%.2f\n", i, s[i].kind, x,
                   y, w, h, bad ? "LEAK" : "ok", (double)s[i].rgba[3]);
    }
}
static void edit_push(void) {
    if (!g_vec) return; // selftest: без GL
    RcVecShape shapes[UI_MAX_PRIMS];
    int n = build_shapes(shapes);
    g_fullN++;
    shapes_dump(shapes, n);
    if (n && g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
}

// Частичная перерисовка (dirty-rect, как в игровом редакторе): растр только
// региона (x,y,w,h, y-вниз), остальное не трогаем. Это и есть лечение лага.
static void edit_push_dirtyN(const int (*rects)[4], int cnt) {
    if (!g_vec) return; // selftest: без GL
    if (cnt <= 0) return;
    RcVecShape shapes[UI_MAX_PRIMS];
    int n = build_shapes(shapes); // один сбор кадра на все регионы
    if (!n) return;
    if (!g_vec->upload_vector_rect) {
        if (g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
        return;
    }
    for (int i = 0; i < cnt; i++) {
        int x = rects[i][0], y = rects[i][1], w = rects[i][2], h = rects[i][3];
        if (w <= 0 || h <= 0) continue;
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > g_WW) w = g_WW - x;
        if (y + h > g_WH) h = g_WH - y;
        if (w <= 0 || h <= 0) continue;
        g_vec->upload_vector_rect(g_vec, shapes, n, x, y, w, h);
    }
}

// §14 F5l(4b): событие СПЛИТТЕРА ДОКОВ — самый внешний слой. Съеденное не
// уходит ни виджетам, ни документу (иначе клик по краю дока ещё и выбирал бы
// виджет под ним). Сброс/персист — на UP (конец операции, как везде).
static int ed_split_event(const UiEvent *e) {
    if (e->kind == UI_EV_DOWN) {
        int which = 0;
        if (!split_hit(e->x, e->y, &which)) return 0;
        g_split = which;
        g_splitX0 = (double)e->x;
        g_splitY0 = (double)e->y;
        g_splitW0 = (which == 1) ? g_dockWl : (which == 2) ? g_dockWr : g_dockH;
        return 1;
    }
    if (e->kind == UI_EV_MOVE) {
        if (!g_split) return 0;
        if (!g_mouseDown) { // UP потерялся (окно ушло из фокуса) — не «липнем»
            g_split = 0;
            return 0;
        }
        // Знак: левый док растёт вправо (+dx); правый и нижний — «вверх» (-d).
        // У нижнего тянем ВЫСОТУ, значит дельта по Y (правый — по X).
        float d = (g_split == 3) ? (float)((double)e->y - g_splitY0)
                                 : (float)((double)e->x - g_splitX0);
        ed_dock_set(g_split, g_splitW0 + (int)((g_split == 2 || g_split == 3) ? -d : d));
        ed_viewport_compute();
        return 1;
    }
    if (e->kind == UI_EV_UP) {
        if (!g_split) return 0;
        g_split = 0;
        ed_panels_save(); // ширина доков — состояние ИНСТРУМЕНТА (конец операции)
        return 1;
    }
    return 0;
}

static void route_event(const UiEvent *e) {
    // Порядок §14: виджеты/палитра/лента -> доки. DOWN/UP, съеденный холстом,
    // в доки не идёт; MOVE форвардим обоим (ховер и там, и там).
    // Легаси-панели событий больше НЕ получают: в окне их нет, а их обработчик
    // (Tab-коллапс, меню, клавиши W/E/R) — СКРЫТЫЙ ДУБЛЬ ленты, из-за которого
    // «убранное» легаси продолжало работать за спиной. UiPanels остаётся только
    // как состояние режима инструмента сцены (ui_panels_tool/set_tool).
    if (ed_split_event(e)) { // F5l(4b): сплиттеры доков — до всего остального
        edit_push();
        return;
    }
    g_part = 0;
    int mine = wt_event(e);
    if (mine && e->kind != UI_EV_MOVE) {
        edit_push();
        return;
    }
    if (g_part && !g_forceFull && !g_noDirty && !g_dirtyOver) { // шаг драга: только регионы
        edit_push_dirtyN(g_dirtyR, g_dirtyN);
        return;
    }
    if (mine) edit_push();
}

// Коалесинг ввода (как UE/Unity): реальная мышь шлёт десятки MOVE-событий за
// кадр, а каждый MOVE у нас = отдельная частичная заливка. Копим события и
// применяем РАЗ ЗА КАДР; подряд идущие MOVE схлопываем в последнюю позицию.
// Без этого лаг зависит от частоты опроса мыши (синтетика = 1 событие/кадр).
#define UI_EVQ 1024
static UiEvent g_evq[UI_EVQ];
static int g_evn = 0;
static void push_ev(const UiEvent *e) {
    if (e->kind == UI_EV_MOVE && g_evn > 0 && g_evq[g_evn - 1].kind == UI_EV_MOVE) {
        g_evq[g_evn - 1] = *e; // схлопнуть подряд идущие MOVE
        return;
    }
    if (g_evn < UI_EVQ) {
        g_evq[g_evn++] = *e;
    } else if (e->kind == UI_EV_MOVE) {
        g_evq[UI_EVQ - 1] = *e; // переполнение: последнюю позицию не теряем
    }
}
static void drain_ev(void) {
    for (int i = 0; i < g_evn; i++) route_event(&g_evq[i]);
    g_evn = 0;
}

static void on_mouse(GLFWwindow *win, int button, int action, int mods) {
    (void)win;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.button = 0;
    e.mods = mods; // F5d: Ctrl+клик = «сквозь» верхний виджет
    e.x = (float)g_mx;
    e.y = (float)g_my;
    if (action == GLFW_PRESS) {
        g_mouseDown = 1; // F5h: драг строки дерева начинается только при ЛКМ
        e.kind = UI_EV_DOWN;
        push_ev(&e);
    } else if (action == GLFW_RELEASE) {
        g_mouseDown = 0;
        e.kind = UI_EV_UP;
        push_ev(&e);
    }
}

static void on_scroll(GLFWwindow *win, double dx, double dy) {
    (void)win;
    (void)dx;
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.kind = UI_EV_WHEEL;
    e.x = (float)g_mx;
    e.y = (float)g_my;
    e.value = (float)dy;
    push_ev(&e);
}

static void on_move(GLFWwindow *win, double x, double y) {
    g_mx = x * g_mouseKx;
    g_my = y * g_mouseKy;
    {
        static int lastHov = -1;
        int hov = wt_handle_hit((float)g_mx, (float)g_my) > 0 ? 1 : 0; // ручка = resize
        if (hov != lastHov) {
            lastHov = hov;
            if (g_curArrow && g_curResize) glfwSetCursor(win, hov ? g_curResize : g_curArrow);
        }
    }
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.kind = UI_EV_MOVE;
    e.x = (float)g_mx;
    e.y = (float)g_my;
    push_ev(&e);
}

// §14 F3c: переключить, ЧТО правим — документ или собственный UI редактора.
static void ed_self_toggle(int on) {
    if (g_nameEdit) name_edit_end(0); // режим меняется — правку текста бросаем
    g_editSelf = on ? 1 : 0;
    g_act = g_editSelf ? &g_edWt : &g_wt;
    g_wtSel = 0;
    g_insField = -1;
    g_dragId = 0;
    g_dragHandle = 0;
    g_palHot = -1;
    edit_push();
    printf("ui_edit: self=%d (правим %s)\n", g_editSelf,
           g_editSelf ? "ХРОМ редактора" : "документ (layout.json)");
}

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    if (action != GLFW_PRESS) return;
    // F5a: поле имени в фокусе — клавиша идёт ЕМУ (иначе Del удалял бы виджет,
    // Esc выходил из программы и т.п.). Поле забирает клавишу целиком.
    if (g_nameEdit && name_edit_key(key, mods)) return;
    if (key == GLFW_KEY_ESCAPE) {
        if (g_edGrab >= 0) { // F5j: Esc = бросить панель там, где она есть
            g_edGrab = -1;
            edit_push();
            return;
        }
        if (g_treeDrag || g_treePress) { // F5h: Esc = отмена броска ветки
            g_treeDrag = 0;
            g_treePress = 0;
            g_treeDropId = -1;
            edit_push();
            return;
        }
        if (g_edShowKeys) { // F5l(2): Esc закрывает шпаргалку (как модальное окно)
            g_edShowKeys = 0;
            edit_push();
            return;
        }
        g_quit = 1;
        return;
    }
    // §14 F5l(2): КЛАВИШИ — ИЗ РЕЕСТРА ДЕЙСТВИЙ. Одна таблица на ленту, шпаргалку
    // и горячие клавиши (как FInputBindingManager во UE): второго списка, который
    // мог разойтись с лентой, больше нет.
    if (key == GLFW_KEY_BACKSPACE) { // алиас Del
        int ai = ed_cmd_find("edit.delete");
        if (ai >= 0) {
            ed_cmd_run(ai);
            return;
        }
    }
    {
        int ai = ed_cmd_by_key(mods, key);
        if (ai >= 0) {
            // undo/redo делят историю с доками: не взялось — клавиша идёт дальше.
            int passthru = ED_ACT[ai].kind == ED_K_CMD &&
                           (ED_ACT[ai].arg == ED_CMD_UNDO || ED_ACT[ai].arg == ED_CMD_REDO);
            if (ed_cmd_run(ai)) return;
            if (!passthru) return; // выключено, но клавиша принадлежит редактору
        }
    }
    // Легаси-панелям клавиши больше не форвардим (их в окне нет): Tab (коллапс
    // доков) — легаси-модель, у нас панели прячутся галочками VIEW; Q/W/E/R —
    // инструменты сцены, они теперь ИЗ РЕЕСТРА (лента MODEL), а не из панелей.
}

// F5a: символы (раскладка/шифт/юникод — GLFW отдаёт codepoint). Иначе буквы не
// набрать: key-callback даёт КЛАВИШУ (GLFW_KEY_A), а не текст.
static void on_char(GLFWwindow *win, unsigned cp) {
    (void)win;
    if (!g_nameEdit) return;
    name_edit_char(cp);
}

// §14 F5l(5): содержимое панели обязано лежать ВНУТРИ её прямоугольника. Закрытая
// панель имеет rect 0x0 — содержимое из неё рисуется в угол (0,0) («зебры»). Тест
// селфтеста проверяет обе стороны инварианта этой функцией.
static int panel_content_inside(const RcVecShape *s, int n, int panelId) {
    float bx, by, bw, bh;
    ed_rect(panelId, &bx, &by, &bw, &bh);
    for (int i = 0; i < n; i++)
        if (s[i].a < bx - 2.0f || s[i].b < by - 2.0f || s[i].a + s[i].c > bx + bw + 2.0f ||
            s[i].b + s[i].d > by + bh + 2.0f)
            return 0;
    return 1;
}

// §14 F5l(2): selftest — клик по кнопке ленты по её id. Кнопки живут на АКТИВНОЙ
// ленте, поэтому сначала переключаем ленту (как юзер заголовком), потом кликаем
// в центр её прямоугольника (ту же геометрию, что рисует и хит-тест).
static int sel_clip_click(const char *id) {
    int ai = ed_cmd_find(id);
    if (ai < 0) return 0;
    g_edTab = ED_ACT[ai].tab;
    float x, y, w, h;
    ed_ribbon_item_rect(ai, &x, &y, &w, &h);
    if (w <= 0) return 0;
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.kind = UI_EV_DOWN;
    e.x = x + w * 0.5f;
    e.y = y + h * 0.5f;
    return wt_event(&e);
}

// §14 F5l(2b): клик по СТРОКЕ ПАЛИТРЫ — тем же путём, что у пользователя
// (событие DOWN в строку). Нужен для команд, которые живут в палитре и в ленте НЕ
// рисуются (правило «одна кнопка на действие»).
static int sel_pal_click(const char *id) {
    int ai = ed_cmd_find(id);
    if (ai < 0) return 0;
    for (int row = 0; row < ed_pal_n(); row++) {
        if (ed_pal_act(row) != ai) continue;
        float x, y, w, h;
        pal_rect(row, &x, &y, &w, &h);
        if (w <= 0 || h <= 0) return 0;
        UiEvent e;
        memset(&e, 0, sizeof e);
        e.kind = UI_EV_DOWN;
        e.x = x + 4;
        e.y = y + 4;
        route_event(&e);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--selftest")) {
        g_selftest = 1; // файлы состояния инструментов юзера не трогаем
        // Без окна/GL: панели в EDIT строят примитивы, вьюпорт — остаток,
        // скриптованный сплиттер двигает hierW. Файлов не пачкаем (remove).
        int bad = 0;
        // Герметичность: чужую раскладку (layout.json) не читаем и не сносим —
        // прячем на время теста, возвращаем как было (иначе тест врёт и рушит
        // рабочую раскладку пользователя).
        // Упавший прогон оставляет бэкап: сначала ВОЗВРАЩАЕМ его, иначе следующий
        // прогон перезапишет бэкап своим «пустым» layout.json — файл потерян.
        if (access("engine/assets/layout.json.selftest-bak", F_OK) == 0) {
            rename("engine/assets/layout.json.selftest-bak", "engine/assets/layout.json");
            printf("ui_edit: selftest вернул бэкап раскладки (прошлый прогон упал)\n");
        }
        int laySaved = (rename("engine/assets/layout.json",
                               "engine/assets/layout.json.selftest-bak") == 0);
        ScWorld sw;
        sc_init(&sw);
        Entity a = sc_spawn(&sw, "A");
        Entity b = sc_spawn(&sw, "B");
        (void)a;
        (void)b;
        UiPanels *pn = ui_panels_create();
        const UiFrame *pf = 0;
        UiEvent pe;
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_KEY;
        pe.key = 'L';
        if (!ui_panels_event(pn, &sw, &pe).handled || !ui_panels_edit(pn)) {
            printf("SELF FAIL: edit on\n");
            bad = 1;
        }
        if (!ui_panels_build(pn, &sw, W, H, &pf) || !pf || pf->n < 4) {
            printf("SELF FAIL: build\n");
            bad = 1;
        }
        int x, y, w, h;
        ui_panels_viewport(pn, W, H, &x, &y, &w, &h);
        // Остаток после доков (дефолт 220/220; файл может менять — считаем от него).
        if (y != 58 || h != H - 58 - 26 || x + w > W) {
            printf("SELF FAIL: viewport %d %d %d %d\n", x, y, w, h);
            bad = 1;
        }
        int x0 = x;
        // Сплиттер: DOWN на левом краю + MOVE +40 => hierW растёт на 40.
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_DOWN;
        pe.x = (float)(x0 - 3);
        pe.y = 300;
        if (!ui_panels_event(pn, &sw, &pe).handled) {
            printf("SELF FAIL: splitter grab\n");
            bad = 1;
        }
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_MOVE;
        pe.x = (float)(x0 - 3 + 40);
        pe.y = 300;
        ui_panels_event(pn, &sw, &pe);
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_UP;
        ui_panels_event(pn, &sw, &pe);
        ui_panels_viewport(pn, W, H, &x, &y, &w, &h);
        if (x != x0 + 40) {
            printf("SELF FAIL: splitter drag %d want %d\n", x, x0 + 40);
            bad = 1;
        }
        // §14 F2a: палитра + холст (создание/выбор) без окна.
        g_WW = W;
        g_WH = H;
        g_vpx = x;
        g_vpy = y;
        g_vpw = w;
        g_vph = h;
        ed_chrome_init(); // §14 F3c(1b): хром = виджеты; геометрия от вьюпорта
        ed_chrome_sync();
        {
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_palHot = -1;
            memset(g_wtN, 0, sizeof g_wtN);
            UiEvent we;
            float px, py, pw, ph;
            // DOWN по палитре (Panel) -> виджет создан и выбран.
            pal_rect(0, &px, &py, &pw, &ph);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = px + 5;
            we.y = py + 5;
            if (!wt_event(&we) || g_wt.count != 1 || !g_wtSel ||
                widget_cget(&g_wt, g_wtSel)->type != UI_W_PANEL) {
                printf("SELF FAIL: pal-spawn\n");
                bad = 1;
            }
            int p1 = g_wtSel;
            // Клик по телу виджета -> он же выбран (не сбрасывается).
            const UiWidget *W1 = widget_cget(&g_wt, p1);
            float bx, by, bw, bh;
            doc_px(W1, &bx, &by, &bw, &bh); // F5l(3): мышь — в координатах экрана
            g_wtSel = 0; // сброс, чтобы проверить именно выбор кликом
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = bx + bw / 2;
            we.y = by + bh / 2;
            if (!wt_event(&we) || g_wtSel != p1) {
                printf("SELF FAIL: wt-select\n");
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we); // отпускание: клик, а не вечный драг
            // Палитра Button -> второй виджет, тип верный.
            pal_rect(1, &px, &py, &pw, &ph);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = px + 5;
            we.y = py + 5;
            wt_event(&we);
            if (g_wt.count != 2 || !widget_cget(&g_wt, g_wtSel) ||
                widget_cget(&g_wt, g_wtSel)->type != UI_W_BUTTON ||
                widget_cget(&g_wt, p1)->type != UI_W_PANEL) {
                printf("SELF FAIL: pal-spawn2\n");
                bad = 1;
            }
            // Пустой клик по холсту -> снятие выбора (наш, не доки).
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = (float)(g_vpx + g_vpw - 5);
            we.y = (float)(g_vpy + g_vph - 5);
            if (!wt_event(&we) || g_wtSel != 0) {
                printf("SELF FAIL: wt-deselect\n");
                bad = 1;
            }
            // Клик по доку (вне вьюпорта) -> не наш (доки не крадём).
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = 2;
            we.y = (float)(g_WH / 2);
            if (wt_event(&we)) {
                printf("SELF FAIL: wt-dock-steal\n");
                bad = 1;
            }
            // Ховер палитры: MOVE по кнопке -> подсветка меняется (перерисовка).
            pal_rect(2, &px, &py, &pw, &ph);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = px + 5;
            we.y = py + 5;
            if (!wt_event(&we) || g_palHot != 2) {
                printf("SELF FAIL: pal-hover\n");
                bad = 1;
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F2b: драг перемещения + ресайз (frozen-frame: якорь на DOWN).
        {
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_palHot = -1;
            g_dragId = 0;
            g_dragHandle = 0;
            memset(g_wtN, 0, sizeof g_wtN);
            int id = wt_spawn(UI_W_BUTTON);
            float x0, y0, w0, h0;
            doc_px(widget_cget(&g_wt, id), &x0, &y0, &w0, &h0); // F5l(3): экран
            UiEvent we;
            // DOWN в теле -> move, MOVE +50/+30 -> сдвиг ровно на это, размер тот же.
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = x0 + w0 / 2;
            we.y = y0 + h0 / 2;
            wt_event(&we);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = x0 + w0 / 2 + 50;
            we.y = y0 + h0 / 2 + 30;
            wt_event(&we);
            float x1, y1, w1, h1;
            doc_px(widget_cget(&g_wt, id), &x1, &y1, &w1, &h1); // F5l(3): экран
            if (x1 != x0 + 50 || y1 != y0 + 30 || w1 != w0 || h1 != h0) {
                printf("SELF FAIL: wt-move %f %f %f %f\n", x1, y1, w1, h1);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            if (g_dragId) {
                printf("SELF FAIL: wt-up\n");
                bad = 1;
            }
            // Ресайз правым краем (хэндл R): +40 -> ширина +40, позиция та же.
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = x1 + w1 - 1;
            we.y = y1 + h1 / 2;
            wt_event(&we);
            if (!g_dragId || g_dragHandle != 2) {
                printf("SELF FAIL: wt-handle-r %d\n", g_dragHandle);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = x1 + w1 - 1 + 40;
            we.y = y1 + h1 / 2;
            wt_event(&we);
            float x2, y2, w2, h2;
            doc_px(widget_cget(&g_wt, id), &x2, &y2, &w2, &h2); // F5l(3): экран
            if (x2 != x1 || y2 != y1 || w2 != w1 + 40 || h2 != h1) {
                printf("SELF FAIL: wt-resize-r %f\n", w2);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            // Мин-размер: левый край тянем вправо далеко -> ширина клампится в WT_MIN.
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = x2 + 1;
            we.y = y2 + h2 / 2;
            wt_event(&we);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = x2 + 1 + w2 + 100;
            we.y = y2 + h2 / 2;
            wt_event(&we);
            float x3, y3, w3, h3;
            doc_px(widget_cget(&g_wt, id), &x3, &y3, &w3, &h3); // F5l(3): экран
            if (w3 != WT_MIN) {
                printf("SELF FAIL: wt-min %f\n", w3);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_dragId = 0;
            g_dragHandle = 0;
        }
        // §14 F1: модель виджетов — add/find/reparent/remove + rect-якоря +
        // save/load roundtrip в /tmp (следов не оставляем).
        {
            UiWidgetTree wt;
            widget_tree_init(&wt);
            UiWidgetRect rP = {UI_A_LEFT | UI_A_TOP, 10, 20, 200, 100};
            UiWidgetStyle sP = {{0.2f, 0.2f, 0.22f, 1.0f}, 4.0f, 1.0f, 16.0f};
            int P = widget_add(&wt, UI_W_PANEL, "P", 0, &rP, &sP);
            UiWidgetRect rB = {UI_A_LEFT | UI_A_TOP, 8, 8, 80, 28};
            int B = widget_add(&wt, UI_W_BUTTON, "Ok", P, &rB, 0);
            UiWidgetRect rL = {UI_A_RIGHT | UI_A_BOTTOM, 8, 8, 120, 20};
            int L = widget_add(&wt, UI_W_LABEL, "T", P, &rL, 0);
            if (!P || !B || !L || wt.count != 3) {
                printf("SELF FAIL: wadd\n");
                bad = 1;
            }
            if (widget_find(&wt, "Ok") != B || widget_find(&wt, "Nope")) {
                printf("SELF FAIL: wfind\n");
                bad = 1;
            }
            // Якоря: L+T = как есть; R+B = от правого/нижнего края.
            float qx, qy, qw, qh;
            widget_rect_px(widget_cget(&wt, P), W, H, &qx, &qy, &qw, &qh);
            if (qx != 10 || qy != 20 || qw != 200 || qh != 100) {
                printf("SELF FAIL: wrect-lt\n");
                bad = 1;
            }
            widget_rect_px(widget_cget(&wt, L), W, H, &qx, &qy, &qw, &qh);
            if (qx != W - 8 - 120 || qy != H - 8 - 20) {
                printf("SELF FAIL: wrect-rb %f %f\n", qx, qy);
                bad = 1;
            }
            // Reparent L под B; цикл P под L — отказ; чужой id — отказ.
            if (!widget_reparent(&wt, L, B)) {
                printf("SELF FAIL: wreparent\n");
                bad = 1;
            }
            if (widget_reparent(&wt, P, L) || widget_reparent(&wt, B, 999) ||
                widget_reparent(&wt, 999, 0) || widget_reparent(&wt, P, P)) {
                printf("SELF FAIL: wreparent-bad-ok\n");
                bad = 1;
            }
            // Remove B валит поддерево (B+L); P сиротеет, count 1.
            if (!widget_remove(&wt, B) || widget_find(&wt, "T") || wt.count != 1) {
                printf("SELF FAIL: wremove count=%d\n", wt.count);
                bad = 1;
            }
            if (widget_remove(&wt, B)) {
                printf("SELF FAIL: wremove-dead-ok\n");
                bad = 1;
            }
            // Дети: 8 ок, 9-й отказ (P пуст после remove).
            int kidOk = 1;
            for (int i = 0; i < 8; i++) {
                char nm[16];
                snprintf(nm, sizeof nm, "k%d", i);
                if (!widget_add(&wt, UI_W_LABEL, nm, P, 0, 0)) kidOk = 0;
            }
            if (!kidOk || widget_add(&wt, UI_W_LABEL, "k9", P, 0, 0) || wt.count != 9) {
                printf("SELF FAIL: wkids-cap count=%d\n", wt.count);
                bad = 1;
            }
            // Roundtrip: свежий файл + слияние в файл с доками (ключи целы).
            const char *tp = "/tmp/opencode/widget_test.json";
            remove(tp);
            if (!widget_json_save(&wt, tp)) {
                printf("SELF FAIL: wsave-fresh\n");
                bad = 1;
            }
            {
                FILE *df = fopen("/tmp/opencode/widget_dock.json", "wb");
                if (df) {
                    fputs("{\n  \"hierW\": [220],\n  \"swapped\": [0]\n}\n", df);
                    fclose(df);
                }
            }
            if (!widget_json_save(&wt, "/tmp/opencode/widget_dock.json")) {
                printf("SELF FAIL: wsave-merge\n");
                bad = 1;
            }
            {
                FILE *df = fopen("/tmp/opencode/widget_dock.json", "rb");
                char chk[70000];
                size_t cn = df ? fread(chk, 1, sizeof chk - 1, df) : 0;
                if (df) fclose(df);
                chk[cn] = 0;
                if (!strstr(chk, "\"hierW\": [220]") || !strstr(chk, "\"widgets\": [")) {
                    printf("SELF FAIL: wsave-dock-kept\n");
                    bad = 1;
                }
            }
            // Обратная сторона F2a2: доки (lay_save) НЕ стирают секцию widgets,
            // в обоих порядках записи (read-modify-write чужой секции).
            {
                UiPanels *pp = ui_panels_create();
                const char *mp = "/tmp/opencode/widget_merge2.json";
                remove(mp);
                if (!widget_json_save(&wt, mp) || !ui_panels_save_layout(pp, mp)) {
                    printf("SELF FAIL: lay-save\n");
                    bad = 1;
                }
                FILE *df = fopen(mp, "rb");
                char chk[70000];
                size_t cn = df ? fread(chk, 1, sizeof chk - 1, df) : 0;
                if (df) fclose(df);
                chk[cn] = 0;
                if (!strstr(chk, "\"hierW\": [") || !strstr(chk, "\"widgets\": [")) {
                    printf("SELF FAIL: lay-keeps-widgets\n");
                    bad = 1;
                }
                if (!widget_json_save(&wt, mp)) { // обратный порядок
                    printf("SELF FAIL: lay-wsave\n");
                    bad = 1;
                }
                df = fopen(mp, "rb");
                cn = df ? fread(chk, 1, sizeof chk - 1, df) : 0;
                if (df) fclose(df);
                chk[cn] = 0;
                if (!strstr(chk, "\"hierW\": [") || !strstr(chk, "\"widgets\": [")) {
                    printf("SELF FAIL: wsave-keeps-dock2\n");
                    bad = 1;
                }
                UiWidgetTree rt2;
                widget_tree_init(&rt2);
                if (!widget_json_load(&rt2, mp) || rt2.count != 9) {
                    printf("SELF FAIL: lay-merge-load count=%d\n", rt2.count);
                    bad = 1;
                }
                ui_panels_destroy(pp);
                remove(mp);
            }
            // F3: порядок/глубина дерева (корни -> дети, в глубину).
            {
                UiWidgetTree keep = g_wt;
                g_wt = wt; // локальное дерево: P + 8 детей
                tree_build();
                if (g_treeN != 9 || g_treeDepth[0] != 0 || g_treeId[0] != P) {
                    printf("SELF FAIL: tree-order n=%d\n", g_treeN);
                    bad = 1;
                }
                for (int i = 1; i < g_treeN; i++)
                    if (g_treeDepth[i] != 1) {
                        printf("SELF FAIL: tree-depth i=%d\n", i);
                        bad = 1;
                    }
                // Клик по строке = выбор этого виджета (hit по центру строки 1).
                float rx, ry, rw, rh;
                tree_rect(1, &rx, &ry, &rw, &rh);
                if (tree_hit(rx + rw * 0.5f, ry + rh * 0.5f) != g_treeId[1]) {
                    printf("SELF FAIL: tree-hit\n");
                    bad = 1;
                }
                g_wt = keep;
            }
            // F3: слои (z) — сдвиг меняет порядок отрисовки/хита; край не двигается.
            {
                UiWidgetTree keep = g_wt;
                g_wt = wt;
                int before[UI_MAX_WIDGETS], after[UI_MAX_WIDGETS];
                int nb = wt_sib_sorted(P, before);
                if (nb < 2) { printf("SELF FAIL: lay-n\n"); bad = 1; }
                if (!widget_layer(&g_wt, before[0], +1)) {
                    printf("SELF FAIL: lay-raise\n");
                    bad = 1;
                }
                int na = wt_sib_sorted(P, after);
                if (na != nb || after[0] != before[1] || after[1] != before[0]) {
                    printf("SELF FAIL: lay-swap\n");
                    bad = 1;
                }
                if (widget_layer(&g_wt, after[na - 1], +1)) {
                    printf("SELF FAIL: lay-top\n"); // верхний край — некуда
                    bad = 1;
                }
                g_wt = keep;
            }
            // z переживает roundtrip save/load.
            {
                const char *zp = "/tmp/opencode/widget_z.json";
                remove(zp);
                if (!widget_json_save(&wt, zp)) {
                    printf("SELF FAIL: z-save\n");
                    bad = 1;
                }
                UiWidgetTree zt;
                widget_tree_init(&zt);
                if (!widget_json_load(&zt, zp) || zt.count != 9) {
                    printf("SELF FAIL: z-load count=%d\n", zt.count);
                    bad = 1;
                } else {
                    int a = widget_find(&zt, "k1");
                    if (!a || widget_cget(&zt, a)->z != widget_cget(&wt, a)->z) {
                        printf("SELF FAIL: z-keep\n");
                        bad = 1;
                    }
                }
                remove(zp);
            }
            UiWidgetTree rt;
            widget_tree_init(&rt);
            if (!widget_json_load(&rt, "/tmp/opencode/widget_dock.json") || rt.count != 9) {
                printf("SELF FAIL: wload count=%d\n", rt.count);
                bad = 1;
            } else {
                const UiWidget *rp = widget_cget(&rt, P);
                if (!rp || strcmp(rp->name, "P") || rp->nchildren != 8 ||
                    rp->rect.anchors != (UI_A_LEFT | UI_A_TOP) || rp->style.rounding != 4.0f) {
                    printf("SELF FAIL: wload-data\n");
                    bad = 1;
                }
                // Связи пережили roundtrip (k3 ребёнок P).
                int k3 = widget_find(&rt, "k3");
                const UiWidget *rk = widget_cget(&rt, k3);
                if (!k3 || !rk || rk->parent != P) {
                    printf("SELF FAIL: wload-links\n");
                    bad = 1;
                }
            }
            // Чужой файл: дубль id и цикл — отказ, дерево чистое.
            {
                FILE *bf = fopen("/tmp/opencode/widget_bad.json", "wb");
                if (bf) {
                    fputs("{\"widgets\":["
                          "{\"id\":1,\"type\":0,\"name\":\"A\",\"parent\":2},"
                          "{\"id\":2,\"type\":0,\"name\":\"B\",\"parent\":1}]}",
                          bf);
                    fclose(bf);
                }
            }
            UiWidgetTree bt;
            widget_tree_init(&bt);
            if (widget_json_load(&bt, "/tmp/opencode/widget_bad.json") || bt.count != 0) {
                printf("SELF FAIL: wload-cycle-ok count=%d\n", bt.count);
                bad = 1;
            }
            if (widget_json_load(&rt, "/tmp/opencode/nope.json") || rt.count != 9) {
                printf("SELF FAIL: wload-missing\n");
                bad = 1;
            }
            remove(tp);
            remove("/tmp/opencode/widget_dock.json");
            remove("/tmp/opencode/widget_bad.json");
        }
        // §14 F3c: ОБЩИЙ рантайм — сборка примитивов и верхний хит (то же,
        // что увидит игра). Порядок (z,id) един для сборки, хита и дерева.
        {
            UiWidgetTree srn;
            widget_tree_init(&srn);
            UiWidgetStyle sst;
            memset(&sst, 0, sizeof sst);
            sst.bg[0] = 1.0f;
            sst.bg[3] = 1.0f;
            sst.alpha = 1.0f;
            UiWidgetRect srect = {UI_A_LEFT | UI_A_TOP, 100, 100, 200, 200};
            int sidA = widget_add(&srn, UI_W_PANEL, "RA", 0, &srect, &sst);
            srect.x = 150;
            srect.y = 150;
            sst.bg[0] = 0.0f;
            sst.bg[2] = 1.0f;
            int sidB = widget_add(&srn, UI_W_PANEL, "RB", 0, &srect, &sst);
            // Прозрачный не попадает в кадр: 2 непрозрачных -> 2, один alpha=0 -> 1.
            RcVecShape sbuf[UI_MAX_PRIMS];
            if (ui_widget_build(&srn, g_WW, g_WH, sbuf, UI_MAX_PRIMS) != 2) {
                printf("SELF FAIL: rtb-n\n");
                bad = 1;
            }
            widget_get(&srn, sidA)->style.alpha = 0.0f;
            if (ui_widget_build(&srn, g_WW, g_WH, sbuf, UI_MAX_PRIMS) != 1) {
                printf("SELF FAIL: rtb-alpha\n");
                bad = 1;
            }
            widget_get(&srn, sidA)->style.alpha = 1.0f;
            // Перекрытие 150..300: выше по (z,id) на старте — B (id больше).
            if (ui_widget_hit(&srn, g_WW, g_WH, 200, 200) != sidB) {
                printf("SELF FAIL: rtb-hit0\n");
                bad = 1;
            }
            if (!widget_layer(&srn, sidA, +1)) {
                printf("SELF FAIL: rtb-raise\n");
                bad = 1;
            }
            if (ui_widget_hit(&srn, g_WW, g_WH, 200, 200) != sidA) {
                printf("SELF FAIL: rtb-hit-top\n");
                bad = 1;
            }
            if (ui_widget_hit(&srn, g_WW, g_WH, 110, 110) != sidA) { // B не покрывает
                printf("SELF FAIL: rtb-hit-a\n");
                bad = 1;
            }
            if (ui_widget_hit(&srn, g_WW, g_WH, 10, 10)) { // мимо обеих
                printf("SELF FAIL: rtb-hit-miss\n");
                bad = 1;
            }
            // Редакторская обёртка = тот же рантайм (не расходимся).
            UiWidgetTree keepRT = g_wt;
            g_wt = srn;
            if (wt_hit(200 + doc_ox(), 200 + doc_oy()) !=
                ui_widget_hit(&g_wt, g_WW, g_WH, 200, 200)) { // F5l(3): мышь -> экран игры
                printf("SELF FAIL: rtb-wrap\n");
                bad = 1;
            }
            g_wt = keepRT;
        }
        // §14 F3c(1b): SELF-HOST — правим СОБСТВЕННЫЙ UI редактора теми же
        // механизмами (выбор/драг/ручки/инспектор), документ не трогаем.
        {
            widget_tree_init(&g_wt); // документ пуст
            g_editSelf = 1;
            g_act = &g_edWt;
            g_wtSel = 0;
            g_palHot = -1;
            g_dragId = 0;
            g_dragHandle = 0;
            ed_chrome_init();
            ed_chrome_sync();
            float px, py, pw, ph;
            ed_rect(g_edPal, &px, &py, &pw, &ph);
            if (pw <= 0 || ph <= 0) {
                printf("SELF FAIL: self-geom %f %f\n", pw, ph);
                bad = 1;
            }
            // Клик по паддингу панели (вне строк палитры) -> выбран виджет ХРОМА.
            UiEvent we;
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = px + 3;
            we.y = py + 3;
            if (!wt_event(&we) || g_wtSel != g_edPal) {
                printf("SELF FAIL: self-pick %d\n", g_wtSel);
                bad = 1;
            }
            // Драг -> панель хрома уехала; правка юзера стабильна (sync не вернёт).
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = px + 3 + 40;
            we.y = py + 3 + 25;
            wt_event(&we);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            float x2, y2, w2, h2;
            ed_rect(g_edPal, &x2, &y2, &w2, &h2);
            if (x2 != px + 40 || y2 != py + 25 || w2 != pw || h2 != ph) {
                printf("SELF FAIL: self-move %f %f\n", x2, y2);
                bad = 1;
            }
            ed_chrome_sync();
            ed_rect(g_edPal, &x2, &y2, &w2, &h2);
            if (x2 != px + 40 || y2 != py + 25) {
                printf("SELF FAIL: self-keep %f %f\n", x2, y2);
                bad = 1;
            }
            // Инспектор в self-режиме правит ХРОМ (round), а не документ.
            UiWidget *cw = widget_get(&g_edWt, g_edPal);
            float r0 = cw->style.rounding;
            g_insField = IF_ROUND;
            g_insDownX = 0;
            g_insDownVal = r0;
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = 40;
            wt_event(&we); // 40 * 0.25 = +10
            if (cw->style.rounding <= r0 || g_wt.count != 0) {
                printf("SELF FAIL: self-ins %f\n", (double)cw->style.rounding);
                bad = 1;
            }
            g_insField = -1;
            // Хром НЕ пишем в layout.json (это вид инструмента, не документ):
            // коммит в self-режиме не создаёт файл.
            remove(UI_LAYOUT_PATH);
            wt_persist();
            {
                FILE *pf = fopen(UI_LAYOUT_PATH, "rb");
                if (pf) {
                    printf("SELF FAIL: self-persist\n");
                    fclose(pf);
                    bad = 1;
                }
            }
            // Слои хрома: ed_ws_panels() панели-корня, Lay + меняет порядок (как у документа).
            int before[UI_MAX_WIDGETS];
            int nb = wt_sib_sorted(0, before);
            if (nb != ed_ws_panels()) {
                printf("SELF FAIL: self-lay-n %d\n", nb);
                bad = 1;
            }
            // Возврат в обычный режим + дефолтный хром для остальных проверок.
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            ed_chrome_init();
            ed_chrome_sync();
        }
        // §14 F3c(1b): список виджетов — ОКНО СО СКРОЛЛОМ (жалоба: строки
        // вылезали за панель; листать было нельзя).
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            g_treeScroll = 0;
            g_palHot = -1;
            for (int i = 0; i < 12; i++) wt_spawn(UI_W_BUTTON);
            int last = g_wtSel;
            tree_build();
            int rows = tree_rows_fit();
            if (g_treeN != 12 || rows < 1) {
                printf("SELF FAIL: tree-scroll-n %d rows %d\n", g_treeN, rows);
                bad = 1;
            }
            // Выбран последний -> он автоматически в кадре.
            g_wtSel = last;
            tree_build();
            {
                int idx = -1;
                for (int i = 0; i < g_treeN; i++)
                    if (g_treeId[i] == g_wtSel) idx = i;
                if (idx < 0 || idx < g_treeScroll || idx >= g_treeScroll + rows) {
                    printf("SELF FAIL: tree-scroll-sel %d sc %d rows %d\n", idx,
                           g_treeScroll, rows);
                    bad = 1;
                }
            }
            // Строка ниже последней видимой — мимо (не «вылезает» за панель).
            {
                float x, y, w, h;
                tree_rect(rows, &x, &y, &w, &h); // виртуальная строка вне окна
                float px, py, pw, ph;
                ed_rect(g_edTree, &px, &py, &pw, &ph);
                if (y + h <= py + ph) { // она вообще внутри панели -> не наш случай
                } else if (tree_hit(x + w * 0.5f, y + h * 0.5f)) {
                    printf("SELF FAIL: tree-scroll-clip\n");
                    bad = 1;
                }
            }
            // Колесо над панелью — листает; над пустотой — нет.
            {
                float px, py, pw, ph;
                ed_rect(g_edTree, &px, &py, &pw, &ph);
                g_wtSel = 0; // без выбора нет автоскролла к нему
                g_treeScroll = 0;
                tree_build();
                UiEvent we;
                memset(&we, 0, sizeof we);
                we.kind = UI_EV_WHEEL;
                we.value = -1.0f; // вниз
                we.x = px + pw * 0.5f;
                we.y = py + ph * 0.5f;
                int sc0 = g_treeScroll;
                if (!wt_event(&we) || g_treeScroll <= sc0) {
                    printf("SELF FAIL: tree-wheel %d -> %d\n", sc0, g_treeScroll);
                    bad = 1;
                }
                memset(&we, 0, sizeof we);
                we.kind = UI_EV_WHEEL;
                we.value = 1.0f;
                we.x = px + pw * 0.5f;
                we.y = py + ph * 0.5f;
                wt_event(&we);
                if (g_treeScroll != 0) {
                    printf("SELF FAIL: tree-wheel-back %d\n", g_treeScroll);
                    bad = 1;
                }
                memset(&we, 0, sizeof we);
                we.kind = UI_EV_WHEEL;
                we.value = -1.0f;
                we.x = (float)(g_vpx + 5);
                we.y = (float)(g_vpy + g_vph - 60);
                if (wt_event(&we)) {
                    printf("SELF FAIL: tree-wheel-steal\n");
                    bad = 1;
                }
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_treeScroll = 0;
        }
        // Удаление виджета: поддерево целиком, выбор уходит на родителя, пустая
        // сцена — отказ. Строка палитры Delete — ДЕЙСТВИЕ (ed_pal_is_action), а хром
        // редактора (своё дерево) удалить нельзя.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            g_palHot = -1;
            int root = wt_spawn(UI_W_PANEL);
            int k1 = wt_spawn(UI_W_BUTTON);
            int k2 = wt_spawn(UI_W_LABEL);
            widget_reparent(&g_wt, k1, root);
            widget_reparent(&g_wt, k2, root);
            if (g_wt.count != 3) {
                printf("SELF FAIL: del-setup %d\n", g_wt.count);
                bad = 1;
            }
            g_wtSel = root; // родитель: уходит всё поддерево
            if (!wt_delete_sel() || g_wt.count != 0 || widget_cget(&g_wt, k1) ||
                widget_cget(&g_wt, k2)) {
                printf("SELF FAIL: del-sub %d\n", g_wt.count);
                bad = 1;
            }
            if (wt_delete_sel() || g_wtSel) { // удалять нечего
                printf("SELF FAIL: del-empty\n");
                bad = 1;
            }
            int a = wt_spawn(UI_W_PANEL);
            int b = wt_spawn(UI_W_BUTTON);
            widget_reparent(&g_wt, b, a);
            g_wtSel = b; // ребёнок: остаётся родитель и становится выбранным
            if (!wt_delete_sel() || g_wt.count != 1 || g_wtSel != a) {
                printf("SELF FAIL: del-child %d sel %d\n", g_wt.count, g_wtSel);
                bad = 1;
            }
            { // клик по строке палитры Delete = то же (проверяет и ed_pal_is_action)
                float x, y, w, h;
                pal_rect(ed_pal_n() - 1, &x, &y, &w, &h);
                UiEvent de;
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_DOWN;
                de.x = x + 4;
                de.y = y + 4;
                route_event(&de);
                if (g_wt.count != 0 || g_wtSel) {
                    printf("SELF FAIL: del-pal %d\n", g_wt.count);
                    bad = 1;
                }
            }
            { // хром редактора — не документ: не удаляется
                int keep = g_edWt.count;
                g_editSelf = 1;
                g_act = &g_edWt;
                g_wtSel = g_edPal;
                if (wt_delete_sel() || g_edWt.count != keep) {
                    printf("SELF FAIL: del-self %d\n", g_edWt.count);
                    bad = 1;
                }
                g_editSelf = 0;
                g_act = &g_wt;
                g_wtSel = 0;
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F4: история документа — тот же путь, что у клавиш (wt_undo_apply):
        // откат/повтор возвращают МОДЕЛЬ (id и дерево), правка → откат, удаление
        // → откат возвращает поддерево, выбор на исчезнувшем виджете сбрасывается.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            ui_wundo_init(&g_undo);
            undo_commit("load");
            int id1 = wt_spawn(UI_W_PANEL);
            undo_commit("add");
            int id2 = wt_spawn(UI_W_BUTTON);
            undo_commit("add");
            if (!wt_undo_apply(0) || g_wt.count != 1 || widget_cget(&g_wt, id2)) {
                printf("SELF FAIL: undo-add %d\n", g_wt.count);
                bad = 1;
            }
            if (!wt_undo_apply(1) || g_wt.count != 2 || !widget_cget(&g_wt, id2)) {
                printf("SELF FAIL: redo-add %d\n", g_wt.count);
                bad = 1;
            }
            float bx0 = widget_cget(&g_wt, id1)->rect.x; // правка «как драг»
            widget_get(&g_wt, id1)->rect.x += 33.0f;
            undo_commit("move");
            if (!wt_undo_apply(0) || widget_cget(&g_wt, id1)->rect.x != bx0) {
                printf("SELF FAIL: undo-move\n");
                bad = 1;
            }
            g_wtSel = id1;
            widget_reparent(&g_wt, id2, id1); // поддерево: кнопка внутри панели
            undo_commit("reparent");
            int cnt = g_wt.count;
            if (!wt_delete_sel() || g_wt.count != 0) {
                printf("SELF FAIL: undo-del %d\n", g_wt.count);
                bad = 1;
            }
            if (!wt_undo_apply(0) || g_wt.count != cnt || !widget_cget(&g_wt, id2)) {
                printf("SELF FAIL: undo-del-back %d\n", g_wt.count);
                bad = 1;
            }
            // повтор удаления: выбранного виджета больше нет — выбор снимается
            g_wtSel = id2;
            if (!wt_undo_apply(1) || g_wt.count != 0 || g_wtSel) {
                printf("SELF FAIL: redo-del %d sel %d\n", g_wt.count, g_wtSel);
                bad = 1;
            }
            // Хром редактора в историю документа не пишется (вид инструмента).
            ui_wundo_init(&g_undo);
            undo_commit("load");
            g_editSelf = 1;
            g_act = &g_edWt;
            widget_get(&g_edWt, g_edPal)->rect.x += 5.0f;
            undo_commit("self");
            if (ui_wundo_depth(&g_undo) != 0) {
                printf("SELF FAIL: undo-self %d\n", ui_wundo_depth(&g_undo));
                bad = 1;
            }
            g_editSelf = 0;
            g_act = &g_wt;
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F5a: ТЕКСТ — правка имени (оно же подпись на холсте). Каретка,
        // вставка В СЕРЕДИНУ, Backspace/Delete по каретке, Home/End, Esc = отмена
        // (без следа в истории), Enter = коммит (документ + шаг истории), мусор
        // ('"'/'[' ) в имя не пускаем (имя пишется в JSON сырым текстом),
        // занятое имя -> суффикс (уникальность), пустое — не коммит.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            ui_wundo_init(&g_undo);
            undo_commit("load");
            int la = wt_spawn(UI_W_LABEL);
            int lb = wt_spawn(UI_W_LABEL);
            snprintf(widget_get(&g_wt, la)->name, UI_WIDGET_NAME, "%s", "Body");
            snprintf(widget_get(&g_wt, lb)->name, UI_WIDGET_NAME, "%s", "Body2");
            undo_commit("add");
            int depth0 = ui_wundo_depth(&g_undo);
            g_wtSel = la;
            // Вход — через КОМАНДУ (гейт canExecute + документ), а не вызовом
            // функции режима: иначе тест не поймал бы обрыв на этом пути.
            ed_cmd_run(ed_cmd_find("edit.rename"));
            // F2/клик по имени: имя ВЫДЕЛЕНО ЦЕЛИКОМ (как UE/Unity/Blender).
            if (!g_nameEdit || strcmp(g_nameBuf, "Body") != 0 || g_nameCaret != 4 ||
                g_nameAnchor != 0 || !name_has_sel()) {
                printf("SELF FAIL: text-begin '%s' %d/%d\n", g_nameBuf, g_nameCaret,
                       g_nameAnchor);
                bad = 1;
            }
            // Первая же буква ЗАМЕНЯЕТ выделенное имя, а не клеится в конец.
            name_edit_char('X');
            if (strcmp(g_nameBuf, "X") != 0 || g_nameCaret != 1 || name_has_sel()) {
                printf("SELF FAIL: text-replace '%s' %d\n", g_nameBuf, g_nameCaret);
                bad = 1;
            }
            name_edit_char('Y');
            if (strcmp(g_nameBuf, "XY") != 0) {
                printf("SELF FAIL: text-type '%s'\n", g_nameBuf);
                bad = 1;
            }
            // Набрали заново полное имя: каретка, вставка В СЕРЕДИНУ, Backspace.
            name_edit_key(GLFW_KEY_HOME, 0);
            for (int i = 0; i < UI_WIDGET_NAME; i++) name_edit_key(GLFW_KEY_DELETE, 0);
            const char *n1 = "Body";
            for (const char *p = n1; *p; p++) name_edit_char((unsigned)(unsigned char)*p);
            if (strcmp(g_nameBuf, "Body") != 0 || g_nameCaret != 4) {
                printf("SELF FAIL: text-retype '%s' %d\n", g_nameBuf, g_nameCaret);
                bad = 1;
            }
            name_edit_key(GLFW_KEY_LEFT, 0); // каретка перед 'y'
            name_edit_char('X');             // вставка В СЕРЕДИНУ строки
            if (strcmp(g_nameBuf, "BodXy") != 0 || g_nameCaret != 4) {
                printf("SELF FAIL: text-ins '%s' %d\n", g_nameBuf, g_nameCaret);
                bad = 1;
            }
            name_edit_key(GLFW_KEY_BACKSPACE, 0); // и убрать её
            if (strcmp(g_nameBuf, "Body") != 0 || g_nameCaret != 3) {
                printf("SELF FAIL: text-bs '%s' %d\n", g_nameBuf, g_nameCaret);
                bad = 1;
            }
            // Shift+Home = выделить до начала; Backspace убирает ВЫДЕЛЕННОЕ.
            name_edit_key(GLFW_KEY_HOME, GLFW_MOD_SHIFT);
            if (!name_has_sel() || name_sel_lo() != 0 || name_sel_hi() != 3) {
                printf("SELF FAIL: text-selshift %d..%d\n", name_sel_lo(), name_sel_hi());
                bad = 1;
            }
            name_edit_key(GLFW_KEY_LEFT, 0); // клик вправо отменяет выделение
            if (name_has_sel()) {
                printf("SELF FAIL: text-selcollapse\n");
                bad = 1;
            }
            name_edit_key(GLFW_KEY_HOME, 0);
            name_edit_key(GLFW_KEY_DELETE, 0); // 'B' в начале
            if (strcmp(g_nameBuf, "ody") != 0 || g_nameCaret != 0) {
                printf("SELF FAIL: text-del '%s' %d\n", g_nameBuf, g_nameCaret);
                bad = 1;
            }
            name_edit_char('"'); // структурные символы в имя не пускаем
            name_edit_char('[');
            if (strcmp(g_nameBuf, "ody") != 0) {
                printf("SELF FAIL: text-sanitize '%s'\n", g_nameBuf);
                bad = 1;
            }
            name_edit_end(0); // Esc: отмена — модель и история не тронуты
            if (strcmp(widget_cget(&g_wt, la)->name, "Body") != 0 ||
                ui_wundo_depth(&g_undo) != depth0) {
                printf("SELF FAIL: text-cancel '%s' d%d\n", widget_cget(&g_wt, la)->name,
                       ui_wundo_depth(&g_undo));
                bad = 1;
            }
            name_edit_begin(); // коммит: Enter (Ctrl+A -> заменяем имя целиком)
            name_edit_key(GLFW_KEY_A, GLFW_MOD_CONTROL);
            if (!name_has_sel()) {
                printf("SELF FAIL: text-ctrl-a\n");
                bad = 1;
            }
            for (const char *p = "Body3"; *p; p++) name_edit_char((unsigned)(unsigned char)*p);
            name_edit_end(1);
            if (strcmp(widget_cget(&g_wt, la)->name, "Body3") != 0 ||
                ui_wundo_depth(&g_undo) != depth0 + 1) {
                printf("SELF FAIL: text-commit '%s' d%d\n", widget_cget(&g_wt, la)->name,
                       ui_wundo_depth(&g_undo));
                bad = 1;
            }
            name_edit_begin(); // без правок: ни модель, ни история не меняются
            name_edit_end(1);
            if (strcmp(widget_cget(&g_wt, la)->name, "Body3") != 0 ||
                ui_wundo_depth(&g_undo) != depth0 + 1) {
                printf("SELF FAIL: text-nochange\n");
                bad = 1;
            }
            name_edit_begin(); // очистить всё и НЕ вводить ничего -> не коммит
            for (int i = 0; i < UI_WIDGET_NAME; i++) name_edit_key(GLFW_KEY_DELETE, 0);
            if (g_nameLen != 0) {
                printf("SELF FAIL: text-clear %d\n", g_nameLen);
                bad = 1;
            }
            name_edit_end(1);
            if (strcmp(widget_cget(&g_wt, la)->name, "Body3") != 0) {
                printf("SELF FAIL: text-empty '%s'\n", widget_cget(&g_wt, la)->name);
                bad = 1;
            }
            name_edit_begin(); // занятое имя (у соседа Body2) -> суффикс
            for (int i = 0; i < UI_WIDGET_NAME; i++) name_edit_key(GLFW_KEY_DELETE, 0);
            for (const char *p = "Body2"; *p; p++) name_edit_char((unsigned)(unsigned char)*p);
            name_edit_end(1);
            if (strcmp(widget_cget(&g_wt, la)->name, "Body2_2") != 0) {
                printf("SELF FAIL: text-uniq '%s'\n", widget_cget(&g_wt, la)->name);
                bad = 1;
            }
            if (!wt_undo_apply(0) || strcmp(widget_cget(&g_wt, la)->name, "Body3") != 0) {
                printf("SELF FAIL: text-undo '%s'\n", widget_cget(&g_wt, la)->name);
                bad = 1;
            }
            // Переименование НЕ трогает геометрию (это текст, не layout).
            if (widget_cget(&g_wt, lb) == 0 || widget_cget(&g_wt, la)->type != UI_W_LABEL) {
                printf("SELF FAIL: text-model\n");
                bad = 1;
            }
            g_nameEdit = 0;
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F5c: ГЕОМЕТРИЯ ХРОМА — панели инструмента не пересекаются и не
        // залезают на легаси-строку меню/коллапса (тестер: «всё стоит криво»,
        // панели «съехали»). Дерево вмещает заголовок + крошки + >=3 строки.
        {
            ed_chrome_sync();
            float ax, ay, aw, ah, bx, by, bw, bh, cx, cy, cw, ch;
            ed_rect(g_edPal, &ax, &ay, &aw, &ah);
            ed_rect(g_edTree, &bx, &by, &bw, &bh);
            ed_rect(g_edIns, &cx, &cy, &cw, &ch);
            int ov = (ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah) ||
                     (ax < cx + cw && cx < ax + aw && ay < cy + ch && cy < ay + ah);
            if (ov || ay < (float)ed_top() || cy < (float)ed_top()) {
                printf("SELF FAIL: chrome-geom pal=%.0f,%.0f tree=%.0f,%.0f ins=%.0f,%.0f "
                       "top=%d\n",
                       ax, ay, bx, by, cx, cy, ed_top());
                bad = 1;
            }
            if (bh < (float)(TREE_HDR + TREE_PATH + 3 * TREE_RH)) {
                printf("SELF FAIL: chrome-tree-h %.0f\n", bh);
                bad = 1;
            }
        }
        // §14 F5l(5): ЗАКРЫТАЯ ПАНЕЛЬ НЕ РИСУЕТ СОДЕРЖИМОЕ (и не ловит клик).
        // Баг тестера: у закрытого инспектора rect обнулялся, а ряды продолжали
        // строиться от него — 1 заголовок + IF_N полей «зебрами» 200x20 из угла
        // (0,0) поверх палитры, «появляются только при выделении». Вторая сторона
        // инварианта: ОТКРЫТАЯ панель рисует содержимое ВНУТРИ своего прямоугольника
        // (если содержимое когда-нибудь «уедет» от панели — тест это поймает).
        {
            static RcVecShape pc[UI_MAX_PRIMS]; // закрытые панели: сюда писать нечем
            static RcVecShape p0[UI_MAX_PRIMS], p1[UI_MAX_PRIMS], p2[UI_MAX_PRIMS];
            int saveOpen[3] = {g_edOpen[0], g_edOpen[1], g_edOpen[2]};
            UiWidgetTree *saveAct = g_act;
            int saveSel = g_wtSel;
            int saveSelf = g_editSelf;
            memset(g_edMoved, 0, sizeof g_edMoved); // каноническая док-раскладка
            widget_tree_init(&g_wt);
            UiWidgetRect rr;
            UiWidgetStyle ss;
            memset(&rr, 0, sizeof rr);
            memset(&ss, 0, sizeof ss);
            ss.alpha = 1.0f;
            rr.anchors = UI_A_LEFT | UI_A_TOP;
            rr.x = 40;
            rr.y = 40;
            rr.w = 120;
            rr.h = 48;
            int probe = widget_add(&g_wt, UI_W_BUTTON, "Probe", 0, &rr, &ss);
            g_act = &g_wt;
            g_editSelf = 0;
            g_wtSel = probe; // выделение есть: именно при нём и всплывал баг
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 0;
            ed_chrome_sync();
            int c0 = 0, c1 = 0, c2 = 0;
            pal_draw(pc, &c0);
            tree_draw(pc, &c1);
            ins_draw(pc, &c2);
            if (c0 || c1 || c2) {
                printf("SELF FAIL: closed-panel-draws pal=%d tree=%d insp=%d\n", c0, c1, c2);
                bad = 1;
            }
            // Закрытая панель не только не рисуется, но и не кликается: точка, где
            // росли «зебры» (угол 8,30+), обязана быть пустой. У инспектора ширина
            // строки фиксированная (INS_W), поэтому без гейта хит бы СРАБОТАЛ.
            if (ins_hit(30, 40) != -1 || ins_head_hit(30, 40)) {
                printf("SELF FAIL: closed-panel-hit\n");
                bad = 1;
            }
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            ed_chrome_sync();
            int n0 = 0, n1 = 0, n2 = 0;
            pal_draw(p0, &n0);
            tree_draw(p1, &n1);
            ins_draw(p2, &n2);
            // Инспектор: заголовок поля-имени + IF_N строк + 3 плашки bg (r/g/b).
            if (n0 < ed_pal_n() || n1 < 1 || n2 != 1 + IF_N + 3) {
                printf("SELF FAIL: open-panel-draws pal=%d tree=%d insp=%d want %d\n", n0, n1, n2,
                       1 + IF_N + 3);
                bad = 1;
            }
            if (!panel_content_inside(p0, n0, g_edPal) ||
                !panel_content_inside(p1, n1, g_edTree) ||
                !panel_content_inside(p2, n2, g_edIns)) {
                printf("SELF FAIL: panel-content-escape\n");
                bad = 1;
            }
            widget_tree_init(&g_wt);
            g_edOpen[0] = saveOpen[0];
            g_edOpen[1] = saveOpen[1];
            g_edOpen[2] = saveOpen[2];
            g_act = saveAct;
            g_wtSel = saveSel;
            g_editSelf = saveSelf;
            ed_chrome_sync();
        }
        // §14 F5d: ИЕРАРХИЯ — крошки предков (клик = выбрать предка), обрезка
        // слева при нехватке места, Ctrl+клик «сквозь» верхний в стопке.
        {
            UiWidgetTree pt;
            widget_tree_init(&pt);
            g_editSelf = 0;
            UiWidgetRect rr;
            UiWidgetStyle ss;
            memset(&rr, 0, sizeof rr);
            memset(&ss, 0, sizeof ss);
            rr.anchors = UI_A_LEFT | UI_A_TOP;
            rr.x = 100;
            rr.y = 100;
            rr.w = 200;
            rr.h = 100;
            ss.alpha = 1.0f;
            int pa = widget_add(&pt, UI_W_PANEL, "Card", 0, &rr, &ss);
            rr.x = 120;
            rr.y = 120;
            rr.w = 100;
            rr.h = 60;
            int pb = widget_add(&pt, UI_W_BUTTON, "Ok", pa, &rr, &ss);
            rr.x = 130;
            rr.y = 130;
            rr.w = 60;
            rr.h = 30;
            int pc = widget_add(&pt, UI_W_LABEL, "Cap", pb, &rr, &ss);
            if (!pa || !pb || !pc) {
                printf("SELF FAIL: path-add\n");
                bad = 1;
            }
            g_act = &pt;
            g_wtSel = pc;
            tree_path_build();
            if (g_pathN != 4 || g_pathId[0] != 0 || g_pathId[1] != pa || g_pathId[2] != pb ||
                g_pathId[3] != pc) {
                printf("SELF FAIL: path-build n=%d\n", g_pathN);
                bad = 1;
            }
            tree_path_layout();
            float px2, py2, pw2, ph2;
            tree_path_rect(&px2, &py2, &pw2, &ph2);
            (void)pw2;
            int hid = tree_path_hit_at(g_pathX[2] + 2, py2 + ph2 / 2);
            if (hid != pb) {
                printf("SELF FAIL: path-hit %d want %d\n", hid, pb);
                bad = 1;
            }
            if (tree_path_hit_at(px2 + 2, py2 - 30) != -1) {
                printf("SELF FAIL: path-hit-out\n");
                bad = 1;
            }
            // Ctrl+клик: сверху вниз по стопке, снизу — снова верхний (цикл).
            int d1 = wt_pick_deep(140, 140, 0);
            int d2 = wt_pick_deep(140, 140, d1);
            int d3 = wt_pick_deep(140, 140, d2);
            int d4 = wt_pick_deep(140, 140, d3);
            if (d1 != pc || d2 != pb || d3 != pa || d4 != pc) {
                printf("SELF FAIL: deep-pick %d %d %d %d\n", d1, d2, d3, d4);
                bad = 1;
            }
            // Длинная цепочка не влезает в панель -> обрезка СЛЕВА, выделенный
            // (последний) сегмент виден всегда.
            int deepId = pb;
            for (int i = 0; i < 7; i++) {
                char nm[UI_WIDGET_NAME];
                snprintf(nm, sizeof nm, "VeryLongWidget%02d", i);
                rr.x = 130;
                rr.y = 130;
                rr.w = 40;
                rr.h = 20;
                deepId = widget_add(&pt, UI_W_LABEL, nm, deepId, &rr, &ss);
            }
            g_wtSel = deepId;
            tree_path_layout();
            float lx, ly, lw, lh;
            tree_path_rect(&lx, &ly, &lw, &lh);
            (void)ly;
            (void)lh;
            if (g_pathN < 8 || g_pathFrom <= 0) {
                printf("SELF FAIL: path-clip n=%d from=%d\n", g_pathN, g_pathFrom);
                bad = 1;
            }
            int last = g_pathN - 1;
            if (g_pathX[last] + g_pathW[last] > lx + lw + 1.0f ||
                g_pathId[last] != deepId) {
                printf("SELF FAIL: path-fit x=%.1f w=%.1f lim=%.1f\n", g_pathX[last],
                       g_pathW[last], lx + lw);
                bad = 1;
            }
            g_wtSel = 0;
            widget_tree_init(&pt);
            g_act = &g_wt;
        }
        // §14 F5h: ДЕРЕВО — drag&drop. Тестер: «не могу добавить/вытащить ветку
        // из дерева, дерево бесполезно». Бросок НА строку = внутрь (стать
        // ребёнком), в ВЕРХ/НИЗ строки = перед/после (порядок = слой), на крошку
        // Scene = в корень. Родитель и порядок меняются ОДНИМ действием модели.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            g_treeScroll = 0;
            g_treePress = 0;
            g_treeDrag = 0;
            g_treeDropId = -1;
            ed_chrome_sync();
            int a = wt_spawn(UI_W_PANEL);
            int b = wt_spawn(UI_W_BUTTON);
            int c = wt_spawn(UI_W_LABEL);
            int kid = wt_spawn(UI_W_BUTTON);
            widget_move_before(&g_wt, kid, a, 0); // ребёнок панели
            if (g_wt.count != 4 || widget_cget(&g_wt, kid)->parent != a) {
                printf("SELF FAIL: tdd-setup %d\n", g_wt.count);
                bad = 1;
            }
            tree_build();
            if (g_treeN != 4 || g_treeId[1] != kid) { // порядок = как рисуем
                printf("SELF FAIL: tdd-order n=%d id1=%d want %d\n", g_treeN, g_treeId[1],
                       kid);
                bad = 1;
            }
            // Геометрия цели: середина = ВНУТРЬ, верх/низ = порядок, мимо панели
            // = отмена, крошка Scene = в корень.
            float x, y, w, h;
            tree_rect(2, &x, &y, &w, &h); // строка b
            int md = -9;
            int tg = tree_drop_at(x + w * 0.5f, y + h * 0.5f, &md);
            if (tg != b || md != 0) {
                printf("SELF FAIL: tdd-at-inside %d %d\n", tg, md);
                bad = 1;
            }
            tg = tree_drop_at(x + w * 0.5f, y + 2.0f, &md);
            if (tg != b || md != 1) {
                printf("SELF FAIL: tdd-at-before %d %d\n", tg, md);
                bad = 1;
            }
            tg = tree_drop_at(x + w * 0.5f, y + h - 2.0f, &md);
            if (tg != b || md != 2) {
                printf("SELF FAIL: tdd-at-after %d %d\n", tg, md);
                bad = 1;
            }
            md = -9;
            if (tree_drop_at((float)(g_vpx + 5), (float)(g_vpy + 5), &md) != -1 || md != -9) {
                printf("SELF FAIL: tdd-at-out %d\n", md);
                bad = 1;
            }
            {
                float scx, scy, scw, sch;
                tree_path_rect(&scx, &scy, &scw, &sch);
                if (tree_drop_at(scx + 2, scy + sch * 0.5f, &md) != 0 || md != 0) {
                    printf("SELF FAIL: tdd-at-scene %d\n", md);
                    bad = 1;
                }
            }
            // ВНУТРЬ: c становится ребёнком b.
            if (!tree_drop_apply(c, b, 0) || widget_cget(&g_wt, c)->parent != b) {
                printf("SELF FAIL: tdd-inside\n");
                bad = 1;
            }
            // ПЕРЕД: корень b перед корнем a (порядок корней живёт в z).
            if (!tree_drop_apply(b, a, 1)) {
                printf("SELF FAIL: tdd-before\n");
                bad = 1;
            }
            int sib[UI_MAX_WIDGETS];
            int ns = wt_sib_sorted(0, sib);
            if (ns < 2 || sib[0] != b || sib[1] != a) {
                printf("SELF FAIL: tdd-before-order %d %d\n", ns >= 1 ? sib[0] : 0,
                       ns >= 2 ? sib[1] : 0);
                bad = 1;
            }
            // ПОСЛЕ: a после b — он уже там: порядок не должен поехать.
            (void)tree_drop_apply(a, b, 2);
            ns = wt_sib_sorted(0, sib);
            if (ns < 2 || sib[0] != b || sib[1] != a) {
                printf("SELF FAIL: tdd-after-order %d %d\n", ns >= 1 ? sib[0] : 0,
                       ns >= 2 ? sib[1] : 0);
                bad = 1;
            }
            // ЦИКЛ: a нельзя сунуть в собственного ребёнка kid (модель отказывает).
            if (tree_drop_apply(a, kid, 0) || widget_cget(&g_wt, a)->parent != 0 ||
                widget_cget(&g_wt, kid)->parent != a) {
                printf("SELF FAIL: tdd-cycle\n");
                bad = 1;
            }
            // В КОРЕНЬ: c (внутри b) на крошку Scene -> снова корень.
            if (!tree_drop_apply(c, 0, 0) || widget_cget(&g_wt, c)->parent != 0) {
                printf("SELF FAIL: tdd-unparent\n");
                bad = 1;
            }
            // ПОЛНЫЙ ПУТЬ СОБЫТИЯМИ: DOWN по строке + MOVE + UP на другой строке =
            // reparent + шаг истории. Клик без движения ветку НЕ тащит.
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_treeScroll = 0;
            g_treePress = 0;
            g_treeDrag = 0;
            ui_wundo_init(&g_undo);
            undo_commit("load");
            int r1 = wt_spawn(UI_W_PANEL);
            int r2 = wt_spawn(UI_W_BUTTON);
            undo_commit("add"); // снапшот ДО броска: откат вернёт оба корня
            tree_build();
            UiEvent te;
            float x1, y1, w1, h1, x2, y2, w2, h2;
            tree_rect(0, &x1, &y1, &w1, &h1);
            tree_rect(1, &x2, &y2, &w2, &h2);
            memset(&te, 0, sizeof te);
            te.kind = UI_EV_DOWN;
            te.x = x1 + w1 * 0.5f;
            te.y = y1 + h1 * 0.5f;
            g_mouseDown = 1;
            wt_event(&te);
            if (g_treePress != r1 || g_wtSel != r1) {
                printf("SELF FAIL: tdd-press %d sel %d\n", g_treePress, g_wtSel);
                bad = 1;
            }
            memset(&te, 0, sizeof te);
            te.kind = UI_EV_MOVE; // сдвиг 2px — порога драга нет
            te.x = x1 + w1 * 0.5f;
            te.y = y1 + h1 * 0.5f + 2.0f;
            wt_event(&te);
            if (g_treeDrag) {
                printf("SELF FAIL: tdd-thresh %d\n", g_treeDrag);
                bad = 1;
            }
            memset(&te, 0, sizeof te);
            te.kind = UI_EV_MOVE; // поехали: цель = строка r2, середина = внутрь
            te.x = x2 + w2 * 0.5f;
            te.y = y2 + h2 * 0.5f;
            wt_event(&te);
            if (g_treeDrag != r1 || g_treeDropId != r2 || g_treeDropMode != 0) {
                printf("SELF FAIL: tdd-drag %d %d %d\n", g_treeDrag, g_treeDropId,
                       g_treeDropMode);
                bad = 1;
            }
            int d0 = ui_wundo_depth(&g_undo);
            memset(&te, 0, sizeof te);
            te.kind = UI_EV_UP;
            te.x = x2 + w2 * 0.5f;
            te.y = y2 + h2 * 0.5f;
            g_mouseDown = 0;
            wt_event(&te);
            if (g_treeDrag || g_treeDropId != -1 || !widget_cget(&g_wt, r1) ||
                widget_cget(&g_wt, r1)->parent != r2 || g_wtSel != r1 ||
                ui_wundo_depth(&g_undo) != d0 + 1) {
                printf("SELF FAIL: tdd-drop drag=%d tgt=%d p=%d d=%d\n", g_treeDrag,
                       g_treeDropId, widget_cget(&g_wt, r1) ? widget_cget(&g_wt, r1)->parent
                                                            : -1,
                       ui_wundo_depth(&g_undo));
                bad = 1;
            }
            if (!wt_undo_apply(0) || widget_cget(&g_wt, r1)->parent != 0) {
                printf("SELF FAIL: tdd-undo\n");
                bad = 1;
            }
            // Esc во время драга: отмена — модель и история не тронуты.
            g_treePress = r2;
            g_treeDrag = r2;
            g_treeDropId = r1;
            int cnt0 = g_wt.count;
            int par0 = widget_cget(&g_wt, r2)->parent;
            int d1 = ui_wundo_depth(&g_undo);
            on_key(0, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
            if (g_treeDrag || g_treePress || g_treeDropId != -1 || g_quit ||
                g_wt.count != cnt0 || widget_cget(&g_wt, r2)->parent != par0 ||
                ui_wundo_depth(&g_undo) != d1) {
                printf("SELF FAIL: tdd-esc drag=%d quit=%d\n", g_treeDrag, g_quit);
                bad = 1;
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_treeScroll = 0;
        }
        // §14 F5h2: панели хрома лежат ПОВЕРХ холста. Тестер: клик по полосе
        // заголовка инспектора («возле названия») СХЛОПЫВАЛ все поля — выбор
        // сбрасывался, будто кликнули по пустому холсту; и «не возвращался»,
        // пока не выберешь виджет заново в дереве. Панель должна СЪЕДАТЬ клик.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            ed_chrome_sync();
            int ca = wt_spawn(UI_W_PANEL);
            g_wtSel = ca; // выбран: поля инспектора есть
            UiEvent ce;
            float px, py, pw, ph;
            // (1) полоса заголовка панели инспектора (над строкой имени)
            ed_rect(g_edIns, &px, &py, &pw, &ph);
            memset(&ce, 0, sizeof ce);
            ce.kind = UI_EV_DOWN;
            ce.x = px + 30;
            ce.y = py + 4;
            if (!wt_event(&ce) || g_wtSel != ca) {
                printf("SELF FAIL: chrome-click-head sel=%d want %d\n", g_wtSel, ca);
                bad = 1;
            }
            // (2) пустое место панели ниже полей
            memset(&ce, 0, sizeof ce);
            ce.kind = UI_EV_DOWN;
            ce.x = px + 20;
            ce.y = py + ph - 8;
            if (!wt_event(&ce) || g_wtSel != ca) {
                printf("SELF FAIL: chrome-click-ins-body sel=%d\n", g_wtSel);
                bad = 1;
            }
            // (3) пустое место списка дерева (ниже строк)
            ed_rect(g_edTree, &px, &py, &pw, &ph);
            memset(&ce, 0, sizeof ce);
            ce.kind = UI_EV_DOWN;
            ce.x = px + 20;
            ce.y = py + ph - 8;
            if (!wt_event(&ce) || g_wtSel != ca) {
                printf("SELF FAIL: chrome-click-tree-body sel=%d\n", g_wtSel);
                bad = 1;
            }
            // (4) поле палитры ПРАВЕЕ кнопок (пустая полоса панели)
            ed_rect(g_edPal, &px, &py, &pw, &ph);
            memset(&ce, 0, sizeof ce);
            ce.kind = UI_EV_DOWN;
            ce.x = px + pw - 3;
            ce.y = py + 60;
            if (!wt_event(&ce) || g_wtSel != ca) {
                printf("SELF FAIL: chrome-click-pal-body sel=%d\n", g_wtSel);
                bad = 1;
            }
            // (5) ХОЛСТ (плашка вьюпорта) — наоборот, снимает выбор (как было)
            memset(&ce, 0, sizeof ce);
            ce.kind = UI_EV_DOWN;
            ce.x = (float)(g_vpx + g_vpw / 2);
            ce.y = (float)(g_vpy + g_vph - 20);
            if (!wt_event(&ce) || g_wtSel != 0) {
                printf("SELF FAIL: chrome-click-canvas sel=%d\n", g_wtSel);
                bad = 1;
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F5j: ПЛАВАЮЩИЕ ПАНЕЛИ. Тестер: «хочу выдвинуть панель поверх всего».
        // Панель тащат за полосу ЗАГОЛОВКА — она отрывается от дока, уходит наверх
        // и живёт, где её положили; «dock» в её заголовке возвращает назад.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            for (int i = 0; i < ed_ws_panels(); i++) g_edOrder[i] = i;
            memset(g_edMoved, 0, sizeof g_edMoved);
            ed_chrome_init(); // вернуть хром на док-места после прошлых проб
            ed_chrome_sync();
            int ca = wt_spawn(UI_W_PANEL);
            g_wtSel = ca;
            float hx, hy, hw, hh;
            ed_panel_home(0, &hx, &hy, &hw, &hh);
            UiEvent fe;
            // (1) заголовок палитры = таск панели; панель уходит наверх
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_DOWN;
            fe.x = hx + 40;
            fe.y = hy + 4;
            if (!wt_event(&fe) || g_edGrab != 0 || g_wtSel != ca) {
                printf("SELF FAIL: float-grab grab=%d sel=%d\n", g_edGrab, g_wtSel);
                bad = 1;
            }
            if (g_edOrder[ed_ws_panels() - 1] != 0 || ed_panel_top(hx + 40, hy + 4) != 0) {
                printf("SELF FAIL: float-raise order=%d top=%d\n", g_edOrder[ed_ws_panels() - 1],
                       ed_panel_top(hx + 40, hy + 4));
                bad = 1;
            }
            {
                const UiWidget *wp = widget_cget(&g_edWt, g_edPal);
                const UiWidget *wt2 = widget_cget(&g_edWt, g_edTree);
                const UiWidget *wi = widget_cget(&g_edWt, g_edIns);
                if (!wp || !wt2 || !wi || !(wp->z > wt2->z && wp->z > wi->z)) {
                    printf("SELF FAIL: float-z pal=%d tree=%d ins=%d\n", wp ? wp->z : -1,
                           wt2 ? wt2->z : -1, wi ? wi->z : -1);
                    bad = 1;
                }
            }
            // (2) тащим вниз-вправо: панель поехала и стала «оторванной»
            g_mouseDown = 1;
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_MOVE;
            fe.x = hx + 140;
            fe.y = hy + 200;
            wt_event(&fe);
            g_mouseDown = 0;
            float fx, fy, fw, fh;
            ed_rect(g_edPal, &fx, &fy, &fw, &fh);
            if (!ed_panel_float(0) || fx != hx + 100 || fy != hy + 196) {
                printf("SELF FAIL: float-move x=%.0f y=%.0f float=%d\n", (double)fx,
                       (double)fy, ed_panel_float(0));
                bad = 1;
            }
            // (3) раскладка доков её больше НЕ возвращает
            ed_chrome_sync();
            ed_rect(g_edPal, &fx, &fy, &fw, &fh);
            if (fx != hx + 100 || fy != hy + 196) {
                printf("SELF FAIL: float-sync x=%.0f y=%.0f\n", (double)fx, (double)fy);
                bad = 1;
            }
            // (4) клик по телу плавающей панели не снимает выбор
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_DOWN;
            fe.x = fx + fw - 4;
            fe.y = fy + fh - 4;
            if (!wt_event(&fe) || g_wtSel != ca) {
                printf("SELF FAIL: float-consume sel=%d\n", g_wtSel);
                bad = 1;
            }
            // (5) кнопка «dock» возвращает панель в док
            float cx, cy, cw, ch;
            ed_float_chip(0, &cx, &cy, &cw, &ch);
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_DOWN;
            fe.x = cx + cw / 2;
            fe.y = cy + ch / 2;
            if (!wt_event(&fe) || ed_panel_float(0)) {
                printf("SELF FAIL: float-chip float=%d\n", ed_panel_float(0));
                bad = 1;
            }
            ed_rect(g_edPal, &fx, &fy, &fw, &fh);
            if (fx != hx || fy != hy) {
                printf("SELF FAIL: float-chip-home x=%.0f y=%.0f\n", (double)fx,
                       (double)fy);
                bad = 1;
            }
            // (6) бросок в СВОЁ док-место (док-зона) тоже возвращает панель
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_DOWN;
            fe.x = hx + 40;
            fe.y = hy + 4;
            wt_event(&fe);
            g_mouseDown = 1;
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_MOVE;
            fe.x = hx + 300;
            fe.y = hy + 240;
            wt_event(&fe); // утащили далеко -> оторвана
            if (!ed_panel_float(0)) {
                printf("SELF FAIL: float-away float=%d\n", ed_panel_float(0));
                bad = 1;
            }
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_MOVE;
            fe.x = hx + 40;
            fe.y = hy + 4;
            wt_event(&fe); // притащили домой
            g_mouseDown = 0;
            memset(&fe, 0, sizeof fe);
            fe.kind = UI_EV_UP;
            fe.x = hx + 40;
            fe.y = hy + 4;
            wt_event(&fe);
            if (ed_panel_float(0)) {
                printf("SELF FAIL: float-dock-zone float=%d\n", ed_panel_float(0));
                bad = 1;
            }
            ed_rect(g_edPal, &fx, &fy, &fw, &fh);
            if (fx != hx || fy != hy) {
                printf("SELF FAIL: float-dock-home x=%.0f y=%.0f\n", (double)fx,
                       (double)fy);
                bad = 1;
            }
            // (7) панель поверх ДРУГОЙ панели: клик забирает верхняя (палитра над
            // деревом: жмём по ряду палитры там, где под ним строка дерева).
            float trx, try_, trw, trh;
            tree_build();
            tree_rect(0, &trx, &try_, &trw, &trh);
            UiWidget *pp = widget_get(&g_edWt, g_edPal);
            if (pp) {
                pp->rect.anchors = UI_A_LEFT | UI_A_TOP;
                pp->rect.x = trx - PAL_PAD;
                pp->rect.y = try_ - (float)(PAL_PAD + PAL_TITLE);
                g_edMoved[g_edPal - 1] = 1; // оторвана: док не вернёт
            }
            ed_panel_raise(0); // и она верхняя
            float qx = trx + 20, qy = try_ + 6;
            if (ed_panel_top(qx, qy) != 0) {
                printf("SELF FAIL: float-top top=%d want 0\n", ed_panel_top(qx, qy));
                bad = 1;
            }
            {
                int before = g_wt.count;
                memset(&fe, 0, sizeof fe);
                fe.kind = UI_EV_DOWN;
                fe.x = qx;
                fe.y = qy;
                wt_event(&fe);
                if (g_wt.count != before + 1) { // спавн из палитры, а не выбор дерева
                    printf("SELF FAIL: float-top-hit cnt=%d want %d sel=%d\n", g_wt.count,
                           before + 1, g_wtSel);
                    bad = 1;
                }
            }
            ed_chrome_init();
            ed_chrome_sync();
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F5l: ГРУППА ЕДЕТ ЗА РОДИТЕЛЕМ. Тестер: «нажал убрать/поставить,
        // сдвинул — и у меня отвалился пополам виджет»: координаты абсолютные,
        // поэтому дети оставались на месте и панель «разваливалась».
        // Figma/UMG/WPF тянут поддерево за родителем — теперь и у нас.
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            ed_chrome_init();
            ed_chrome_sync();
            int p1 = wt_spawn(UI_W_PANEL);
            int b1 = wt_spawn(UI_W_BUTTON);
            if (!p1 || !b1 || !widget_reparent(&g_wt, b1, p1)) {
                printf("SELF FAIL: group-setup\n");
                bad = 1;
            }
            UiWidget *pw = widget_get(&g_wt, p1);
            UiWidget *bw = widget_get(&g_wt, b1);
            if (!wt_has_kids(p1) || wt_has_kids(b1)) {
                printf("SELF FAIL: group-has-kids\n");
                bad = 1;
            }
            float px0, py0, pw0, ph0, bx0, by0, bw0, bh0;
            doc_px(pw, &px0, &py0, &pw0, &ph0); // F5l(3): мышь и грязь — экран
            doc_px(bw, &bx0, &by0, &bw0, &bh0);
            float rel0 = (bx0 - px0) * 1000.0f + (by0 - py0);
            // (1) тащим РОДИТЕЛЯ за тело: дитя обязано ехать с ним
            UiEvent de;
            memset(&de, 0, sizeof de);
            de.kind = UI_EV_DOWN;
            de.x = px0 + 12;
            de.y = py0 + 12;
            g_mouseDown = 1;
            if (!wt_event(&de)) {
                printf("SELF FAIL: group-grab\n");
                bad = 1;
            }
            memset(&de, 0, sizeof de);
            de.kind = UI_EV_MOVE;
            de.x = px0 + 52;
            de.y = py0 + 36;
            wt_event(&de);
            float px1, py1, pw1, ph1, bx1, by1, bw1, bh1;
            doc_px(pw, &px1, &py1, &pw1, &ph1);
            doc_px(bw, &bx1, &by1, &bw1, &bh1);
            if (fabsf((px1 - px0) - 40.0f) > 0.5f || fabsf((py1 - py0) - 24.0f) > 0.5f ||
                fabsf((bx1 - bx0) - 40.0f) > 0.5f || fabsf((by1 - by0) - 24.0f) > 0.5f) {
                printf("SELF FAIL: group-move pal=%.0f,%.0f kid=%.0f,%.0f want +40,+24\n",
                       (double)(px1 - px0), (double)(py1 - py0), (double)(bx1 - bx0),
                       (double)(by1 - by0));
                bad = 1;
            }
            if (fabsf(((bx1 - px1) * 1000.0f + (by1 - py1)) - rel0) > 1.0f) {
                printf("SELF FAIL: group-rel moved\n"); // относительное не сохранилось
                bad = 1;
            }
            // (2) dirty-регион покрывает СТАРЫЕ и НОВЫЕ границы поддерева
            {
                int covL = 0, covR = 0;
                for (int i = 0; i < g_dirtyN; i++) {
                    int x0 = g_dirtyR[i][0], y0 = g_dirtyR[i][1];
                    int x1 = x0 + g_dirtyR[i][2], y1 = y0 + g_dirtyR[i][3];
                    if (x0 <= (int)bx0 && y0 <= (int)by0 && x1 >= (int)(bx0 + bw0) &&
                        y1 >= (int)(by0 + bh0))
                        covL = 1;
                    if (x0 <= (int)bx1 && y0 <= (int)by1 && x1 >= (int)(bx1 + bw1) &&
                        y1 >= (int)(by1 + bh1))
                        covR = 1;
                }
                if (!g_dirtyN || !covL || !covR) {
                    printf("SELF FAIL: group-dirty n=%d old=%d new=%d\n", g_dirtyN, covL, covR);
                    bad = 1;
                }
            }
            memset(&de, 0, sizeof de);
            de.kind = UI_EV_UP;
            de.x = px0 + 52;
            de.y = py0 + 36;
            g_mouseDown = 0;
            wt_event(&de);
            // (3) поле x инспектора тянет то же поддерево
            {
                g_wtSel = p1;
                g_insField = IF_X;
                g_insDownVal = pw->rect.x;
                g_insDownX = 200.0f;
                g_insPrevPx[0] = 0;
                g_insPrevPx[1] = 0;
                g_insPrevPx[2] = 0;
                g_insPrevPx[3] = 0;
                float cb0 = 0, cb1 = 0, cb2 = 0, cb3 = 0;
                doc_px(bw, &cb0, &cb1, &cb2, &cb3);
                float oldx = pw->rect.x;
                UiEvent ie;
                memset(&ie, 0, sizeof ie);
                ie.kind = UI_EV_MOVE;
                ie.x = 212.0f; // +12 px = +12 по x
                ie.y = 10.0f;
                wt_event(&ie);
                float cb4 = 0, cb5 = 0, cb6 = 0, cb7 = 0;
                doc_px(bw, &cb4, &cb5, &cb6, &cb7);
                float want = pw->rect.x - oldx;
                if (fabsf((cb4 - cb0) - want) > 0.5f) {
                    printf("SELF FAIL: group-ins kid=%.0f want=%.0f\n", (double)(cb4 - cb0),
                           (double)want);
                    bad = 1;
                }
                g_insField = -1;
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            ed_chrome_init();
            ed_chrome_sync();
        }
        // §14 F5l(3): ДОКУМЕНТ ВНУТРИ ВЬЮПОРТА (экран игры). Тестер: «нажал на
        // панель и зебра полетела» — в тесном окне виджет рождался ЗА границей
        // экрана, а документ рисовался поверх хрома редактора.
        {
            for (int i = 0; i < ed_ws_panels(); i++) g_edOrder[i] = i;
            memset(g_edMoved, 0, sizeof g_edMoved);
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            ed_chrome_init();
            ed_chrome_sync();
            // (1) вьюпорт = окно МИНУС полосы доков; экран игры в середине
            if (g_vpx != ED_DOCK_W + PAL_PAD ||
                g_vpx + g_vpw != g_WW - (ED_DOCK_W + PAL_PAD) ||
                g_vpy != ed_top() || g_vpw <= 0 || g_vph <= 0) {
                printf("SELF FAIL: dock-vp vp=%d,%d %dx%d\n", g_vpx, g_vpy, g_vpw, g_vph);
                bad = 1;
            }
            // (2) якорь R цепляется к краю ЭКРАНА ИГРЫ, а не окна
            UiWidgetRect rr;
            memset(&rr, 0, sizeof rr);
            rr.anchors = UI_A_RIGHT | UI_A_TOP;
            rr.x = 10;
            rr.y = 10;
            rr.w = 100;
            rr.h = 20;
            int rid = widget_add(&g_wt, UI_W_LABEL, "R", 0, &rr, 0);
            float rx, ry, rw, rh;
            doc_px(widget_cget(&g_wt, rid), &rx, &ry, &rw, &rh);
            if (fabsf((rx + rw) - (float)(g_vpx + g_vpw - 10)) > 0.5f) {
                printf("SELF FAIL: doc-anchor-r %.0f want %d\n", (double)(rx + rw),
                       g_vpx + g_vpw - 10);
                bad = 1;
            }
            // (3) спавн ложится В экран игры (раньше — за окно)
            int sid = wt_spawn(UI_W_PANEL);
            const UiWidget *swp = widget_cget(&g_wt, sid);
            if (!swp || swp->rect.x < 0.0f || swp->rect.y < 0.0f ||
                swp->rect.x + swp->rect.w > (float)doc_w() ||
                swp->rect.y + swp->rect.h > (float)doc_h()) {
                printf("SELF FAIL: spawn-in-vp %.0f,%.0f vp=%dx%d\n", (double)swp->rect.x,
                       (double)swp->rect.y, doc_w(), doc_h());
                bad = 1;
            }
            // (4) за вьюпортом документа НЕТ: выехавший виджет обрезан, клик по
            // доке документ не выбирает (раньше пиксели/клик уходили в хром)
            UiWidget *ew = widget_get(&g_wt, sid);
            ew->rect.x = -80.0f;
            ew->rect.y = 40.0f;
            RcVecShape dbuf[UI_MAX_PRIMS];
            int dn = 0;
            wt_draw_all(dbuf, &dn);
            int leak = 0;
            for (int i = 0; i < dn; i++) {
                if (dbuf[i].a < (float)g_vpx - 0.5f || dbuf[i].b < (float)g_vpy - 0.5f ||
                    dbuf[i].a + dbuf[i].c > (float)(g_vpx + g_vpw) + 0.5f ||
                    dbuf[i].b + dbuf[i].d > (float)(g_vpy + g_vph) + 0.5f)
                    leak++;
            }
            if (!dn || leak) {
                printf("SELF FAIL: doc-clip n=%d leak=%d\n", dn, leak);
                bad = 1;
            }
            // Точка ВНУТРИ экрана игры попадает в виджет; та же точка документа,
            // но левее вьюпорта (в доке) — не попадает (клип документа). Координаты
            // берём ОТ ВИДЖЕТА: хром/вьюпорт могут менять высоту, а константа тогда
            // перестаёт попадать в виджет (так и было, когда убрали легаси-полосы).
            {
                float sx, sy, sw, sh;
                doc_px(widget_cget(&g_wt, sid), &sx, &sy, &sw, &sh);
                float hy = sy + sh * 0.5f;      // середина виджета по вертикали
                float hxIn = sx + sw * 0.5f;    // внутри экрана игры
                float hxOut = sx + 20.0f;       // тот же виджет, но в доке (клип)
                if (!wt_hit(hxIn, hy) || wt_hit(hxOut, hy) ||
                    hxOut >= (float)g_vpx) {
                    printf("SELF FAIL: doc-hit-out %.0f,%.0f %.0f..%.0f vp=%d\n", (double)sx,
                           (double)sy, (double)hxIn, (double)hxOut, g_vpx);
                    bad = 1;
                }
            }
            // (5) тесное окно: доки и экран игры помещаются, а новый виджет
            // зажимается внутрь маленького экрана (раньше рождался за окном)
            {
                int kw = g_WW, kh = g_WH;
                g_WW = 640;
                g_WH = 360;
                ed_chrome_sync();
                if (g_vpx != ED_DOCK_W + PAL_PAD || g_vpw < 64 || g_vph < 64 ||
                    g_vpx + g_vpw > 640 - (ED_DOCK_W + PAL_PAD) + 1) {
                    printf("SELF FAIL: vp-narrow vp=%d,%d %dx%d\n", g_vpx, g_vpy, g_vpw,
                           g_vph);
                    bad = 1;
                }
                widget_tree_init(&g_wt); // count=0 -> спавн из центра экрана игры
                g_wtSel = 0;
                int nid = wt_spawn(UI_W_PANEL);
                const UiWidget *nw2 = widget_cget(&g_wt, nid);
                if (!nw2 || nw2->rect.x < 0.0f || nw2->rect.y < 0.0f ||
                    nw2->rect.x + nw2->rect.w > (float)doc_w() + 0.5f ||
                    nw2->rect.y + nw2->rect.h > (float)doc_h() + 0.5f) {
                    printf("SELF FAIL: spawn-narrow %.0f,%.0f %gx%g vp=%dx%d\n",
                           (double)nw2->rect.x, (double)nw2->rect.y, (double)nw2->rect.w,
                           (double)nw2->rect.h, doc_w(), doc_h());
                    bad = 1;
                }
                g_WW = kw;
                g_WH = kh;
                ed_chrome_sync();
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            ed_chrome_init();
            ed_chrome_sync();
        }
        // §14 F5k: ИНСТРУМЕНТЫ-ВКЛАДКИ. Тестер: «хотел, чтобы панели были
        // ИНСТРУМЕНТАМИ: открыл сам — вот они, закрыл — места не занимают»
        // (View в Word / панели в Roblox Studio). И панель не должна уползать за
        // окно: «вверх невидимая полоса, а влево/вправо/вниз уезжает за край».
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            for (int i = 0; i < ed_ws_panels(); i++) g_edOrder[i] = i;
            memset(g_edMoved, 0, sizeof g_edMoved);
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            ed_chrome_init();
            ed_chrome_sync();
            int ca = wt_spawn(UI_W_PANEL);
            g_wtSel = ca;
            float phx, phy, phw, phh, thx, thy, thw, thh;
            ed_panel_home(0, &phx, &phy, &phw, &phh);
            ed_panel_home(1, &thx, &thy, &thw, &thh);
            if (!(thy > phy)) { // палитра сверху, дерево под ней
                printf("SELF FAIL: tabs-stack pal=%.0f tree=%.0f\n", (double)phy,
                       (double)thy);
                bad = 1;
            }
            UiEvent te;
            // (1) галочка ленты VIEW «Palette» -> инструмент закрыт, виджета нет.
            // F5l(2): открытие/закрытие инструмента переехало из ЗАГОЛОВКА в
            // галочки ленты (View в Word / панели в Roblox Studio), а заголовок
            // теперь переключает САМУ ленту.
            if (!sel_clip_click("view.panel.palette") || g_edOpen[0]) {
                printf("SELF FAIL: tabs-close open=%d\n", g_edOpen[0]);
                bad = 1;
            }
            float zx, zy, zw, zh;
            ed_rect(g_edPal, &zx, &zy, &zw, &zh);
            if (zw != 0 || zh != 0) {
                printf("SELF FAIL: tabs-close-rect %.0fx%.0f\n", (double)zw, (double)zh);
                bad = 1;
            }
            if (pal_hit(phx + 20, phy + PAL_PAD + PAL_TITLE + 10) >= 0) {
                printf("SELF FAIL: tabs-close-hit\n"); // закрытый не отвечает
                bad = 1;
            }
            // (2) место ПЕРЕТЕКЛО: дерево встало на место палитры
            ed_panel_home(1, &thx, &thy, &thw, &thh);
            if (thy != phy) {
                printf("SELF FAIL: tabs-reflow tree=%.0f want %.0f\n", (double)thy,
                       (double)phy);
                bad = 1;
            }
            // (3) галочка ещё раз -> открылась и ВЕРНУЛАСЬ В ДОК
            sel_clip_click("view.panel.palette");
            ed_rect(g_edPal, &zx, &zy, &zw, &zh);
            if (!g_edOpen[0] || zx != phx || zy != phy) {
                printf("SELF FAIL: tabs-open open=%d at=%.0f,%.0f want=%.0f,%.0f\n",
                       g_edOpen[0], (double)zx, (double)zy, (double)phx, (double)phy);
                bad = 1;
            }
            // (4) панель не выезжает за окно НИ ОДНОЙ стороной
            {
                UiWidget *pp = widget_get(&g_edWt, g_edPal);
                memset(&te, 0, sizeof te);
                te.kind = UI_EV_DOWN;
                te.x = phx + 20;
                te.y = phy + 4;
                wt_event(&te); // взяли за заголовок
                const float far[4][2] = {{9999.0f, -9999.0f},
                                         {-9999.0f, 9999.0f},
                                         {9999.0f, 9999.0f},
                                         {-9999.0f, -9999.0f}};
                for (int t2 = 0; t2 < 4; t2++) {
                    g_mouseDown = 1;
                    memset(&te, 0, sizeof te);
                    te.kind = UI_EV_MOVE;
                    te.x = far[t2][0];
                    te.y = far[t2][1];
                    wt_event(&te);
                    g_mouseDown = 0;
                    if (!pp || pp->rect.x < 0 || pp->rect.y < 0 ||
                        pp->rect.x + pp->rect.w > (float)g_WW ||
                        pp->rect.y + pp->rect.h > (float)g_WH) {
                        printf("SELF FAIL: tabs-clamp%d at=%.0f,%.0f %.0fx%.0f\n", t2,
                               pp ? (double)pp->rect.x : -1.0,
                               pp ? (double)pp->rect.y : -1.0, pp ? (double)pp->rect.w : 0.0,
                               pp ? (double)pp->rect.h : 0.0);
                        bad = 1;
                    }
                }
                memset(&te, 0, sizeof te);
                te.kind = UI_EV_UP;
                te.x = phx + 20;
                te.y = phy + 4;
                wt_event(&te);
            }
            // (5) пустое место полосы вкладок = хром: выбор не роняется
            {
                float bx, by, bw2, bh2;
                tabs_rect(&bx, &by, &bw2, &bh2);
                memset(&te, 0, sizeof te);
                te.kind = UI_EV_DOWN;
                te.x = bx + bw2 - 20;
                te.y = by + 8;
                if (!wt_event(&te) || g_wtSel != ca) {
                    printf("SELF FAIL: tabs-consume sel=%d\n", g_wtSel);
                    bad = 1;
                }
            }
            // (6) текст под панелью не пишем: легаси-хром рисует свой текст
            // вслепую ОТДЕЛЬНЫМ проходом поверх фигур и просвечивал сквозь окно
            {
                float occ[1][4];
                ed_rect(g_edPal, &occ[0][0], &occ[0][1], &occ[0][2], &occ[0][3]);
                ui_text_occluders(occ, 1);
                int in = ui_text_occluded(occ[0][0] + 10.0f, occ[0][1] + 4.0f);
                int out = ui_text_occluded(occ[0][0] - 40.0f, occ[0][1] + 4.0f);
                ui_text_occluders(0, 0);
                if (!in || out) {
                    printf("SELF FAIL: tabs-occlude in=%d out=%d\n", in, out);
                    bad = 1;
                }
            }
            // (6) «открыл сам» переживает запуск: своё хранилище (не документ)
            {
                setenv("UI_EDIT_BENCH", "1", 1);
                int keep[3] = {g_edOpen[0], g_edOpen[1], g_edOpen[2]};
                g_edOpen[0] = 1;
                g_edOpen[1] = 0;
                g_edOpen[2] = 1;
                ed_panels_save();
                g_edOpen[0] = 0;
                g_edOpen[1] = 1;
                g_edOpen[2] = 0;
                ed_panels_load();
                if (g_edOpen[0] != 1 || g_edOpen[1] != 0 || g_edOpen[2] != 1) {
                    printf("SELF FAIL: tabs-persist %d%d%d\n", g_edOpen[0], g_edOpen[1],
                           g_edOpen[2]);
                    bad = 1;
                }
                remove(ED_PANELS_BENCH);
                unsetenv("UI_EDIT_BENCH");
                g_edOpen[0] = keep[0];
                g_edOpen[1] = keep[1];
                g_edOpen[2] = keep[2];
            }
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            ed_chrome_init();
            ed_chrome_sync();
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // ====================================================================
        // §14 F5l(2): ЛЕНТА = РЕЕСТР ДЕЙСТВИЙ. Тестер: «таб-полоса должна быть
        // лентой с группой кнопок на табе + галочки Вид». Проверяем ЧИСЛАМИ:
        // реестр (id уникальны, метки непусты, каждая лента непуста), гейт
        // canExecute (выключенное не срабатывает), галочку View (закрыл — места
        // нет, вьюпорт шире), спавн кнопкой ленты, отсутствие двойных клавиш,
        // шпаргалку (строк = действиям, F1/Esc), тултип и персист активной ленты.
        // ====================================================================
        {
            g_WW = 1280;
            g_WH = 720;
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            g_edTab = 0;
            g_edShowKeys = 0;
            g_rbHot = -1;
            for (int i = 0; i < ed_ws_panels(); i++) g_edOrder[i] = i;
            memset(g_edMoved, 0, sizeof g_edMoved);
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            ed_chrome_init();
            ed_chrome_sync();
            ui_wundo_init(&g_undo);
            undo_commit("load"); // нулевое состояние истории — как при старте редактора
            // (1) реестр корректен: id уникальны, метки непусты, каждая лента непуста
            {
                int dup = 0, empty = 0, perTab[ED_TABS_MAX];
                for (int t = 0; t < ed_ws_tabs(); t++) perTab[t] = 0;
                for (int i = 0; i < ed_cmd_n(); i++) {
                    const EdCmd *a = &ED_ACT[i];
                    if (!a->id[0] || !a->label[0] || !a->group[0]) empty++;
                    if ((int)a->tab >= ed_ws_tabs()) empty++;
                    else perTab[a->tab]++;
                    for (int j = i + 1; j < ed_cmd_n(); j++)
                        if (strcmp(ED_ACT[i].id, ED_ACT[j].id) == 0) dup++;
                }
                for (int t = 0; t < ed_ws_tabs(); t++)
                    if (perTab[t] == 0) empty++;
                if (ed_cmd_n() < 10 || dup || empty) {
                    printf("SELF FAIL: ribbon-registry n=%d dup=%d empty=%d\n", ed_cmd_n(), dup,
                           empty);
                    bad = 1;
                }
            }
            // (1c) ТАБЛИЦА РЕЖИМА (A3-1): id/подпись/док каждой панели валидны, и
            // число записей-панелей совпадает с числом виджетов-панелей хрома
            // (источник числа — таблица, а не литерал в циклах).
            {
                int pn = ed_ws_panels(), badw = 0, badd = 0, cnt = 0;
                for (int i = 0; i < pn; i++) {
                    const EdPanelDesc *p = ed_ws_panel(i);
                    if (!p || !p->id || !p->id[0] || !ed_ws_caption(i)[0]) badw++;
                    int d = ed_ws_dock(i);
                    if (d != ED_DK_LEFT && d != ED_DK_RIGHT && d != ED_DK_BOTTOM) badd++;
                }
                for (int i = 0; i < ED_PANEL_MAX; i++)
                    if (g_edPanels[i] > 0) cnt++;
                if (pn < 1 || pn > ED_PANEL_MAX || ed_ws_tabs() < 1 || badw || badd ||
                    cnt != pn) {
                    printf("SELF FAIL: ws-table pn=%d tabs=%d badw=%d badd=%d widgets=%d\n",
                           pn, ed_ws_tabs(), badw, badd, cnt);
                    bad = 1;
                }
            }
            // (1b) ПАЛИТРА — вид ТОГО ЖЕ реестра (второго списка строк нет):
            // строки/метки = записи с видом ED_V_PALETTE, имена типов — из записей
            // создания. Раньше это были отдельные PAL_LABEL/PAL_TYPE (дубль).
            {
                int ok = ed_pal_n() == 4;
                if (ok) {
                    if (strcmp(ed_pal_label(0), "Panel") || strcmp(ed_pal_label(1), "Button") ||
                        strcmp(ed_pal_label(2), "Label") || strcmp(ed_pal_label(3), "Delete"))
                        ok = 0;
                    if (ed_pal_is_action(0) || ed_pal_is_action(1) || ed_pal_is_action(2) ||
                        !ed_pal_is_action(3))
                        ok = 0;
                    if (strcmp(ed_pal_label(2), wt_type_name(UI_W_LABEL)) != 0) ok = 0;
                    if (strcmp(wt_type_name(UI_W_PANEL), "Panel") ||
                        strcmp(wt_type_name(99), "?"))
                        ok = 0;
                }
                if (!ok) {
                    printf("SELF FAIL: ribbon-palette n=%d l=%s,%s,%s,%s\n", ed_pal_n(),
                           ed_pal_label(0), ed_pal_label(1), ed_pal_label(2), ed_pal_label(3));
                    bad = 1;
                }
            }
            // (2) canExecute = canExecute: ОДНА точка правды «доступно сейчас», и она
            // ЖИВАЯ (меняется вместе с документом), а не таблица констант
            {
                int sel0 = ed_cmd_enabled(ed_cmd_find("edit.rename"));
                int del0 = ed_cmd_enabled(ed_cmd_find("edit.delete"));
                int undo0 = ed_cmd_enabled(ed_cmd_find("hist.undo"));
                int pal0 = ed_cmd_enabled(ed_cmd_find("view.panel.palette"));
                int self0 = ed_cmd_enabled(ed_cmd_find("tools.self"));
                if (sel0 || del0 || undo0 || !pal0 || !self0 || ed_cmd_on(ed_cmd_find("tools.self"))) {
                    printf("SELF FAIL: ribbon-gate sel=%d del=%d undo=%d pal=%d self=%d\n", sel0,
                           del0, undo0, pal0, self0);
                    bad = 1;
                }
                int w1 = wt_spawn(UI_W_BUTTON); // выбрали + появилась история -> гейт открылся
                g_wtSel = w1;
                undo_commit("add");
                int sel1 = ed_cmd_enabled(ed_cmd_find("edit.rename"));
                int del1 = ed_cmd_enabled(ed_cmd_find("edit.delete"));
                int undo1 = ed_cmd_enabled(ed_cmd_find("hist.undo"));
                int redo1 = ed_cmd_enabled(ed_cmd_find("hist.redo"));
                if (!sel1 || !del1 || !undo1 || redo1) {
                    printf("SELF FAIL: ribbon-gate-live sel=%d del=%d undo=%d redo=%d\n", sel1,
                           del1, undo1, redo1);
                    bad = 1;
                }
                // (2b) A2: ДОКУМЕНТ — интерфейс ХОСТА. Команды hist./edit. ходят
                // через ed_doc и про виджеты не знают. Числами: у АКТИВНОГО
                // документа есть undo/redo/выделение/сохранение, а без
                // активного документа закрыто ВСЁ (не «падает», а честно 0).
                {
                    int n1 = g_wt.count;
                    int okDoc = ed_doc_active() && strcmp(ed_doc_kind(), "ui") == 0 &&
                                strcmp(ed_doc_title(), "UI") == 0 && ed_doc_can_undo() &&
                                ed_doc_has_sel() && !ed_doc_can_redo() && ed_doc_save();
                    int okUndo = ed_doc_undo() && g_wt.count == n1 - 1 && ed_doc_can_redo();
                    int okRedo = ed_doc_redo() && g_wt.count == n1;
                    ed_doc_active_set(-1); // документа нет: закрыто ВСЁ
                    int okNo = !ed_doc_can_undo() && !ed_doc_has_sel() && !ed_doc_undo() &&
                               !ed_doc_del_sel() && !ed_doc_rename_sel() && !ed_doc_active();
                    ed_doc_active_set(0);
                    g_wtSel = w1; // вернуть выбор: дальше по коду он ожидается
                    if (!okDoc || !okUndo || !okRedo || !okNo || g_wt.count != n1) {
                        printf("SELF FAIL: doc-iface doc=%d undo=%d redo=%d no=%d n=%d\n",
                               okDoc, okUndo, okRedo, okNo, g_wt.count);
                        bad = 1;
                    }
                }
            }
            // (3) заголовок ленты ПЕРЕКЛЮЧАЕТ ленту (а не панель), кнопки — ниже
            // полосы заголовков (в свою полосу). И НИ ОДНОЙ КНОПКИ-КОПИИ: команда,
            // у которой есть строка палитры, в ленте не рисуется (жалоба тестера
            // 18.10: «нажимаю Panel, иду в UI — почему тут опять Panel?»).
            {
                float tx, ty, tw, th, p2x, p2y, p2w, p2h, p3x, p3y, p3w, p3h;
                UiEvent te;
                tab_rect(2, &tx, &ty, &tw, &th); // VIEW (0 HOME, 1 MODEL)
                memset(&te, 0, sizeof te);
                te.kind = UI_EV_DOWN;
                te.x = tx + 20;
                te.y = ty + 5;
                if (!wt_event(&te) || g_edTab != 2) {
                    printf("SELF FAIL: ribbon-tab tab=%d\n", g_edTab);
                    bad = 1;
                }
                ed_ribbon_layout();
                int uin = g_rbN;
                ed_ribbon_item_rect(ed_cmd_find("view.panel.palette"), &tx, &ty, &tw, &th);
                if (uin != 4 || tw <= 0 || ty < (float)(ED_TOP_CHROME + ED_TABS_H) ||
                    ty + th > (float)ed_top()) {
                    printf("SELF FAIL: ribbon-tab-items n=%d rect=%.0f,%.0f %.0fx%.0f\n", uin,
                           (double)tx, (double)ty, (double)tw, (double)th);
                    bad = 1;
                }
                // ДЕДУП: запись со строкой палитры не рисуется НИ НА ОДНОЙ ленте, а
                // запись без строки палитры на своей ленте НАРИСОВАНА (обе стороны).
                int dupBtn = 0, missing = 0;
                for (int t = 0; t < ed_ws_tabs(); t++) {
                    g_edTab = t;
                    ed_ribbon_layout();
                    for (int i = 0; i < ed_cmd_n(); i++) {
                        float ix, iy, iw, ih;
                        ed_ribbon_item_rect(i, &ix, &iy, &iw, &ih);
                        int inRb = ed_cmd_ribbon(i); // предикат — тот же, что у ленты
                        if (!inRb && iw > 0) dupBtn++;
                        if (inRb && (int)ED_ACT[i].tab == t && iw <= 0) missing++;
                    }
                }
                if (dupBtn || missing || strcmp(wt_type_name(UI_W_BUTTON), "Button") != 0) {
                    printf("SELF FAIL: ribbon-nodup dup=%d missing=%d\n", dupBtn, missing);
                    bad = 1;
                }
                // другая лента = ДРУГОЙ НАБОР кнопок (лента, а не одна полоса)
                g_edTab = 0; // HOME
                ed_ribbon_layout();
                int hnn = g_rbN;
                ed_ribbon_item_rect(ed_cmd_find("edit.rename"), &tx, &ty, &tw, &th);
                ed_ribbon_item_rect(ed_cmd_find("view.panel.palette"), &p2x, &p2y, &p2w, &p2h);
                ed_ribbon_item_rect(ed_cmd_find("ui.spawn.button"), &p3x, &p3y, &p3w, &p3h);
                if (hnn != 5 || tw <= 0 || p2w != 0 || p3w != 0) {
                    printf("SELF FAIL: ribbon-tab-set home=%d rename_w=%.0f pal_w=%.0f "
                           "spawn_w=%.0f\n",
                           hnn, (double)tw, (double)p2w, (double)p3w);
                    bad = 1;
                }
                // MODEL — лента ДРУГОГО ДОМЕНА (игровая сцена, не UI редактора): на ней
                // только записи ED_DM_SCENE, ровно четыре РЕЖИМА инструмента (radio) и
                // ни одной кнопки фронта. Тестер: «для самого редактора отдельно ...
                // разделить, но чтобы функционал сохранился».
                g_edTab = 1; // MODEL
                ed_ribbon_layout();
                int mscn = g_rbN, domOk = 1, toolOk = 1;
                for (int s = 0; s < mscn; s++) {
                    const EdCmd *ma = &ED_ACT[g_rbAct[s]];
                    if (ma->domain != ED_DM_SCENE) domOk = 0;
                    if (ma->kind != ED_K_TOOL) toolOk = 0;
                }
                ed_ribbon_item_rect(ed_cmd_find("scene.move"), &tx, &ty, &tw, &th);
                ed_ribbon_item_rect(ed_cmd_find("ui.spawn.button"), &p2x, &p2y, &p2w, &p2h);
                // и в обратную сторону: на фронте нет инструментов сцены
                int frontHasScene = 0;
                g_edTab = 0;
                ed_ribbon_layout();
                for (int s = 0; s < g_rbN; s++)
                    if (ED_ACT[g_rbAct[s]].domain == ED_DM_SCENE) frontHasScene++;
                if (mscn != 4 || !domOk || !toolOk || tw <= 0 || p2w != 0 || frontHasScene) {
                    printf("SELF FAIL: ribbon-model n=%d dom=%d tool=%d w=%.0f front_w=%.0f "
                           "mixed=%d\n",
                           mscn, domOk, toolOk, (double)tw, (double)p2w, frontHasScene);
                    bad = 1;
                }
                g_edTab = 0;
            }
            // (3c) СПЛИТТЕРЫ ДОКОВ (F5l(4b)): ширина дока тянется мышью, вьюпорт
            // = остаток; клампы держат И панель, И вьюпорт; размер переживает
            // запуск (ed_panels.json). Ширины вернём к дефолту — дальше тесты
            // считают от ED_DOCK_W.
            {
                int keepOpen[3] = {g_edOpen[0], g_edOpen[1], g_edOpen[2]};
                g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1; // оба дока на месте
                g_dockWl = g_dockWr = ED_DOCK_W;
                ed_viewport_compute();
                int vx0 = g_vpx, vw0 = g_vpw;
                int maxw = g_WW - (g_dockWr + PAL_PAD) - PAL_PAD - 64;
                float sx, sw, sy, sh;
                int which = -1;
                // попадание: левый/правый край дока — сплиттер, середина — НЕТ
                split_rect(1, &sx, &sy, &sw, &sh);
                int hitL = split_hit(sx + sw * 0.5f, (float)(ed_top() + 40), &which);
                split_rect(2, &sx, &sy, &sw, &sh);
                int hitR = split_hit(sx + sw * 0.5f, (float)(ed_top() + 40), &which);
                int hitMid = split_hit((float)(g_vpx + g_vpw / 2), (float)(ed_top() + 40),
                                       &which);
                if (!hitL || !hitR || hitMid || g_vpx != ED_DOCK_W + PAL_PAD) {
                    printf("SELF FAIL: dock-hit L=%d R=%d mid=%d vpx=%d\n", hitL, hitR, hitMid,
                           g_vpx);
                    bad = 1;
                }
                // ЛЕНТА — не зона сплиттера (иначе клик по вкладке резал бы док)
                split_rect(1, &sx, &sy, &sw, &sh);
                if (split_hit(sx + sw * 0.5f, 5.0f, &which)) {
                    printf("SELF FAIL: dock-hit-ribbon\n");
                    bad = 1;
                }
                // драг левого сплиттера +60: док шире, вьюпорт уехал и сузился
                UiEvent de;
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_DOWN;
                split_rect(1, &sx, &sy, &sw, &sh);
                de.x = sx + sw * 0.5f;
                de.y = (float)(ed_top() + 40);
                g_mouseDown = 1;
                route_event(&de);
                if (g_split != 1) {
                    printf("SELF FAIL: dock-grab %d\n", g_split);
                    bad = 1;
                }
                float grabx = de.x;
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_MOVE;
                de.x = grabx + 60.0f;
                de.y = (float)(ed_top() + 40);
                route_event(&de);
                if (g_dockWl != ED_DOCK_W + 60 || g_vpx != vx0 + 60 || g_vpw != vw0 - 60) {
                    printf("SELF FAIL: dock-drag w=%d vpx=%d vpw=%d\n", g_dockWl, g_vpx,
                           g_vpw);
                    bad = 1;
                }
                // кламп ВПРАВО: вьюпорт не схлопывается (док упирается в границу)
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_MOVE;
                de.x = (float)g_WW + 5000.0f;
                de.y = (float)(ed_top() + 40);
                route_event(&de);
                if (g_dockWl != maxw || g_vpw < 64) {
                    printf("SELF FAIL: dock-clamp w=%d max=%d vpw=%d\n", g_dockWl, maxw,
                           g_vpw);
                    bad = 1;
                }
                // кламп ВЛЕВО: панель не уже минимума (читаемость важнее «широкого»)
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_MOVE;
                de.x = -5000.0f;
                de.y = (float)(ed_top() + 40);
                route_event(&de);
                if (g_dockWl != ED_DOCKWIN_MIN) {
                    printf("SELF FAIL: dock-min w=%d\n", g_dockWl);
                    bad = 1;
                }
                // UP = конец операции: размер уходит в файл состояния инструмента
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_MOVE;
                de.x = (float)(vx0 + 33.0f);
                de.y = (float)(ed_top() + 40);
                route_event(&de);
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_UP;
                g_mouseDown = 0;
                route_event(&de);
                int want = g_dockWl;
                if (g_split != 0 || want == ED_DOCK_W) {
                    printf("SELF FAIL: dock-up split=%d w=%d\n", g_split, want);
                    bad = 1;
                }
                setenv("UI_EDIT_BENCH", "1", 1);
                ed_panels_save();
                g_dockWl = ED_DOCK_W; // сброс: «прочиталось» не спутать с дефолтом
                ed_panels_load();
                if (g_dockWl != want || g_dockWr != ED_DOCK_W) {
                    printf("SELF FAIL: dock-persist w=%d want %d r=%d\n", g_dockWl, want,
                           g_dockWr);
                    bad = 1;
                }
                { // БИТЫЙ файл: 99999 в dockL -> кламп ПАРОЙ, а не панель во всё окно
                    FILE *f = fopen(ED_PANELS_BENCH, "wb");
                    if (f) {
                        fprintf(f, "{\n \"edOpen\": [1,1,1],\n \"dockL\": 99999,\n"
                                   " \"dockR\": 1\n}\n");
                        fclose(f);
                    }
                    int room = g_WW - 2 * PAL_PAD - 64; // оба дока открыты
                    ed_panels_load();
                    int vpx2 = 0, vpw2 = 0;
                    ed_viewport_compute();
                    vpx2 = g_vpx;
                    vpw2 = g_vpw;
                    if (g_dockWl != room - ED_DOCKWIN_MIN || g_dockWr != ED_DOCKWIN_MIN ||
                        vpw2 < 64 || g_dockWl + g_dockWr > room || vpx2 + vpw2 > g_WW) {
                        printf("SELF FAIL: dock-load-clamp L=%d R=%d room=%d vpw=%d\n",
                               g_dockWl, g_dockWr, room, vpw2);
                        bad = 1;
                    }
                }
                remove(ED_PANELS_BENCH);
                unsetenv("UI_EDIT_BENCH");
                g_dockWl = g_dockWr = ED_DOCK_W; // дальше тесты считают от дефолта
                g_edOpen[0] = keepOpen[0];
                g_edOpen[1] = keepOpen[1];
                g_edOpen[2] = keepOpen[2];
                ed_viewport_compute();
            }
            // (3d) КОНСОЛЬ (Ш1c): кольцо журнала + НИЖНИЙ док. Данные — ui_log
            // (одна реализация на всех), вид — панель консоли: строки, прокрутка,
            // кнопка Clear. Панель — ЧЕТВЁРТАЯ в том же механизме (доки/сплиттеры/
            // персист), а не второй вид «панели».
            {
                int keepOpen3 = g_edOpen[3];
                int keepH = g_dockH;
                // (1) кольцо: старые вытесняются, порядок «старая -> новая»
                ui_log_clear();
                for (int i = 0; i < UI_LOG_LINES + 44; i++) ui_log_add("line %d", i);
                if (ui_log_n() != UI_LOG_LINES ||
                    strcmp(ui_log_line(0), "line 44") != 0 ||
                    strcmp(ui_log_line(UI_LOG_LINES - 1), "line 299") != 0 ||
                    ui_log_total() != (unsigned)(UI_LOG_LINES + 44)) {
                    printf("SELF FAIL: con-ring n=%d l0=%s llast=%s tot=%u\n", ui_log_n(),
                           ui_log_line(0), ui_log_line(UI_LOG_LINES - 1), ui_log_total());
                    bad = 1;
                }
                // (2) тумблер: лента VIEW (Ctrl+4) -> нижний док занят, экран игры
                // сузился по высоте; закрытая — ни пикселя, ни хитбокса
                g_edOpen[3] = 0;
                ed_chrome_sync();
                int vph0 = g_vph;
                if (g_dockB != 0 || ed_live(3)) {
                    printf("SELF FAIL: con-closed dockB=%d live=%d\n", g_dockB, ed_live(3));
                    bad = 1;
                }
                if (!sel_clip_click("view.panel.console") || !g_edOpen[3]) {
                    printf("SELF FAIL: con-toggle open=%d\n", g_edOpen[3]);
                    bad = 1;
                }
                ed_chrome_sync();
                if (g_dockB != g_dockH + PAL_PAD || !ed_live(3) ||
                    g_vph != vph0 - g_dockB || g_vph != g_WH - ed_top() - g_dockB) {
                    printf("SELF FAIL: con-open dockB=%d h=%d vph=%d\n", g_dockB, g_dockH,
                           g_vph);
                    bad = 1;
                }
                // (3) сплиттер НИЖНЕГО дока — тот же механизм (i=3), но тянут ВЫСОТУ
                {
                    float sx, sy, sw, sh;
                    split_rect(3, &sx, &sy, &sw, &sh);
                    int which = -1;
                    if (!split_hit(sx + sw * 0.5f, sy + sh * 0.5f, &which) || which != 3) {
                        printf("SELF FAIL: con-split-hit which=%d\n", which);
                        bad = 1;
                    }
                    UiEvent de;
                    float grabY = sy + sh * 0.5f;
                    memset(&de, 0, sizeof de);
                    de.kind = UI_EV_DOWN;
                    de.x = sx + sw * 0.5f;
                    de.y = grabY;
                    g_mouseDown = 1;
                    route_event(&de);
                    if (g_split != 3) {
                        printf("SELF FAIL: con-split-grab %d\n", g_split);
                        bad = 1;
                    }
                    int h0 = g_dockH, vph1 = g_vph;
                    memset(&de, 0, sizeof de);
                    de.kind = UI_EV_MOVE;
                    de.x = sx + sw * 0.5f;
                    de.y = grabY - 40.0f; // тянем ВВЕРХ = панель выше, экран игры ниже
                    route_event(&de);
                    if (g_dockH != h0 + 40 || g_vph != vph1 - 40) {
                        printf("SELF FAIL: con-split-drag h=%d vph=%d\n", g_dockH, g_vph);
                        bad = 1;
                    }
                    memset(&de, 0, sizeof de);
                    de.kind = UI_EV_MOVE;
                    de.x = sx + sw * 0.5f;
                    de.y = 0.0f; // за верх: кламп по минимальному вьюпорту
                    route_event(&de);
                    if (g_dockH > g_WH - ed_top() - 64 - PAL_PAD || g_vph < 64) {
                        printf("SELF FAIL: con-split-clamp h=%d vph=%d\n", g_dockH, g_vph);
                        bad = 1;
                    }
                    memset(&de, 0, sizeof de);
                    de.kind = UI_EV_UP;
                    g_mouseDown = 0;
                    route_event(&de);
                    if (g_split != 0) {
                        printf("SELF FAIL: con-split-up %d\n", g_split);
                        bad = 1;
                    }
                    // (4) персист: высота нижнего дока и открытость консоли
                    setenv("UI_EDIT_BENCH", "1", 1);
                    int want = g_dockH;
                    ed_panels_save();
                    g_dockH = 123;
                    g_edOpen[3] = 0;
                    ed_panels_load();
                    if (g_dockH != want || !g_edOpen[3]) {
                        printf("SELF FAIL: con-persist h=%d want=%d open=%d\n", g_dockH,
                               want, g_edOpen[3]);
                        bad = 1;
                    }
                    { // битая высота: кламп, панель не съедает экран игры
                        FILE *f = fopen(ED_PANELS_BENCH, "wb");
                        if (f) {
                            fprintf(f, "{\n \"edOpen\": [1,1,1,1],\n \"dockH\": 99999\n}\n");
                            fclose(f);
                        }
                        ed_panels_load();
                        ed_viewport_compute();
                        if (g_dockH != g_WH - ed_top() - 64 - PAL_PAD || g_vph < 64) {
                            printf("SELF FAIL: con-load-clamp h=%d vph=%d\n", g_dockH, g_vph);
                            bad = 1;
                        }
                    }
                    remove(ED_PANELS_BENCH);
                    unsetenv("UI_EDIT_BENCH");
                }
                // (5) видимый срез и прокрутка: хвост внизу; «кручу в пустоту» — 0
                g_dockH = keepH;
                ed_chrome_sync();
                {
                    int start = 0, rows = 0;
                    g_conScroll = 0;
                    con_view(&start, &rows);
                    if (rows <= 0 || start != ui_log_n() - rows) {
                        printf("SELF FAIL: con-view start=%d rows=%d n=%d\n", start, rows,
                               ui_log_n());
                        bad = 1;
                    }
                    g_conScroll = 100000;
                    con_view(&start, &rows);
                    if (start != 0) {
                        printf("SELF FAIL: con-scroll-clamp start=%d\n", start);
                        bad = 1;
                    }
                    UiEvent we;
                    memset(&we, 0, sizeof we);
                    we.kind = UI_EV_WHEEL;
                    we.value = 1.0;
                    float px, py;
                    con_row_rect(0, &px, &py);
                    we.x = px + 4;
                    we.y = py + 4;
                    g_conScroll = 0;
                    int handled = wt_event(&we);
                    if (!handled || g_conScroll <= 0) {
                        printf("SELF FAIL: con-wheel h=%d scroll=%d\n", handled, g_conScroll);
                        bad = 1;
                    }
                }
                // (6) Clear: ЕДИНСТВЕННОЕ место команды — кнопка в шапке (в ленте её
                // нет); клик по кнопке чистит журнал
                {
                    int ai = ed_cmd_find("console.clear");
                    float x, y, w, h;
                    g_edTab = ED_ACT[ai].tab;
                    ed_ribbon_layout();
                    ed_ribbon_item_rect(ai, &x, &y, &w, &h);
                    if (ai < 0 || !(ED_ACT[ai].views & ED_V_DOCK) || w != 0) {
                        printf("SELF FAIL: con-clear-view ai=%d w=%.0f\n", ai, (double)w);
                        bad = 1;
                    }
                    float cx, cy, cw, ch;
                    con_clear_chip(&cx, &cy, &cw, &ch);
                    UiEvent ce;
                    memset(&ce, 0, sizeof ce);
                    ce.kind = UI_EV_DOWN;
                    ce.x = cx + cw * 0.5f;
                    ce.y = cy + ch * 0.5f;
                    if (!wt_event(&ce) || ui_log_n() != 0) {
                        printf("SELF FAIL: con-clear n=%d\n", ui_log_n());
                        bad = 1;
                    }
                }
                // (7) журнал ПИШУТ действия: создание/удаление виджета оставляют след
                {
                    ui_log_clear();
                    widget_tree_init(&g_wt);
                    g_wtSel = 0;
                    g_act = &g_wt;
                    if (!sel_pal_click("ui.spawn.button") || ui_log_n() != 1 ||
                        !g_wtSel || !strstr(ui_log_line(0), "Button")) {
                        printf("SELF FAIL: con-log-add n=%d sel=%d l=%s\n", ui_log_n(), g_wtSel,
                               ui_log_n() ? ui_log_line(0) : "-");
                        bad = 1;
                    }
                    int n1 = ui_log_n();
                    if (!sel_pal_click("edit.delete") || ui_log_n() <= n1 ||
                        !strstr(ui_log_line(ui_log_n() - 1), "delete")) {
                        printf("SELF FAIL: con-log-del n=%d\n", ui_log_n());
                        bad = 1;
                    }
                }
                ui_log_clear();
                g_edOpen[3] = keepOpen3; // вернуть состояние: дальше тесты от дефолта
                g_dockH = keepH;
                g_conScroll = 0;
                widget_tree_init(&g_wt);
                g_wtSel = 0;
                ed_chrome_sync();
            }
            // (3b) ЛЕГАСИ-ХРОМА В ОКНЕ НЕТ. Было: полосы ui_panels — меню «File
            // Edit View» (0..26), тулбар «Q:Sel W:Move E:Rot R:Scale» (26..58),
            // строка коллапса доков (58..84). Тестер: «ты убрал нормальные и оставил
            // легаси». Числа: ED_TOP_CHROME = 0, полоса заголовков НАЧИНАЕТСЯ от края
            // окна (y=0), и клик в самой верхней точке слева (там жило легаси-меню
            // «File») попадает в НАШ заголовок.
            {
                float bx, by, bw, bh;
                tabs_rect(&bx, &by, &bw, &bh);
                int topOk = ED_TOP_CHROME == 0 && by == 0.0f &&
                            ed_top() == ED_TABS_H + ED_RIBBON_H + PAL_PAD;
                g_edTab = 2;
                UiEvent le;
                memset(&le, 0, sizeof le);
                le.kind = UI_EV_DOWN;
                le.x = 60.0f; // там, где у легаси жило меню «File»
                le.y = 10.0f;
                int hit = wt_event(&le);
                if (!topOk || !hit || g_edTab != 0) {
                    printf("SELF FAIL: ribbon-nolegacy chrome=%d top=%d tabY=%.0f hit=%d "
                           "tab=%d\n",
                           ED_TOP_CHROME, ed_top(), (double)by, hit, g_edTab);
                    bad = 1;
                }
                g_edTab = 0;
            }
            // (4) кнопка ленты UI «Button» = спавн виджета (и выбор ложится на него)
            {
                int c0 = g_wt.count;
                if (!sel_pal_click("ui.spawn.button") || g_wt.count != c0 + 1 ||
                    !widget_cget(g_act, g_wtSel)) {
                    printf("SELF FAIL: ribbon-spawn %d->%d sel=%d\n", c0, g_wt.count, g_wtSel);
                    bad = 1;
                }
            }
            // (5) выключенное действие НЕ срабатывает: без выбора Delete ничего не
            // делает (гейт canExecute, а не «кнопка для вида»)
            {
                g_wtSel = 0;
                int c0 = g_wt.count;
                int r = ed_cmd_run(ed_cmd_find("edit.delete"));
                sel_pal_click("edit.delete"); // клик по строке палитры (в ленте её нет)
                if (r || g_wt.count != c0) {
                    printf("SELF FAIL: ribbon-disabled r=%d %d->%d\n", r, c0, g_wt.count);
                    bad = 1;
                }
            }
            // (6) галочка View «Inspector»: закрыта — размера нет, полоса дока
            // освободилась (вьюпорт шире); ещё раз — назад в док
            {
                int w0 = g_vpw + g_dockR;
                if (!sel_clip_click("view.panel.inspector") || g_edOpen[2]) {
                    printf("SELF FAIL: ribbon-view open=%d\n", g_edOpen[2]);
                    bad = 1;
                }
                float x, y, w, h;
                ed_rect(g_edIns, &x, &y, &w, &h);
                if (w != 0 || h != 0 || g_dockR != 0 || g_vpw + g_dockR != w0) {
                    printf("SELF FAIL: ribbon-view-rect %.0fx%.0f dockR=%d vp=%d\n", (double)w,
                           (double)h, g_dockR, g_vpw);
                    bad = 1;
                }
                sel_clip_click("view.panel.inspector");
                if (!g_edOpen[2] || g_dockR == 0) {
                    printf("SELF FAIL: ribbon-view-back open=%d dockR=%d\n", g_edOpen[2],
                           g_dockR);
                    bad = 1;
                }
            }
            // (7) клавиши: НЕТ двух действий на один аккорд (иначе клавиша врёт) и
            // имена клавиш читаемы (тултип/шпаргалка)
            {
                int dup = 0, keys = 0, km[64], kk[64], kn = 0;
                for (int i = 0; i < ed_cmd_n(); i++)
                    for (int s = 0; s < 2; s++) {
                        int key = s == 0 ? ED_ACT[i].key : ED_ACT[i].key2;
                        int mods = s == 0 ? ED_ACT[i].mods : ED_ACT[i].mods2;
                        if (!key) continue;
                        keys++;
                        if (kn < 64) {
                            km[kn] = mods & ED_MOD_MASK;
                            kk[kn] = key;
                            kn++;
                        }
                    }
                for (int i = 0; i < kn; i++)
                    for (int j = i + 1; j < kn; j++)
                        if (km[i] == km[j] && kk[i] == kk[j]) dup++;
                char b[40];
                int okn = strcmp(ed_cmd_key_name(GLFW_KEY_F2, 0, b, sizeof b), "F2") == 0 &&
                          strcmp(ed_cmd_key_name(GLFW_KEY_DELETE, 0, b, sizeof b), "Del") == 0 &&
                          strcmp(ed_cmd_key_name(GLFW_KEY_Z, GLFW_MOD_CONTROL, b, sizeof b),
                                 "Ctrl+Z") == 0 &&
                          strcmp(ed_cmd_key_name(GLFW_KEY_1, GLFW_MOD_CONTROL, b, sizeof b),
                                 "Ctrl+1") == 0;
                if (keys < 8 || dup || !okn) {
                    printf("SELF FAIL: ribbon-keys keys=%d dup=%d names=%d\n", keys, dup, okn);
                    bad = 1;
                }
                // клавиша в реестре действительно находит действие (Ctrl+3 -> Inspector)
                int ai = ed_cmd_by_key(GLFW_MOD_CONTROL, GLFW_KEY_3);
                if (ai < 0 || strcmp(ED_ACT[ai].id, "view.panel.inspector") != 0) {
                    printf("SELF FAIL: ribbon-keyroute ai=%d\n", ai);
                    bad = 1;
                }
            }
            // (8) шпаргалка: F1 открывает, строк ровно по действиям, Esc и клик
            // закрывают (клик по ней холст не трогает)
            {
                on_key(0, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
                float kx, ky, kw, kh;
                ed_keys_rect(&kx, &ky, &kw, &kh);
                int rows = (int)((kh - 26.0f - 8.0f) / (float)ED_KEYS_RH);
                if (!g_edShowKeys || rows != ed_cmd_n() || !ed_keys_hit(kx + 10, ky + 10) ||
                    ed_keys_hit(kx - 20, ky - 20)) {
                    printf("SELF FAIL: ribbon-keysheet on=%d rows=%d\n", g_edShowKeys, rows);
                    bad = 1;
                }
                on_key(0, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
                if (g_edShowKeys || g_quit) {
                    printf("SELF FAIL: ribbon-keysheet-esc on=%d quit=%d\n", g_edShowKeys,
                           g_quit);
                    bad = 1;
                }
                on_key(0, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
                UiEvent ke;
                memset(&ke, 0, sizeof ke);
                ke.kind = UI_EV_DOWN;
                ke.x = kx + 10;
                ke.y = ky + 10;
                if (!wt_event(&ke) || g_edShowKeys) {
                    printf("SELF FAIL: ribbon-keysheet-click on=%d\n", g_edShowKeys);
                    bad = 1;
                }
            }
            // (9) тултип: у включённой кнопки — метка и клавиша, у выключенной —
            // честная пометка [off]; плашка не вылезает за окно
            {
                g_edTab = 2; // VIEW
                g_rbHot = ed_cmd_find("view.panel.tree");
                char tip[160];
                ed_ribbon_tip_text(g_rbHot, tip, sizeof tip);
                float tx, ty, tw, th;
                ed_ribbon_tip_rect(&tx, &ty, &tw, &th);
                int ok1 = strstr(tip, "Tree") && strstr(tip, "Ctrl+2") && !strstr(tip, "off");
                int in1 = tw > 0 && tx >= 0 && ty >= 0 && tx + tw <= (float)g_WW &&
                          ty + th <= (float)g_WH;
                g_edTab = 0; // HOME
                g_rbHot = ed_cmd_find("edit.delete");
                ed_ribbon_tip_text(g_rbHot, tip, sizeof tip);
                int ok2 = strstr(tip, "off") != 0;
                if (!ok1 || !in1 || !ok2) {
                    printf("SELF FAIL: ribbon-tip ok1=%d in=%d off=%d\n", ok1, in1, ok2);
                    bad = 1;
                }
                g_rbHot = -1;
            }
            // (10) активная лента переживает запуск (своё хранилище инструмента)
            {
                setenv("UI_EDIT_BENCH", "1", 1);
                int keepTab = g_edTab;
                int keepOpen[3] = {g_edOpen[0], g_edOpen[1], g_edOpen[2]};
                g_edTab = 2;
                ed_panels_save();
                g_edTab = 0;
                ed_panels_load();
                if (g_edTab != 2) {
                    printf("SELF FAIL: ribbon-persist tab=%d\n", g_edTab);
                    bad = 1;
                }
                remove(ED_PANELS_BENCH);
                unsetenv("UI_EDIT_BENCH");
                g_edTab = keepTab;
                g_edOpen[0] = keepOpen[0];
                g_edOpen[1] = keepOpen[1];
                g_edOpen[2] = keepOpen[2];
            }
            // (11) лента не залезает на вьюпорт (свой слой окна, не оверлей экрана)
            {
                if (g_vpy != ed_top() || ed_top() < ED_TOP_CHROME + ED_TABS_H + ED_RIBBON_H) {
                    printf("SELF FAIL: ribbon-space vpTop=%d top=%d\n", g_vpy, ed_top());
                    bad = 1;
                }
            }
            // (12) MODEL — РАБОЧИЕ инструменты сцены, а не кнопки для вида: режим
            // живёт в движке (ui_panels_set_tool), включён ровно один (radio),
            // клавиши Q/W/E/R берутся ИЗ РЕЕСТРА, режим переживает запуск. Гизмо
            // этого режима живёт в игровом редакторе — здесь проверяем состояние,
            // которое он читает (раньше те же 4 режима задавал легаси-тулбар).
            {
                UiPanels *sp = ui_panels_create();
                UiPanels *keep = g_panels;
                g_panels = sp;
                int mv = ed_cmd_find("scene.move");
                int srt = ed_cmd_find("scene.rotate");
                int sca = ed_cmd_find("scene.scale");
                int ssl = ed_cmd_find("scene.select");
                int pan = ed_cmd_find("ui.spawn.panel");
                int domBad = !(mv >= 0 && srt >= 0 && sca >= 0 && ssl >= 0 && pan >= 0) ||
                             ED_ACT[mv].domain != ED_DM_SCENE ||
                             ED_ACT[pan].domain != ED_DM_FRONT ||
                             ED_ACT[mv].kind != ED_K_TOOL;
                ui_panels_set_tool(sp, 1);
                int onN = 0;
                for (int i = 0; i < ed_cmd_n(); i++)
                    if (ED_ACT[i].domain == ED_DM_SCENE && ed_cmd_on(i)) onN++;
                if (domBad || onN != 1 || !ed_cmd_on(mv) || ed_cmd_on(ssl)) {
                    printf("SELF FAIL: scene-radio dom=%d on=%d mv=%d sel=%d\n", domBad, onN,
                           ed_cmd_on(mv), ed_cmd_on(ssl));
                    bad = 1;
                }
                // клик по кнопке ленты MODEL переключает режим ДВИЖКА (тот же путь,
                // что у легаси-тулбара) и клавиша из реестра — тоже
                if (!sel_clip_click("scene.rotate") || ui_panels_tool(sp) != 2) {
                    printf("SELF FAIL: scene-click tool=%d\n", ui_panels_tool(sp));
                    bad = 1;
                }
                g_nameEdit = 0;
                on_key(0, GLFW_KEY_R, 0, GLFW_PRESS, 0); // R = Scale (из реестра)
                if (ui_panels_tool(sp) != 3) {
                    printf("SELF FAIL: scene-key tool=%d\n", ui_panels_tool(sp));
                    bad = 1;
                }
                int ai = ed_cmd_by_key(0, GLFW_KEY_Q); // Q = Select
                if (ai < 0 || strcmp(ED_ACT[ai].id, "scene.select") != 0) {
                    printf("SELF FAIL: scene-keyroute ai=%d\n", ai);
                    bad = 1;
                }
                // режим — состояние ИНСТРУМЕНТА: переживает запуск (edTool)
                setenv("UI_EDIT_BENCH", "1", 1);
                ed_panels_save();
                ui_panels_set_tool(sp, 0);
                ed_panels_load();
                if (ui_panels_tool(sp) != 3) {
                    printf("SELF FAIL: scene-persist tool=%d\n", ui_panels_tool(sp));
                    bad = 1;
                }
                remove(ED_PANELS_BENCH);
                unsetenv("UI_EDIT_BENCH");
                g_panels = keep;
                ui_panels_destroy(sp);
                g_edTab = 0;
            }
            g_edTab = 0;
            g_edShowKeys = 0;
            g_rbHot = -1;
            g_edOpen[0] = g_edOpen[1] = g_edOpen[2] = 1;
            ed_chrome_init();
            ed_chrome_sync();
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        ui_panels_destroy(pn);
        // Тест мог писать свою раскладку: своё убираем, чужое возвращаем.
        remove("engine/assets/layout.json");
        if (laySaved)
            rename("engine/assets/layout.json.selftest-bak", "engine/assets/layout.json");
        printf(bad ? "SELF FAIL\n" : "edit selftest OK\n");
        return bad;
    }
    if (!glfwInit()) {
        printf("ui_edit: glfw FAIL\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(W, H, "ui_edit: front (§13)", 0, 0);
    if (!win) {
        printf("ui_edit: window FAIL\n");
        return 1;
    }
    { // §14 F5l: тест-инструмент — окно заданного размера. Жалоба тестера
        // «нажал на панель и зебра полетела» пришла из ТЕСНОГО окна: проверяем
        // доки/полосу вкладок на узком и низком окне (иначе не воспроизвести).
        const char *ws = getenv("UI_EDIT_WIN");
        int ww = 0, wh = 0;
        if (ws && sscanf(ws, "%dx%d", &ww, &wh) == 2 && ww >= 240 && wh >= 240)
            glfwSetWindowSize(win, ww, wh);
    }
    // §14 F5l(4): МИНИМАЛЬНЫЙ РАЗМЕР ОКНА. Доки (лево+право) и экран игры должны
    // помещаться вместе: уже 2*(216+8)+192 = 640 окно не вмещает раскладку, и
    // панели начинают наезжать друг на друга/на вьюпорт (тестер: «зебра улетела»
    // из тесного окна). Так делают все редакторы (UE/Blender/Figma) — пол на
    // размер, а не «сломанная раскладка на маленьком окне».
    glfwSetWindowSizeLimits(win, 640, 360, GLFW_DONT_CARE, GLFW_DONT_CARE);
    RenderCore *vec = rc_create("vec");
    if (!vec || !vec->init(vec, win)) {
        printf("ui_edit: vec FAIL\n");
        return 1;
    }
    g_vec = vec;
    // Демо-сцена для иерархии (та же тройка что в игровом редакторе).
    sc_init(&g_scene);
    {
        Entity scene = sc_spawn(&g_scene, "Scene");
        Entity hero = sc_spawn(&g_scene, "Hero");
        Entity sword = sc_spawn(&g_scene, "Sword");
        (void)hero;
        if (sword && scene) sc_parent(&g_scene, sword, scene);
    }
    g_panels = ui_panels_create();
    if (!g_panels) {
        printf("ui_edit: panels FAIL\n");
        return 1;
    }
    widget_tree_init(&g_wt); // §14: пусто до создания из палитры
    ed_chrome_init();        // §14 F3c(1b): собственный UI редактора — тоже виджеты
    ed_panels_load();        // §14 F5k: какие инструменты были открыты (переживают запуск)
    // Ш1c: консоль начинается с фактов (что открыто), а не с пустоты.
    ui_log_add("state: panels [%d%d%d%d] tab=%s", g_edOpen[0], g_edOpen[1], g_edOpen[2],
               g_edOpen[3], ed_ws_tab(g_edTab));
    ed_viewport_compute(); // F5l(4): вьюпорт редактора считают ДОКИ, а не легаси-панели
    ed_chrome_sync();
    {
        const char *sf = getenv("UI_EDIT_SELF");
        if (sf && sf[0] == '1') {
            g_editSelf = 1;
            g_act = &g_edWt;
            printf("ui_edit: self=1 (env): правим ХРОМ редактора\n");
        }
    }
    UiText *text = ui_text_init("engine/assets/mono.ttf", 16.0f);
    g_text = text; // F5a: атлас нужен хит-тесту/каретке поля имени
    { // §14 F5l(2): тест-ручки ленты — активная лента и открытая шпаргалка.
        // Только ЧТЕНИЕ состояния: файлы юзера эти ручки не пишут.
        const char *tb = getenv("UI_EDIT_TAB");
        if (tb && tb[0]) {
            int t = -1;
            if (tb[0] >= '0' && tb[0] <= '9') t = atoi(tb);
            else
                for (int i = 0; i < ed_ws_tabs(); i++)
                    if (strcasecmp(tb, ed_ws_tab(i)) == 0) t = i;
            if (t >= 0 && t < ed_ws_tabs()) {
                g_edTab = t;
                printf("ui_edit: tab=%s (%d)\n", ed_ws_tab(t), t);
            }
        }
        const char *kb = getenv("UI_EDIT_KEYS");
        if (kb && kb[0] == '1') {
            g_edShowKeys = 1;
            printf("ui_edit: keys overlay ON rows=%d\n", ed_cmd_n());
        }
    }
    if (!text) printf("ui_edit: text OFF (rects only)\n");
    // Всегда EDIT (L скриптом — как дефолтный select в ui_test).
    {
        UiEvent ke;
        memset(&ke, 0, sizeof ke);
        ke.kind = UI_EV_KEY;
        ke.key = 'L';
        ui_panels_event(g_panels, &g_scene, &ke);
        printf("ui_edit: edit=%d (want 1)\n", ui_panels_edit(g_panels));
    }
    unsigned prog = rhi_gl_prog("engine/shaders/blit/blit.vert", "engine/shaders/blit/blit.frag");
    unsigned vao = rhi_gl_empty_vao();
    if (!prog || !vao) {
        printf("ui_edit: blit FAIL\n");
        return 1;
    }
    RcTarget t;
    memset(&t, 0, sizeof t);
    RcView v;
    memset(&v, 0, sizeof v);
    v.resW = W;
    v.resH = H;
    glfwSetMouseButtonCallback(win, on_mouse);
    glfwSetCursorPosCallback(win, on_move);
    glfwSetScrollCallback(win, on_scroll);
    glfwSetKeyCallback(win, on_key);
    glfwSetCharCallback(win, on_char); // F5a: ввод текста (имя виджета)
    // Синхронизация кадра с дисплеем (по умолчанию ВКЛ). Без неё на слабом iGPU
    // кадр может попасть в композитор «полусобранным» — это и выглядит как
    // шлейф/косынка при драге. Бенчи/диагностика снимают: UI_EDIT_VSYNC=0.
    {
        const char *vs = getenv("UI_EDIT_VSYNC");
        glfwSwapInterval(vs && vs[0] == '0' ? 0 : 1);
    }
    {
        const char *nf = getenv("UI_EDIT_FULL");
        if (nf && nf[0] == '1') {
            g_noDirty = 1;
            printf("ui_edit: full=1 (dirty-rect выключен — диагноз шлейфа при драге)\n");
        }
    }
    g_curArrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_curResize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    glfwShowWindow(win);
    if (!getenv("UI_EDIT_WIN")) glfwMaximizeWindow(win); // WIN=WxH: окно как просили
    glfwPollEvents();
    drain_ev();
    // §14 F2a2: документ ЧИТАЕМ всегда из wt_path() (в тест-режиме это bench-файл,
    // файл юзера не трогаем). Раньше чтение было завязано на wt_persist_ok() — и
    // тест-прогон вообще не видел документа (клик «не выделял» ничего).
    widget_json_load(&g_wt, wt_path());
    ui_log_add("open %s: %d widgets", wt_path(), g_wt.count); // Ш1c: что загружено
    ui_wundo_init(&g_undo);
    undo_commit("load"); // нулевое состояние истории = загруженный документ
    edit_push(); // первая заливка
    // Демо-виджеты для скриншота палитры/холста (UI_EDIT_SEED=1).
    if (getenv("UI_EDIT_SEED")) {
        int p = wt_spawn(UI_W_PANEL);
        int b = wt_spawn(UI_W_BUTTON);
        int l = wt_spawn(UI_W_LABEL);
        // Вложенность: демо показывает и глубину в дереве, и крошки предков.
        if (p && b) widget_reparent(&g_wt, b, p);
        if (b && l) widget_reparent(&g_wt, l, b);
        { // Дети ЛОЖАТСЯ В ПАНЕЛЬ (модель абсолютная): иначе кнопка/подпись
            // висят на краю панели и это читается как «виджет развалился».
            UiWidget *pw = widget_get(&g_wt, p);
            UiWidget *bw = widget_get(&g_wt, b);
            UiWidget *lw = widget_get(&g_wt, l);
            if (pw && bw) {
                bw->rect.x = pw->rect.x + 24.0f;
                bw->rect.y = pw->rect.y + 30.0f;
            }
            if (bw && lw) {
                lw->rect.x = bw->rect.x + 8.0f;
                lw->rect.y = bw->rect.y + bw->rect.h + 12.0f;
            }
        }
        if (l) g_wtSel = l; // выбран самый глубокий: Scene > Panel > Button > Label
        edit_push();
    }
    { // UI_EDIT_RENAME=1: сразу открыть поле имени (скриншот/проверка каретки)
        const char *rn = getenv("UI_EDIT_RENAME");
        if (rn && rn[0] == '1') {
            ed_cmd_run(ed_cmd_find("edit.rename")); // та же дверь, что у F2
            printf("ui_edit: rename '%s' (sel %d..%d)\n", g_nameBuf, name_sel_lo(),
                   name_sel_hi());
        }
    }
    { // UI_EDIT_SPAWN=N: сразу N кнопок — проверить/показать скролл списка
        const char *sp = getenv("UI_EDIT_SPAWN");
        if (sp && sp[0] > '0') {
            int k = atoi(sp);
            for (int i = 0; i < k; i++) wt_spawn(UI_W_BUTTON);
            edit_push();
            printf("ui_edit: spawn %d (widgets=%d)\n", k, g_wt.count);
        }
    }
    // Бенч драга: без vsync-капа, синтетика в цикле (ниже).
    const char *bench = getenv("UI_EDIT_BENCH");
    if (bench && bench[0]) {
        g_bench = 1;
        g_forceFull = !strcmp(bench, "full");
        {
            const char *ff = getenv("UI_EDIT_BENCH_FULL"); // форс полного пути при той же траектории
            if (ff && ff[0] == '1') g_forceFull = 1;
        }
        g_benchNoEv = !strcmp(bench, "none") || !strcmp(bench, "spawn");
        g_benchSpawn = !strcmp(bench, "spawn");
        {
            const char *bn = getenv("UI_EDIT_BENCH_N"); // залп: сколько спавнов
            if (bn && bn[0] > '0') g_benchNSpawn = atoi(bn);
        }
        g_benchResize = !strcmp(bench, "resize");
        {
            const char *bc = getenv("UI_EDIT_BENCH_CORNER"); // тянуть угол (BR), не край
            if (bc && bc[0] == '1') g_benchCorner = 1;
        }
        const char *bk = getenv("UI_EDIT_BENCH_K");
        if (bk && bk[0] > '0') g_benchK = atoi(bk);
        {
            const char *bev = getenv("UI_EDIT_BENCH_EVENTS"); // событий/кадр (мышь)
            if (bev && bev[0] > '0') g_benchEvents = atoi(bev);
            if (g_benchEvents < 1) g_benchEvents = 1;
            const char *nc = getenv("UI_EDIT_BENCH_NOCOAL");
            if (nc && nc[0] == '1') g_benchNoCoal = 1;
        }
        glfwSwapInterval(0);
        if (!g_wt.count) wt_spawn(UI_W_PANEL);
        for (int i = 0; i < UI_MAX_WIDGETS; i++)
            if (g_wt.alive[i]) {
                g_benchId = i + 1;
                break;
            }
        const char *bid = getenv("UI_EDIT_BENCH_ID"); // какую цель тащить (1..)
        if (bid && bid[0]) {
            int want = atoi(bid), seen = 0;
            for (int i = 0; i < UI_MAX_WIDGETS; i++)
                if (g_wt.alive[i] && ++seen == want) {
                    g_benchId = i + 1;
                    break;
                }
        }
        g_wtSel = g_benchId;
        { // Диагностика: раздуть цель (UI_EDIT_BENCH_SIZE=925x530) и повесить детей
            const char *bs = getenv("UI_EDIT_BENCH_SIZE");
            if (bs && bs[0]) {
                int sw = 0, sh = 0;
                UiWidget *bw = widget_get(&g_wt, g_benchId);
                if (bw && sscanf(bs, "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0) {
                    bw->rect.anchors |= UI_A_LEFT | UI_A_TOP;
                    bw->rect.w = (float)sw;
                    bw->rect.h = (float)sh;
                }
            }
            const char *bkd = getenv("UI_EDIT_BENCH_KIDS");
            if (bkd && bkd[0] == '1') {
                UiWidgetRect cr;
                UiWidgetStyle cs;
                memset(&cr, 0, sizeof cr);
                memset(&cs, 0, sizeof cs);
                cr.anchors = UI_A_LEFT | UI_A_TOP;
                cs.alpha = 1.0f;
                cs.fontSize = 14.0f;
                cr.x = 300;
                cr.y = 150;
                cr.w = 150;
                cr.h = 22; // label
                widget_add(&g_wt, UI_W_LABEL, "Label1", g_benchId, &cr, &cs);
                cs.bg[0] = 0.22f;
                cs.bg[1] = 0.34f;
                cs.bg[2] = 0.62f;
                cs.bg[3] = 1.0f;
                cs.rounding = 4.0f;
                cr.x = 380;
                cr.y = 200;
                cr.w = 300;
                cr.h = 270; // button
                widget_add(&g_wt, UI_W_BUTTON, "Button1", g_benchId, &cr, &cs);
            }
        }
        edit_push();
    }
    // Доказательство картинкой: UI_EDIT_SHOT=shot.ppm UI_EDIT_SHOT_AT=60.
    const char *shot = getenv("UI_EDIT_SHOT");
    int shotAt = 60;
    {
        const char *sa = getenv("UI_EDIT_SHOT_AT");
        if (sa && sa[0]) shotAt = atoi(sa);
    }
    int frames = 0;
    // §14 F5h: сценарии для снимка/проверки (общее с тестером):
    //   UI_EDIT_TDRAG=<строка>     — начать драг строки дерева (призрак+индикатор);
    //   UI_EDIT_TDROP=<s>:<t>:<m>  — применить бросок (строки дерева, m 0/1/2).
    // Выполняем на 3-м кадре (УЖЕ после ресайза окна — координаты настоящие).
    const char *clEnv = getenv("UI_EDIT_CLICK"); // x:y — клик по коду события
    const char *drEnv = getenv("UI_EDIT_DRAG"); // x1:y1:x2:y2 — синтетический драг
    const char *rcEnv = getenv("UI_EDIT_RECTS"); // 1 = напечатать геометрию панелей
    const char *cl2Env = getenv("UI_EDIT_CLICK2"); // x:y — второй шаг (кадр 4)
    const char *tdEnv = getenv("UI_EDIT_TDRAG");
    const char *tdpEnv = getenv("UI_EDIT_TDROP");
    const char *scP = getenv("UI_EDIT_SCRIPT"); // §14 F5k2: скрипт действий (шаг/кадр)
    int tdcnt = 0;
    static const float hintCol[4] = {0.55f, 0.55f, 0.58f, 1.0f};
    while (!glfwWindowShouldClose(win) && !g_quit) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        { // живой FPS (для жалобы «со временем деградирует»)
            double nowT = glfwGetTime();
            if (g_lastT > 0.0) {
                float dt = (float)(nowT - g_lastT);
                if (dt > 0.0f) {
                    float f = 1.0f / dt;
                    g_fpsEma = g_fpsEma > 0.0f ? g_fpsEma * 0.9f + f * 0.1f : f;
                }
            }
            g_lastT = nowT;
        }
        if (g_nameEdit) { // F5a: каретка мигает — перерисовываем ТОЛЬКО имя
            double nt = glfwGetTime();
            if (g_nameBlink == 0.0) g_nameBlink = nt;
            else if (nt - g_nameBlink >= 0.5) {
                g_nameBlink = nt;
                g_caretOn = !g_caretOn;
                name_edit_redraw();
            }
        }
        {
            int fw = 0, fh = 0, ww = 0, wh = 0;
            glfwGetFramebufferSize(win, &fw, &fh);
            glfwGetWindowSize(win, &ww, &wh);
            if (fw > 0 && fh > 0 && (fw != g_WW || fh != g_WH)) {
                g_WW = fw;
                g_WH = fh;
                if (ww > 0 && wh > 0) {
                    g_mouseKx = (float)fw / (float)ww;
                    g_mouseKy = (float)fh / (float)wh;
                }
                eng_target_free(&t);
                memset(&t, 0, sizeof t);
                RcView vu;
                memset(&vu, 0, sizeof vu);
                vu.resW = g_WW;
                vu.resH = g_WH;
                if (vec->frame) vec->frame(vec, &vu);
                v.resW = g_WW;
                v.resH = g_WH;
                ed_chrome_sync(); // F5k: док пересчитываем от нового размера
                ed_panels_clamp(); // и оторванные панели втягиваем в окно
                edit_push();
                printf("ui_edit: resize %dx%d\n", g_WW, g_WH);
            }
        }
        tdcnt++; // кадр: синтетика F5h ждёт ресайза окна (координаты настоящие)
        if (rcEnv && rcEnv[0] == '1' && tdcnt == 3) { // F5j: где стоят панели
            for (int i = 0; i < ed_ws_panels(); i++) {
                float x, y, w, h, ix, iy, iw, ih;
                ed_panel_home(i, &x, &y, &w, &h);
                ed_rect(g_edPanels[i], &ix, &iy, &iw, &ih);
                printf("ui_edit: panel %d id=%d home=%.0f,%.0f %.0fx%.0f now=%.0f,%.0f "
                       "%.0fx%.0f float=%d\n",
                       i, g_edPanels[i], (double)x, (double)y, (double)w, (double)h,
                       (double)ix, (double)iy, (double)iw, (double)ih, ed_panel_float(i));
            }
            printf("ui_edit: vp=%d,%d %dx%d win=%dx%d\n", g_vpx, g_vpy, g_vpw, g_vph, g_WW,
                   g_WH);
            printf("ui_edit: ribbon tab=%s n=%d groups=%d actions=%d\n",
                   ed_ws_tab(g_edTab), g_rbN, g_rbGroupN, ed_cmd_n());
            for (int s2 = 0; s2 < g_rbN; s2++) { // кнопки активной ленты
                const EdCmd *a = &ED_ACT[g_rbAct[s2]];
                float x, y, w, h;
                ed_ribbon_item_rect(g_rbAct[s2], &x, &y, &w, &h);
                printf("ui_edit: rb %s x=%.0f y=%.0f %.0fx%.0f en=%d\n", a->id, (double)x,
                       (double)y, (double)w, (double)h, ed_cmd_enabled(g_rbAct[s2]));
            }
        }
        if (clEnv && clEnv[0] >= '0' && tdcnt == 3) { // F5h2: клик по коду события
            int cx = -1, cy = -1;
            if (sscanf(clEnv, "%d:%d", &cx, &cy) == 2) {
                UiEvent ce;
                memset(&ce, 0, sizeof ce);
                ce.kind = UI_EV_DOWN;
                ce.x = (float)cx;
                ce.y = (float)cy;
                route_event(&ce);
                memset(&ce, 0, sizeof ce);
                ce.kind = UI_EV_UP;
                ce.x = (float)cx;
                ce.y = (float)cy;
                route_event(&ce);
                printf("ui_edit: click %d,%d -> sel=%d\n", cx, cy, g_wtSel);
            }
        }
        if (drEnv && drEnv[0] >= '0' && tdcnt == 3) { // F5j: драг (панель/виджет)
            int x1 = -1, y1 = -1, x2 = -1, y2 = -1;
            if (sscanf(drEnv, "%d:%d:%d:%d", &x1, &y1, &x2, &y2) == 4) {
                UiEvent de;
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_DOWN;
                de.x = (float)x1;
                de.y = (float)y1;
                g_mouseDown = 1;
                route_event(&de);
                for (int s = 1; s <= 8; s++) { // движения долями — как живая мышь
                    memset(&de, 0, sizeof de);
                    de.kind = UI_EV_MOVE;
                    de.x = (float)(x1 + (x2 - x1) * s / 8);
                    de.y = (float)(y1 + (y2 - y1) * s / 8);
                    route_event(&de);
                }
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_UP;
                de.x = (float)x2;
                de.y = (float)y2;
                g_mouseDown = 0;
                route_event(&de);
                printf("ui_edit: drag %d,%d -> %d,%d grab=%d float=%d\n", x1, y1, x2, y2,
                       g_edGrab, ed_panel_float(0) || ed_panel_float(1) || ed_panel_float(2) ||
                                     ed_panel_float(3));
            }
        }
        { // §14 F5l(5): «ВЗЯЛ И ДЕРЖУ» — как РУКА тестера: клик (выбор) -> DOWN по
            // ручке -> по одному MOVE на кадр, БЕЗ отпускания. Обычный драг делает
            // DOWN+8 MOVE+UP в одном кадре и следа не оставляет, а артефакты
            // («полосы фона вылезают за вьюпорт») видны ТОЛЬКО в удержании.
            // Раскладка шагов: 0 = клик (выбор, если ничего не выбрано),
            // 1 = DOWN (теперь ловятся ручки выбранного = resize), 2..10 = MOVE.
            static const char *hdEnv = 0;
            static int hdStep = -1;
            static float hx1 = 0, hy1 = 0, hx2 = 0, hy2 = 0;
            if (!hdEnv && tdcnt == 3) hdEnv = getenv("UI_EDIT_HOLD");
            if (hdEnv && hdEnv[0] && hdStep < 0 &&
                sscanf(hdEnv, "%f:%f:%f:%f", &hx1, &hy1, &hx2, &hy2) == 4)
                hdStep = 0;
            if (hdStep >= 0 && hdStep <= 10 && tdcnt >= 3) {
                UiEvent he;
                memset(&he, 0, sizeof he);
                float t = hdStep <= 1 ? 0.0f : (float)(hdStep - 1) / 9.0f;
                he.x = hx1 + (hx2 - hx1) * t;
                he.y = hy1 + (hy2 - hy1) * t;
                if (hdStep == 0) { // клик: выбрать то, за что будем тянуть
                    he.kind = UI_EV_DOWN;
                    g_mouseDown = 1;
                    route_event(&he);
                    he.kind = UI_EV_UP;
                    g_mouseDown = 0;
                } else if (hdStep == 1) {
                    he.kind = UI_EV_DOWN;
                    g_mouseDown = 1;
                } else {
                    he.kind = UI_EV_MOVE;
                }
                route_event(&he);
                printf("ui_edit: hold step=%d %.0f,%.0f part=%d n=%d over=%d sel=%d\n", hdStep,
                       (double)he.x, (double)he.y, g_part, g_dirtyN, g_dirtyOver, g_wtSel);
                hdStep++;
            }
        }
        if (scP && tdcnt >= 3) { // §14 F5k2: СКРИПТ действий — воспроизведение
            // жалоб тестера (клик/драг по шагам, ОДИН шаг на кадр). Формат:
            //   UI_EDIT_SCRIPT="c:x:y;d:x1:y1:x2:y2;c:x:y"
            // После шага печатается состояние (sel/open/float/панели). Точка
            // замены: запись живого ввода + реплэй (пока — ручной скрипт).
            while (*scP == ';') scP++;
            if (*scP) {
                const char *se = strchr(scP, ';');
                char act[96];
                size_t an = se ? (size_t)(se - scP) : strlen(scP);
                if (an > sizeof act - 1) an = sizeof act - 1;
                memcpy(act, scP, an);
                act[an] = 0;
                scP = se ? se + 1 : scP + an;
                float a = 0, b = 0, c2 = 0, d2 = 0;
                if (sscanf(act, "c:%f:%f", &a, &b) == 2) {
                    UiEvent e2;
                    memset(&e2, 0, sizeof e2);
                    e2.kind = UI_EV_DOWN;
                    e2.x = a;
                    e2.y = b;
                    route_event(&e2);
                    memset(&e2, 0, sizeof e2);
                    e2.kind = UI_EV_UP;
                    e2.x = a;
                    e2.y = b;
                    route_event(&e2);
                } else if (sscanf(act, "d:%f:%f:%f:%f", &a, &b, &c2, &d2) == 4) {
                    UiEvent e2;
                    memset(&e2, 0, sizeof e2);
                    e2.kind = UI_EV_DOWN;
                    e2.x = a;
                    e2.y = b;
                    g_mouseDown = 1;
                    route_event(&e2);
                    for (int s = 1; s <= 8; s++) { // долями — как живая мышь
                        memset(&e2, 0, sizeof e2);
                        e2.kind = UI_EV_MOVE;
                        e2.x = a + (c2 - a) * (float)s / 8.0f;
                        e2.y = b + (d2 - b) * (float)s / 8.0f;
                        route_event(&e2);
                    }
                    memset(&e2, 0, sizeof e2);
                    e2.kind = UI_EV_UP;
                    e2.x = c2;
                    e2.y = d2;
                    g_mouseDown = 0;
                    route_event(&e2);
                } else if (sscanf(act, "m:%f:%f", &a, &b) == 2) {
                    // F5l(2): ЧИСТЫЙ ХОД мыши — проверка ховера (тултип кнопки
                    // ленты), не трогая ни выбора, ни панелей. Ставим и ЖИВОЙ
                    // курсор окна: иначе настоящая мышь следующим событием
                    // сбрасывает ховер (синтетика обязана быть как рука).
                    UiEvent e2;
                    memset(&e2, 0, sizeof e2);
                    e2.kind = UI_EV_MOVE;
                    e2.x = a;
                    e2.y = b;
                    g_mx = a;
                    g_my = b;
                    route_event(&e2);
                    glfwSetCursorPos(win, (double)(a / (g_mouseKx > 0 ? g_mouseKx : 1.0f)),
                                     (double)(b / (g_mouseKy > 0 ? g_mouseKy : 1.0f)));
                }
                printf("ui_edit: script %s -> sel=%d open=%d%d%d float=%d%d%d", act, g_wtSel,
                       g_edOpen[0], g_edOpen[1], g_edOpen[2], ed_panel_float(0),
                       ed_panel_float(1), ed_panel_float(2));
                for (int i = 0; i < ed_ws_panels(); i++) {
                    float x, y, w, h;
                    ed_rect(g_edPanels[i], &x, &y, &w, &h);
                    printf(" p%d=%.0f,%.0f %.0fx%.0f", i, (double)x, (double)y, (double)w,
                           (double)h);
                }
                printf("\n");
            }
        }
        if (cl2Env && cl2Env[0] >= '0' && tdcnt == 4) { // F5j: второй шаг сценария
            int cx = -1, cy = -1;
            if (sscanf(cl2Env, "%d:%d", &cx, &cy) == 2) {
                UiEvent ce;
                memset(&ce, 0, sizeof ce);
                ce.kind = UI_EV_DOWN;
                ce.x = (float)cx;
                ce.y = (float)cy;
                route_event(&ce);
                memset(&ce, 0, sizeof ce);
                ce.kind = UI_EV_UP;
                ce.x = (float)cx;
                ce.y = (float)cy;
                route_event(&ce);
                printf("ui_edit: click2 %d,%d -> sel=%d float=%d\n", cx, cy, g_wtSel,
                       ed_panel_float(0) || ed_panel_float(1) || ed_panel_float(2));
            }
        }
        if ((tdEnv || tdpEnv) && tdcnt >= 3) {
            ed_chrome_sync();
            tree_build();
            if (tdpEnv && tdpEnv[0] >= '0' && tdcnt == 3) { // бросок — один раз
                int sr = -1, tr = -1, md2 = 0;
                if (sscanf(tdpEnv, "%d:%d:%d", &sr, &tr, &md2) >= 2 && sr >= 0 &&
                    sr < g_treeN) {
                    int drag = g_treeId[sr];
                    int tgt = (tr >= 0 && tr < g_treeN) ? g_treeId[tr] : 0;
                    const UiWidget *dw = widget_cget(g_act, drag);
                    int oldp = dw ? dw->parent : 0;
                    if (tree_drop_apply(drag, tgt, md2)) {
                        const UiWidget *nw = widget_cget(g_act, drag);
                        g_wtSel = drag;
                        wt_persist();
                        undo_commit((nw ? nw->parent : 0) != oldp ? "reparent" : "reorder");
                        printf("ui_edit: tdrop id=%d -> %d mode=%d ok parent=%d\n",
                               drag, tgt, md2, nw ? nw->parent : -1);
                    } else {
                        printf("ui_edit: tdrop id=%d -> %d mode=%d FAIL\n", drag, tgt,
                               md2);
                    }
                }
            }
            if (tdEnv && tdEnv[0] >= '0') {
                int row = atoi(tdEnv);
                if (row >= 0 && row < g_treeN) {
                    int trow = row + 1 < g_treeN ? row + 1 : row - 1;
                    g_treeDrag = g_treeId[row];
                    float x, y, w, h;
                    tree_rect(trow - g_treeScroll, &x, &y, &w, &h);
                    g_treeDragX = x + w * 0.55f;
                    g_treeDragY = y + h * 0.5f;
                    g_treeDropId = tree_drop_at(g_treeDragX, g_treeDragY, &g_treeDropMode);
                    if (tdcnt == 3)
                        printf("ui_edit: tdrag row=%d id=%d -> %d mode=%d\n", row,
                               g_treeDrag, g_treeDropId, g_treeDropMode);
                }
            }
            if ((tdpEnv && tdpEnv[0] >= '0') || (tdEnv && tdEnv[0] >= '0')) edit_push();
        }
        if (!eng_target_resize(&t, g_WW, g_WH)) {
            printf("ui_edit: target FAIL\n");
            return 1;
        }
        // Чистим цель сами (W2: решение вызывающего, не ядра).
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glViewport(0, 0, g_WW, g_WH);
        glClearColor(0.05f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        eng_pass_begin(&t, 0.05f, 0.07f, 0.12f, 1.0f, 1.0f, 0);
        if (vec->frame_to) vec->frame_to(vec, &v, &t);
        if (text) {
            // Легаси-хром в окне редактора не рисуется (см. build_shapes), значит
            // и текста под ним нет: ни его подписей, ни окклюдеров для них.
            { // §14 F5l(2): тултип и шпаргалка — тоже ПОВЕРХНОСТЬ: текст под ними не
                // пишем, иначе подписи панелей просвечивают сквозь плашку (тот же
                // класс бага, что F5l(5) «текст под закрытой панелью»). Свой текст
                // этих плашек рисуется ПОСЛЕ снятия перекрытий.
                float occ2[2][4];
                int no2 = 0;
                if (g_rbHot >= 0) {
                    ed_ribbon_tip_rect(&occ2[no2][0], &occ2[no2][1], &occ2[no2][2],
                                       &occ2[no2][3]);
                    if (occ2[no2][2] > 0) no2++;
                }
                if (g_edShowKeys) {
                    ed_keys_rect(&occ2[no2][0], &occ2[no2][1], &occ2[no2][2], &occ2[no2][3]);
                    no2++;
                }
                ui_text_occluders(occ2, no2);
            }
            // Подписи виджетов (button/label) + заголовки панелей хрома.
            static const float tcol[4] = {0.85f, 0.87f, 0.92f, 1.0f};
            static const float hcol[4] = {0.60f, 0.65f, 0.74f, 1.0f};
            { // §14 F5l(2): заголовки лент (активная = яркая) + метки кнопок ленты
                for (int i = 0; i < ed_ws_tabs(); i++) {
                    float x, y, w, h;
                    tab_rect(i, &x, &y, &w, &h);
                    (void)w;
                    (void)h;
                    if (!ed_text_vis(-1, x + 8, y + 2)) continue;
                    ui_text_quad(text, x + 8, y + 2, ed_ws_tab(i),
                                 i == g_edTab ? tcol : hcol);
                }
                static const float gcol[4] = {0.44f, 0.48f, 0.57f, 1.0f};
                static const float ocol[4] = {0.36f, 0.38f, 0.44f, 1.0f};
                for (int g = 0; g < g_rbGroupN; g++) { // подписи групп
                    float gx, gy, gw, gh;
                    ed_ribbon_group_rect(g, &gx, &gy, &gw, &gh);
                    const char *gn = 0;
                    for (int s = 0; s < g_rbN; s++)
                        if (g_rbGrpSlot[s] == g) {
                            gn = g_rbGrp[s];
                            break;
                        }
                    if (gn && gw > 0) ui_text_quad(text, gx + 6, gy + gh - 12, gn, gcol);
                }
                for (int s = 0; s < g_rbN; s++) { // метки кнопок активной ленты
                    const EdCmd *a = &ED_ACT[g_rbAct[s]];
                    float x, y, w, h;
                    ed_ribbon_item_rect(g_rbAct[s], &x, &y, &w, &h);
                    if (w <= 0) continue;
                    float tw = ed_txt_w(a->label);
                    float tx = (a->kind == ED_K_TOGGLE || a->kind == ED_K_TOOL)
                                   ? x + 20.0f
                                   : x + (w - tw) * 0.5f;
                    ui_text_quad(text, tx, y + 2.0f, a->label,
                                 ed_cmd_enabled(g_rbAct[s]) ? tcol : ocol);
                }
            }
            {
                // §14 F3c: хром редактора = виджеты: заголовки — их имена/роль.
                // Ш1c: ОДИН цикл на все панели РЕЖИМА (подпись — из его таблицы).
                for (int i = 0; i < ed_ws_panels(); i++) {
                    float px, py, pw, ph;
                    ed_rect(g_edPanels[i], &px, &py, &pw, &ph);
                    if (!ed_panel_open(i) || !ed_text_vis(i, px + PAL_PAD, py + 4))
                        continue;
                    const char *t = ed_ws_caption(i);
                    if (i == 1 && g_editSelf) t = "EDITOR UI (F7)";
                    ui_text_quad(text, px + PAL_PAD, py + 4, t, hcol);
                }
                if (ed_live(3)) { // Ш1c: строки журнала (хвост — внизу, как в терминале)
                    static const float dim[4] = {0.36f, 0.38f, 0.44f, 1.0f};
                    float px, py;
                    con_row_rect(0, &px, &py);
                    int start = 0, rows = 0, ln = ui_log_n();
                    con_view(&start, &rows);
                    if (ln == 0) {
                        if (ed_text_vis(3, px, py))
                            ui_text_quad(text, px, py, "(empty)", dim);
                    }
                    for (int r = 0; r < rows; r++) {
                        int li = start + r;
                        if (li < 0 || li >= ln) continue;
                        float ry = py + (float)r * (float)CON_RH;
                        if (!ed_text_vis(3, px, ry)) continue;
                        ui_text_quad(text, px, ry, ui_log_line(li), tcol);
                    }
                    float cx, cy, cw, ch;
                    con_clear_chip(&cx, &cy, &cw, &ch);
                    if (ed_text_vis(3, cx + 8, cy + 2))
                        ui_text_quad(text, cx + 8, cy + 2, "Clear", tcol);
                }
                for (int k = 0; k < ed_ws_panels(); k++) { // F5j: у оторванной панели кнопка «dock»
                    int i = g_edOrder[k];
                    if (!ed_panel_float(i)) continue;
                    float cx, cy, cw, ch;
                    ed_float_chip(i, &cx, &cy, &cw, &ch);
                    (void)ch;
                    if (ed_text_vis(i, cx + 5, cy))
                        ui_text_quad(text, cx + 5, cy, "dock", hcol);
                }
                // Пустое поле = НЕ «сломалось»: пишем, что делать (тестер:
                // «поля схлопнулись и не возвращаются» — на деле выбор снят).
                float ix, iy, iw, ih; // Ш1c: rect инспектора берём явно (свой now)
                ed_rect(g_edIns, &ix, &iy, &iw, &ih);
                (void)iw;
                (void)ih;
                if (!g_wtSel && ed_panel_open(2) &&
                    ed_text_vis(2, ix + PAL_PAD, iy + PAL_PAD + PAL_TITLE + 2)) {
                    static const float dcol[4] = {0.45f, 0.47f, 0.53f, 1.0f};
                    ui_text_quad(text, ix + PAL_PAD, iy + PAL_PAD + PAL_TITLE + 2,
                                 "no selection", dcol);
                    ui_text_quad(text, ix + PAL_PAD, iy + PAL_PAD + PAL_TITLE + 20,
                                 "pick in WIDGETS", dcol);
                }
            }
            for (int i = 0; ed_panel_open(0) && i < ed_pal_n(); i++) {
                float px, py, pw, ph;
                pal_rect(i, &px, &py, &pw, &ph);
                if (ed_text_vis(0, px + 6, py + 5))
                    ui_text_quad(text, px + 6, py + 5, ed_pal_label(i), tcol);
            }
            // F3: подписи дерева виджетов (только видимые строки).
            tree_build();
            if (g_treeDrag) { // F5h: подпись призрака (что тащим)
                const UiWidget *dw = widget_cget(g_act, g_treeDrag);
                if (dw && ed_text_vis(-1, g_treeDragX + 16, g_treeDragY + 4))
                    ui_text_quad(text, g_treeDragX + 16, g_treeDragY + 4, dw->name, tcol);
            }
            if (ed_panel_open(1)) { // F5d: крошки предков (не пишем, если дерево закрыто)
                tree_path_layout();
                float px, py, pw, ph;
                tree_path_rect(&px, &py, &pw, &ph);
                (void)pw;
                (void)ph;
                int cv = ed_text_vis(1, px, py + 1); // F5j: крошки под панелью = невидимы
                if (cv) {
                    if (g_pathFrom >= 2) ui_text_quad(text, px, py + 1, "...", hcol);
                    for (int i = g_pathFrom; i < g_pathN; i++) {
                        char b[UI_WIDGET_NAME];
                        const char *s = tree_path_label(i, b);
                        if (i > g_pathFrom)
                            ui_text_quad(text, g_pathX[i] - ui_text_width(text, "/"), py + 1,
                                         "/", hcol);
                        ui_text_quad(text, g_pathX[i], py + 1, s,
                                     i == g_pathN - 1 ? tcol : hcol);
                    }
                }
            }
            if (ed_panel_open(1)) { // F5k: строки дерева
                int rows = tree_rows_fit();
                for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
                    const UiWidget *wd = widget_cget(g_act, g_treeId[i]);
                    if (!wd) continue;
                    float tx, ty, tw, th;
                    tree_rect(i - g_treeScroll, &tx, &ty, &tw, &th);
                    (void)tw;
                    (void)th;
                    float nx = tx + 16 + (float)(g_treeDepth[i] * 10);
                    if (!ed_text_vis(1, nx, ty + 3)) continue; // F5j: под панелью
                    ui_text_quad(text, nx, ty + 3, wd->name, tcol);
                    // F5d: ТИП рядом с именем (как Unity UI Builder: видно, что за
                    // элемент, не открывая инспектор). Серым — имя остаётся главным.
                    const char *tn = wt_type_name(wd->type);
                    ui_text_quad(text, nx + ui_text_width(text, wd->name) + 8, ty + 3, tn,
                                 hcol);
                }
            }
            // F3b: подписи инспектора (заголовок + поля).
            if (g_wtSel && ed_panel_open(2)) {
                const UiWidget *sw = widget_cget(g_act, g_wtSel);
                if (sw) {
                    float hx, hy, hw, hh;
                    ins_rect(-1, &hx, &hy, &hw, &hh);
                    (void)hh;
                    // F5a: правим текст — рисуем БУФЕР (а не имя модели).
                    int iv = ed_text_vis(2, hx + 6, hy + 2); // F5j: под панелью невидимо
                    if (iv)
                        ui_text_quad(text, hx + 6, hy + 2,
                                     g_nameEdit ? g_nameBuf : sw->name, tcol);
                    if (!g_nameEdit && iv) { // F5d: ТИП справа — видно, что за элемент
                        const char *tn = wt_type_name(sw->type);
                        ui_text_quad(text, hx + hw - 6 - ui_text_width(text, tn), hy + 2, tn,
                                     hcol);
                    }
                    for (int i = 0; i < IF_N; i++) {
                        float x, y, ww, hh2;
                        ins_rect(i, &x, &y, &ww, &hh2);
                        if (!ed_text_vis(2, x + 6, y + 3)) continue; // F5j: под панелью
                        ui_text_quad(text, x + 6, y + 3, INS_LABEL[i], tcol);
                        char vb[32];
                        vb[0] = 0;
                        if (i == IF_X) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.x);
                        else if (i == IF_Y) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.y);
                        else if (i == IF_W) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.w);
                        else if (i == IF_H) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.h);
                        else if (i == IF_ROUND)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.rounding);
                        else if (i == IF_ALPHA)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.alpha);
                        else if (i == IF_FONT)
                            snprintf(vb, sizeof vb, "%.0f", (double)sw->style.fontSize);
                        else if (i == IF_BG_R)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.bg[0]);
                        else if (i == IF_BG_G)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.bg[1]);
                        else if (i == IF_BG_B)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.bg[2]);
                        if (vb[0]) ui_text_quad(text, x + 70, y + 3, vb, tcol);
                    }
                }
            }
            // Ш1c: подсказка не лезет ПОД нижний док — поднимаем её над консолью.
            // Ш1c: подсказка живёт ВНУТРИ экрана игры двумя короткими строками —
            // раньше одна длинная строка уезжала под доки/консоль. Здесь только
            // мышиные операции и «где смотреть клавиши»: сами клавиши — в
            // шпаргалке F1 (второго списка клавиш не держим).
            {
                float hx = (float)(g_vpx + PAL_PAD);
                float hy = (float)(g_vpy + g_vph - 36);
                ui_text_quad(text, hx, hy,
                             "frontedit: click=select | Ctrl+click=under | drag=move |"
                             " edges=resize",
                             hintCol);
                ui_text_quad(text, hx, hy + 18.0f,
                             "tree-row drag=reparent/order | panel head drag=float |"
                             " dock chip=back | F1=keys",
                             hintCol);
            }
            { // HUD: FPS + счётчик полных перерисовок + глубина истории (F4)
                char hud[64];
                snprintf(hud, sizeof hud, "FPS %.0f  full %d  undo %d/%d",
                         (double)g_fpsEma, g_fullN, ui_wundo_depth(&g_undo),
                         ui_wundo_redo_depth(&g_undo));
                ui_text_quad(text, (float)(g_WW - 250), 6.0f, hud, hintCol);
            }
            // §14 F5l(5): ПОДПИСИ ДОКУМЕНТА — ПОСЛЕ хрома. Бюджет квадов общий на
            // кадр, и у ИНСТРУМЕНТА приоритет перед содержимым (UE/Figma): раньше
            // подписи шли первыми, и у документа с десятками виджетов надписи панелей
            // (палитра/дерево/инспектор) не рисовались вообще (баг тестера «сломал
            // отображение текста»). Теперь при переполнении теряется содержимое.
            for (int i = 0; i < UI_MAX_WIDGETS; i++) {
                const UiWidget *w = widget_cget(&g_wt, i + 1);
                if (!w || w->type == UI_W_PANEL) continue;
                float wx, wy, ww, hh;
                doc_px(w, &wx, &wy, &ww, &hh); // F5l(3): координаты экрана игры
                if (wx < (float)g_vpx || wy < (float)g_vpy ||
                    wx >= (float)(g_vpx + g_vpw) || wy >= (float)(g_vpy + g_vph))
                    continue; // за вьюпортом подписи нет (документ туда обрезан)
                if (ed_text_vis(-1, wx + 6, wy + (hh - 16) / 2))
                    ui_text_quad(text, wx + 6, wy + (hh - 16) / 2, w->name, tcol);
            }
            // §14 F5l(2): ТУЛТИП и ШПАРГАЛКА — последним проходом текста (текст
            // рисуется в порядке вызовов: кто позже, тот поверх). Так подсказка и
            // список клавиш видны над окнами панелей и над подписями документа.
            // Снимаем перекрытия: свой текст плашек писать МОЖНО.
            ui_text_occluders(0, 0);
            if (g_rbHot >= 0) {
                float x, y, w, h;
                ed_ribbon_tip_rect(&x, &y, &w, &h);
                if (w > 0) {
                    char tip[160];
                    ed_ribbon_tip_text(g_rbHot, tip, sizeof tip);
                    ui_text_quad(text, x + 7, y + 2, tip, tcol);
                }
            }
            if (g_edShowKeys) {
                float x, y, w, h;
                ed_keys_rect(&x, &y, &w, &h);
                char hd[64];
                snprintf(hd, sizeof hd, "KEYS: %d actions (F1/Esc close)", ed_cmd_n());
                ui_text_quad(text, x + 8, y + 4, hd, tcol);
                for (int i = 0; i < ed_cmd_n(); i++) {
                    float ry = y + 26.0f + (float)i * (float)ED_KEYS_RH;
                    char kb[48];
                    ed_cmd_keys(i, kb, sizeof kb);
                    int en = ed_cmd_enabled(i);
                    // ГДЕ команда живёт: строка палитры -> "PALETTE", кнопка в шапке
                    // панели -> "PANEL", иначе — лента. Тестеру («нажимаю Panel, иду
                    // в UI — опять Panel?») эта колонка и отвечает: команда живёт в
                    // ОДНОМ месте, и колонка берёт это из вида записи.
                    const EdCmd *ka = &ED_ACT[i];
                    const char *where = ed_cmd_ribbon(i)
                                            ? ed_ws_tab(ka->tab)
                                            : ((ka->views & ED_V_PALETTE) ? "PALETTE" : "PANEL");
                    ui_text_quad(text, x + 8, ry, where, hcol);
                    ui_text_quad(text, x + 70, ry, ED_ACT[i].label, en ? tcol : hcol);
                    if (kb[0])
                        ui_text_quad(text, x + w - 10.0f - ui_text_width(text, kb), ry, kb,
                                     en ? tcol : hcol);
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
            glViewport(0, 0, g_WW, g_WH);
            if (getenv("UI_EDIT_SHAPES")) // диагноз «текст пропал»: бюджет числом
                printf("ui_edit: text quads %d/%d\n", ui_text_quads(text), UI_TEXT_MAXQ);
            ui_text_flush(text, g_WW, g_WH);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        if (shot && shot[0] && frames == shotAt) {
            uint8_t *px = (uint8_t *)malloc((size_t)g_WW * g_WH * 3);
            if (px) {
                // ПАК-ВЫРАВНИВАНИЕ: по умолчанию пак = 4 байта, и для ширины
                // не кратной 4 (217, 317, …) GL дописывает паддинг в КОНЕЦ
                // строки — за границу буфера. Это НЕ косметика: тестер «нажал
                // на панель, и зебра улетела» приходил из тесного окна, где
                // программа портила кучу и рисовала мусор. Пишем строго 3*w.
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
                glReadPixels(0, 0, g_WW, g_WH, GL_RGB, GL_UNSIGNED_BYTE, px);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                FILE *ff = fopen(shot, "wb");
                if (ff) {
                    fprintf(ff, "P6\n%d %d\n255\n", g_WW, g_WH);
                    for (int y = g_WH - 1; y >= 0; y--)
                        fwrite(px + (size_t)y * g_WW * 3, 1, (size_t)g_WW * 3, ff);
                    fclose(ff);
                    printf("ui_edit: shot %s\n", shot);
                }
                free(px);
            }
        }
        // Бенч ЗАЛПА СПАВНОВ (жалоба: «лаг при создании +10 кнопок сразу»):
        // N кликов по палитре в ОДНОМ кадре — худший случай. Меряем залп в мс и
        // fps ПОСЛЕ (steady state с N виджетами). UI_EDIT_PERSIST=1 — честно, с файлом.
        if (g_benchSpawn && frames == 10) {
            double t0 = glfwGetTime();
            for (int i = 0; i < g_benchNSpawn; i++) {
                float px, py, pw, ph;
                pal_rect(1, &px, &py, &pw, &ph); // кнопка «Button»
                UiEvent be;
                memset(&be, 0, sizeof be);
                be.kind = UI_EV_DOWN;
                be.x = px + 5;
                be.y = py + 5;
                double a0 = glfwGetTime();
                route_event(&be);
                double a1 = glfwGetTime();
                if (getenv("UI_EDIT_TRACE"))
                    printf("  spawn[%d] event=%.3f ms\n", i, (a1 - a0) * 1000.0);
            }
            double t1 = glfwGetTime();
            RcVecShape tmp[UI_MAX_PRIMS];
            printf("ui_edit: bench spawn n=%d dt=%.3f ms each=%.3f ms shapes=%d widgets=%d\n",
                   g_benchNSpawn, (t1 - t0) * 1000.0,
                   (t1 - t0) * 1000.0 / (double)g_benchNSpawn, build_shapes(tmp),
                   g_wt.count);
        }
        // Бенч драга: DOWN в кадре 10, далее MOVE; замер FPS, выход по концу.
        if (g_bench && frames == 10) g_benchT0 = glfwGetTime();
        if (g_bench && !g_benchNoEv && frames >= 10 && frames < 10 + g_benchK) {
            UiEvent be;
            memset(&be, 0, sizeof be);
            if (frames == 10) {
                const UiWidget *bw = widget_cget(&g_wt, g_benchId);
                float bx, by, bww, bhh;
                doc_px(bw, &bx, &by, &bww, &bhh); // F5l(3): мышь — экран, не модель
                if (g_benchResize) { // хватаем правую ручку (или угол BR)
                    g_benchX = bx + bww - 1;
                    g_benchY = g_benchCorner ? by + bhh - 1 : by + bhh / 2;
                } else {
                    const char *bg = getenv("UI_EDIT_BENCH_GRAB"); // bg = за фон (не за детей)
                    if (bg && bg[0] == 'b') {
                        g_benchX = bx + bww * 0.2f;
                        g_benchY = by + bhh * 0.8f;
                    } else {
                        g_benchX = bx + bww / 2;
                        g_benchY = by + bhh / 2;
                    }
                }
                be.kind = UI_EV_DOWN;
                be.x = g_benchX;
                be.y = g_benchY;
                route_event(&be);
            } else {
                // N событий за кадр (как реальная мышь: опрос 125-1000Гц).
                // Коалесинг схлопывает подряд MOVE -> 1 заливка/кадр.
                float bx1, by1;
                if (g_benchResize) { // растём вправо (и вниз, если угол)
                    bx1 = g_benchX + (float)(frames - 10) * 3;
                    by1 = g_benchCorner ? g_benchY + (float)(frames - 10) * 2 : g_benchY;
                } else {
                    // Быстрый свип через весь вьюпорт (стресс: большие дельты).
                    int sw = g_vpw > 120 ? g_vpw - 80 : 200;
                    int sh = g_vph > 120 ? g_vph - 80 : 150;
                    bx1 = g_benchX + (float)((frames - 10) * 11 % sw);
                    by1 = g_benchY + (float)((frames - 10) * 5 % sh);
                }
                for (int j = 0; j < g_benchEvents; j++) {
                    UiEvent me;
                    memset(&me, 0, sizeof me);
                    me.kind = UI_EV_MOVE;
                    // подшаги: последний совпадает с одиночным событием
                    me.x = bx1 - (float)(g_benchEvents - 1 - j) * 0.3f;
                    me.y = by1 - (float)(g_benchEvents - 1 - j) * 0.2f;
                    if (g_benchNoCoal)
                        route_event(&me); // старое поведение: заливка на КАЖДОЕ событие
                    else
                        push_ev(&me);
                }
                if (!g_benchNoCoal) drain_ev();
            }
        }
        if (g_bench && frames == 10 + g_benchK - 1) {
            double dt = glfwGetTime() - g_benchT0;
            printf("ui_edit: bench %s steps=%d dt=%.3f fps=%.1f\n",
                   g_forceFull     ? "full"
                   : g_benchSpawn  ? "spawn"
                   : g_benchResize ? "resize"
                   : g_benchNoEv   ? "none"
                                   : "bands",
                   g_benchK, dt, g_benchK / dt);
            const UiWidget *bw = widget_cget(&g_wt, g_benchId);
            if (bw) {
                float rx, ry, rw, rh;
                widget_rect_px(bw, g_WW, g_WH, &rx, &ry, &rw, &rh);
                printf("ui_edit: bench geom id=%d x=%.1f y=%.1f w=%.1f h=%.1f\n",
                       g_benchId, rx, ry, rw, rh);
            }
            g_quit = 1;
        }
        // Окно прогресса (деградация во времени): каждые 100 шагов.
        if (g_bench && !g_benchNoEv && frames >= 10 && frames % 100 == 9 && frames > 10) {
            double dt = glfwGetTime() - g_benchT0;
            int done = frames - 9;
            printf("ui_edit: bench win steps=%d fps=%.1f\n", done, done / dt);
        }
        eng_pass_end(&t);
        comp_present_gl(prog, vao, &t, g_WW, g_WH);
        glfwSwapBuffers(win);
        glfwPollEvents();
        drain_ev(); // применить ввод раз за кадр (коалесинг MOVE)
        frames++;
    }
    printf("ui_edit: frames=%d DONE\n", frames);
    if (g_curArrow) glfwDestroyCursor(g_curArrow);
    if (g_curResize) glfwDestroyCursor(g_curResize);
    g_curArrow = g_curResize = 0;
    ui_panels_destroy(g_panels);
    ui_text_destroy(text);
    return 0;
}
