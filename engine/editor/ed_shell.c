// ed_shell: оболочка редактора — раскладка окна и семейство панелей
// (см. ed_shell.h: зачем отдельным файлом и где граница).
//
// Здесь ЧИСЛА: полосы доков, вьюпорт, клампы, полосы сплиттеров. «Кто в каком
// доке» спрашиваем у РЕЖИМА (ed_ws_dock) — поэтому пятая панель не требует
// правок тут: она просто попадает в свой док.
#include "editor/ed_shell.h"

#include <string.h>
#include <math.h>

int ed_shell_top(void) { return ED_TOP_CHROME + ED_TABS_H + ED_RIBBON_H + ED_PAD; }

// Открыта ли хоть одна панель в доке `dock` (ED_DK_*). Одна дверь: и вьюпорт,
// и клампы, и сплиттеры спрашивают ЭТО, а не номера панелей по файлу.
static int lay_open(const EdShell *L, int dock) {
    for (int i = 0; i < ed_ws_panels(); i++)
        if (L->open[i] && ed_ws_dock(i) == dock) return 1;
    return 0;
}

void ed_shell_reset(EdShell *L) {
    if (!L) return;
    memset(L, 0, sizeof *L);
    L->dockWl = L->dockWr = ED_DOCK_W;
    L->dockH = 150; // высота нижнего дока (консоль, Ш1c)
    for (int i = 0; i < ed_ws_panels() && i < ED_PANEL_MAX; i++)
        L->open[i] = ed_ws_open_default(i); // открытость по умолчанию — у режима
}

void ed_shell_compute(EdShell *L, int ww, int wh) {
    if (!L) return;
    L->dockL = lay_open(L, ED_DK_LEFT) ? L->dockWl + ED_PAD : 0;
    L->dockR = lay_open(L, ED_DK_RIGHT) ? L->dockWr + ED_PAD : 0;
    L->dockB = lay_open(L, ED_DK_BOTTOM) ? L->dockH + ED_PAD : 0; // нижний док режет снизу
    int top = ed_shell_top(); // под полосой лент
    L->vpx = L->dockL;
    L->vpy = top;
    L->vpw = ww - L->dockL - L->dockR;
    L->vph = wh - top - L->dockB;
    if (L->vpw < 64) L->vpw = 64; // тесное окно: вьюпорт не схлопываем в ноль
    if (L->vph < 64) L->vph = 64;
}

// Держим ДВЕ границы: панель не меньше минимума, и вьюпорт не схлопывается.
static void dock_clamp(const EdShell *L, int which, int *v, int ww, int wh) {
    if (which == ED_SP_BOTTOM) { // низ меряем по ВЫСОТЕ (экран игры остаётся сверху)
        int minh = ED_DOCKH_MIN;
        int maxh = wh - ed_shell_top() - 64 - ED_PAD; // 64 = минимальный вьюпорт
        if (maxh < minh) maxh = minh;                 // тесное окно: минимум важнее
        if (*v < minh) *v = minh;
        if (*v > maxh) *v = maxh;
        return;
    }
    int minw = ED_DOCKWIN_MIN;
    // Противоположный боковой док: он тоже занимает место, и его нельзя съесть.
    int otherDock = (which == ED_SP_LEFT) ? ED_DK_RIGHT : ED_DK_LEFT;
    int other = lay_open(L, otherDock) ? (which == ED_SP_LEFT ? L->dockWr : L->dockWl) + ED_PAD
                                       : 0;
    int maxw = ww - other - ED_PAD - 64; // 64 = минимальный вьюпорт
    if (maxw < minw) maxw = minw;        // тесное окно: минимум важнее
    if (*v < minw) *v = minw;
    if (*v > maxw) *v = maxw;
}

void ed_shell_dock_set(EdShell *L, int which, int v, int ww, int wh) {
    if (!L) return;
    dock_clamp(L, which, &v, ww, wh);
    if (which == ED_SP_LEFT) L->dockWl = v;
    else if (which == ED_SP_RIGHT) L->dockWr = v;
    else L->dockH = v; // нижний док (Ш1c)
}

void ed_shell_clamp_all(EdShell *L, int ww) {
    if (!L) return;
    int gl = lay_open(L, ED_DK_LEFT) ? ED_PAD : 0;
    int gr = lay_open(L, ED_DK_RIGHT) ? ED_PAD : 0;
    int room = ww - gl - gr - 64; // всё, что можно отдать докам
    if (room < 2 * ED_DOCKWIN_MIN) room = 2 * ED_DOCKWIN_MIN;
    int maxL = room - ED_DOCKWIN_MIN;
    if (maxL < ED_DOCKWIN_MIN) maxL = ED_DOCKWIN_MIN;
    if (L->dockWl < ED_DOCKWIN_MIN) L->dockWl = ED_DOCKWIN_MIN;
    if (L->dockWl > maxL) L->dockWl = maxL;
    int maxR = room - L->dockWl;
    if (L->dockWr < ED_DOCKWIN_MIN) L->dockWr = ED_DOCKWIN_MIN;
    if (L->dockWr > maxR) L->dockWr = maxR;
}

void ed_shell_split_rect(const EdShell *L, int which, int ww, int wh, float *x, float *y,
                         float *w, float *h) {
    float top = (float)ed_shell_top();
    if (which == ED_SP_BOTTOM) { // низ: по ВЕРХНЕМУ краю дока, во всю ширину вьюпорта
        *h = (float)ED_SPLIT_HIT;
        *y = (float)(wh - L->dockB) - *h * 0.5f;
        *x = (float)L->dockL;
        *w = (float)(ww - L->dockL - L->dockR);
        if (*w < 1.0f) { // боковых доков нет — тянем во всю ширину окна
            *x = 0.0f;
            *w = (float)ww;
        }
        return;
    }
    *w = (float)ED_SPLIT_HIT;
    // Боковой док кончается НАД нижним доком: линия не режет консоль насквозь.
    *h = (float)wh - (float)L->dockB - top;
    if (*h < 1.0f) *h = 1.0f;
    *y = top;
    if (which == ED_SP_LEFT) *x = (float)L->dockL - *w * 0.5f;      // правый край левого
    else *x = (float)(ww - L->dockR) - *w * 0.5f;                   // левый край правого
}

int ed_shell_split_hit(const EdShell *L, int ww, int wh, float px, float py, int *which) {
    if (py < (float)ed_shell_top()) return 0; // сплиттеры живут под лентой
    for (int i = 1; i <= 3; i++) {
        if (i == ED_SP_LEFT && L->dockL <= 0) continue;   // дока нет — тянуть нечего
        if (i == ED_SP_RIGHT && L->dockR <= 0) continue;
        if (i == ED_SP_BOTTOM && L->dockB <= 0) continue;
        float x, y, w, h;
        ed_shell_split_rect(L, i, ww, wh, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            if (which) *which = i;
            return 1;
        }
    }
    return 0;
}

// --- ПАНЕЛИ-ИНСТРУМЕНТЫ (хром-дерево) ----------------------------------------
// Панель = виджет-окно. ГЕОМЕТРИЮ панели решает док (полоса хоста), СОДЕРЖИМОЕ
// рисует режим — он и спрашивает rect через ed_shell_panel_rect.

int ed_shell_panel_open(const EdShell *S, int i) {
    return S && i >= 0 && i < ed_ws_panels() && S->open[i];
}

int ed_shell_panel_float(const EdShell *S, int i) {
    int id = (S && i >= 0 && i < ed_ws_panels()) ? S->panel[i] : 0;
    return id > 0 && id <= UI_MAX_WIDGETS && S->moved[id - 1];
}

void ed_shell_panel_rect(const EdShell *S, int ww, int wh, int i, float *x, float *y, float *w,
                         float *h) {
    const UiWidget *p = NULL;
    if (S && i >= 0 && i < ed_ws_panels()) p = widget_cget(&S->wt, S->panel[i]);
    if (!p) { // закрытая панель: нулевой rect, содержимое обязано молчать
        *x = *y = *w = *h = 0.0f;
        return;
    }
    widget_rect_px(p, ww, wh, x, y, w, h);
}

int ed_shell_panel_live(const EdShell *S, int i) {
    if (!ed_shell_panel_open(S, i)) return 0;
    const UiWidget *p = widget_cget(&S->wt, S->panel[i]);
    return p && p->rect.w > 1.0f && p->rect.h > 1.0f;
}

void ed_shell_float_chip(const EdShell *S, int ww, int wh, int i, float *cx, float *cy, float *cw,
                         float *ch) {
    float x, y, w, h;
    ed_shell_panel_rect(S, ww, wh, i, &x, &y, &w, &h);
    (void)h;
    *cw = 34.0f;
    *ch = 15.0f;
    *cx = x + w - ED_PAD - *cw; // правый конец заголовка оторванной панели
    *cy = y + 4.0f;
}

int ed_shell_panel_top(const EdShell *S, int ww, int wh, float px, float py) {
    for (int k = ed_ws_panels() - 1; k >= 0; k--) {
        int i = S->order[k];
        if (!S->open[i]) continue; // закрытый инструмент не ловит клик
        float x, y, w, h;
        ed_shell_panel_rect(S, ww, wh, i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}

int ed_shell_panel_head_at(const EdShell *S, int ww, int wh, float px, float py, int *which) {
    int i = ed_shell_panel_top(S, ww, wh, px, py);
    if (i < 0) return 0;
    float x, y, w, h;
    ed_shell_panel_rect(S, ww, wh, i, &x, &y, &w, &h);
    if (py > y + (float)ED_TITLE_H) return 0;
    if (which) *which = i;
    return 1;
}

void ed_shell_panel_raise(EdShell *S, int i) {
    int k = 0;
    while (k < ed_ws_panels() && S->order[k] != i) k++;
    if (k >= ed_ws_panels() - 1) return; // уже верхняя
    for (; k < ed_ws_panels() - 1; k++) S->order[k] = S->order[k + 1];
    S->order[ed_ws_panels() - 1] = i;
    widget_move_before(&S->wt, S->panel[i], 0, 0); // в конец корней = верх
}

void ed_shell_panel_moved(EdShell *S, int i, int v) {
    int id = (S && i >= 0 && i < ed_ws_panels()) ? S->panel[i] : 0;
    if (id > 0 && id <= UI_MAX_WIDGETS) S->moved[id - 1] = v;
}

void ed_shell_panel_dock(EdShell *S, int i) {
    ed_shell_panel_moved(S, i, 0); // снять «оторвана»: док снова её вернёт
    if (S) S->grab = -1;
}
