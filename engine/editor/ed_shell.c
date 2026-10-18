// ed_shell: оболочка редактора — раскладка окна и семейство панелей
// (см. ed_shell.h: зачем отдельным файлом и где граница).
//
// Здесь ЧИСЛА: полосы доков, вьюпорт, клампы, полосы сплиттеров. «Кто в каком
// доке» спрашиваем у РЕЖИМА (ed_ws_dock) — поэтому пятая панель не требует
// правок тут: она просто попадает в свой док.
#include "editor/ed_shell.h"

#include <string.h>
#include <stdlib.h> // getenv: путь состояния в тест/сид-режиме

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

// --- ДВЕРИ РЕЖИМА, ХРОМ, УКЛАДКА ПАНЕЛЕЙ, ПЕРСИСТ ------------------------------

void ed_shell_mode(EdShell *S, const EdShellMode *m) {
    if (S && m) S->mode = *m; // КОПИЯ: режим может отдать и локальную структуру
}

void ed_shell_no_save(EdShell *S, int v) {
    if (S) S->no_save = v ? 1 : 0;
}

// Сколько панель просит у РЕЖИМА. Нет режима — дефолт (оболочка не падает).
static void lay_size(const EdShell *S, int i, float *h, float *minh) {
    *h = 120.0f;
    *minh = 0.0f;
    if (S && S->mode.panel_size) S->mode.panel_size(i, h, minh);
}

// Зажать высоту в свободное место дока. minh > 0 — «одна строка важнее целого»;
// minh == 0 — размер ведёт режим (оболочка не трогает).
static void lay_fit(float *h, float minh, float avail) {
    if (minh <= 0.0f) return;
    if (avail < minh) avail = minh;
    if (*h > avail) *h = avail;
    if (*h < minh) *h = minh;
}

void ed_shell_init(EdShell *S) {
    if (!S) return;
    widget_tree_init(&S->wt);
    memset(S->moved, 0, sizeof S->moved);
    UiWidgetStyle st[2]; // вид панели: основной и вторичный фон (тема хрома)
    memset(st, 0, sizeof st);
    for (int k = 0; k < 2; k++) {
        st[k].alpha = 1.0f;
        st[k].rounding = 6.0f;
        st[k].fontSize = 14.0f;
        st[k].bg[0] = 0.11f;
        st[k].bg[1] = 0.12f;
        st[k].bg[2] = 0.16f;
        st[k].bg[3] = 1.0f;
    }
    st[1].bg[0] = 0.09f;
    st[1].bg[1] = 0.10f;
    st[1].bg[2] = 0.13f;
    UiWidgetRect r;
    memset(&r, 0, sizeof r);
    r.anchors = UI_A_LEFT | UI_A_TOP;
    for (int i = 0; i < ed_ws_panels() && i < ED_PANEL_MAX; i++) {
        int look = ed_ws_look(i);
        if (look < 0 || look > 1) look = 1;
        S->panel[i] = widget_add(&S->wt, UI_W_PANEL, ed_ws_tag(i), 0, &r, &st[look]);
        S->order[i] = i; // порядок панелей = порядок таблицы (верх — последний)
    }
    S->grab = -1;
}

void ed_shell_panel_home(const EdShell *S, int ww, int wh, int i, float *ox, float *oy, float *ow,
                         float *oh) {
    int dock = ed_ws_dock(i);
    if (dock == ED_DK_BOTTOM) { // нижний док: своя полоса снизу окна (консоль)
        *ox = (float)ED_PAD;
        *oy = (float)(wh - S->dockH - ED_PAD);
        *ow = (float)(ww - 2 * ED_PAD);
        *oh = (float)S->dockH;
        return;
    }
    float y = (float)ed_shell_top(); // курсор стопки дока
    for (int k = 0; k < i; k++) {    // панели выше сдвигают курсор вниз
        if (ed_ws_dock(k) != dock) continue;
        if (!S->open[k] || ed_shell_panel_float(S, k)) continue; // места не занимают
        float h, minh;
        lay_size(S, k, &h, &minh);
        lay_fit(&h, minh, (float)(wh - S->dockB - ED_PAD) - y);
        y += h + (float)ED_PANEL_GAP;
    }
    float h, minh;
    lay_size(S, i, &h, &minh);
    lay_fit(&h, minh, (float)(wh - S->dockB - ED_PAD) - y); // до нижнего дока
    if (dock == ED_DK_LEFT) { // левый док: от левого края окна, ширина = полоса
        *ox = (float)ED_PAD;
        *ow = (float)S->dockWl;
    } else { // правый док: панель шире полосы на поля (как и было)
        *ox = (float)(ww - S->dockWr - ED_PAD);
        *ow = (float)(S->dockWr + 2 * ED_PAD);
    }
    *oy = y;
    *oh = h;
}

void ed_shell_sync(EdShell *S, int ww, int wh) {
    if (!S) return;
    ed_shell_compute(S, ww, wh); // вьюпорт = окно минус полосы доков
    for (int i = 0; i < ed_ws_panels(); i++) {
        UiWidget *p = widget_get(&S->wt, S->panel[i]);
        if (!p) continue;
        if (!S->open[i]) { // F5k: инструмент закрыт -> виджет нулевого размера:
            // рантайм его не рисует, хит/ряды (ширины считаются от панели) — тоже.
            p->rect.anchors = UI_A_LEFT | UI_A_TOP;
            p->rect.x = p->rect.y = 0;
            p->rect.w = p->rect.h = 0;
            continue;
        }
        if (ed_shell_panel_float(S, i)) continue; // F5j: оторванную не трогаем
        float x, y, w, h;
        ed_shell_panel_home(S, ww, wh, i, &x, &y, &w, &h);
        p->rect.anchors = UI_A_LEFT | UI_A_TOP;
        p->rect.x = x;
        p->rect.y = y;
        p->rect.w = w;
        p->rect.h = h;
    }
}

void ed_shell_panels_keep_inside(EdShell *S, int ww, int wh) {
    if (!S) return;
    for (int i = 0; i < ed_ws_panels(); i++) {
        if (!S->open[i]) continue;
        UiWidget *p = widget_get(&S->wt, S->panel[i]);
        if (!p) continue;
        float mx = (float)ww - p->rect.w, my = (float)wh - p->rect.h;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (p->rect.x > mx) p->rect.x = mx;
        if (p->rect.x < 0) p->rect.x = 0;
        if (p->rect.y > my) p->rect.y = my;
        if (p->rect.y < 0) p->rect.y = 0;
    }
}

const char *ed_shell_path(void) {
    if (getenv("UI_EDIT_BENCH") || getenv("UI_EDIT_SEED")) return ED_SHELL_BENCH_PATH;
    return ED_SHELL_PATH;
}

void ed_shell_save(const EdShell *S) {
    if (!S || S->no_save) return; // прогон юзера файл состояния не пачкает
    FILE *f = fopen(ed_shell_path(), "wb");
    if (!f) return;
    fprintf(f,
            "{\n  \"_note\": \"editor tools (#14 F5k): edOpen = 1 = panel open, "
            "dockL/dockR/dockH = dock pane sizes; остальные поля — свои у режима. "
            "Editor state, not a document.\",\n  \"edOpen\": [");
    for (int i = 0; i < ed_ws_panels(); i++) fprintf(f, "%s%d", i ? ", " : "", S->open[i]);
    fprintf(f, "],\n  \"dockL\": %d,\n  \"dockR\": %d,\n  \"dockH\": %d", S->dockWl, S->dockWr,
            S->dockH);
    if (S->mode.state_write) { // свои поля режима (активная лента, инструмент сцены)
        fprintf(f, ",\n  ");
        S->mode.state_write(f);
    }
    fprintf(f, "\n}\n");
    fclose(f);
}

void ed_shell_load(EdShell *S, int ww, int wh) {
    if (!S) return;
    FILE *f = fopen(ed_shell_path(), "rb");
    if (!f) return;
    char buf[512];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    if (S->mode.state_read) S->mode.state_read(buf); // сперва свои поля режима
    const char *q = strstr(buf, "\"edOpen\"");
    if (q) {
        q = strchr(q, '[');
        if (q) {
            int v[ED_PANEL_MAX], got = 0;
            for (int i = 0; i < ED_PANEL_MAX; i++) v[i] = -1;
            const char *r = q + 1; // сразу за '['
            while (got < ED_PANEL_MAX && sscanf(r, "%d", &v[got]) == 1) {
                got++;
                r = strchr(r, ',');
                if (!r) break;
                r++;
            }
            if (got >= 3) // старый файл (три значения): консоль читается закрытой
                for (int i = 0; i < ed_ws_panels(); i++) S->open[i] = (i < got && v[i]) ? 1 : 0;
        }
    }
    { // размеры доков (кламп при чтении: битый файл панель не сносит)
        const char *dl = strstr(buf, "\"dockL\"");
        const char *dr = strstr(buf, "\"dockR\"");
        const char *db = strstr(buf, "\"dockH\"");
        int v;
        if (dl) {
            dl = strchr(dl, ':');
            if (dl && sscanf(dl + 1, "%d", &v) == 1 && v > 0) S->dockWl = v;
        }
        if (dr) {
            dr = strchr(dr, ':');
            if (dr && sscanf(dr + 1, "%d", &v) == 1 && v > 0) S->dockWr = v;
        }
        if (db) {
            db = strchr(db, ':');
            if (db && sscanf(db + 1, "%d", &v) == 1 && v > 0) S->dockH = v;
        }
        ed_shell_dock_set(S, ED_SP_BOTTOM, S->dockH, ww, wh);
        ed_shell_clamp_all(S, ww);
        ed_shell_compute(S, ww, wh);
    }
}
