// Операции дерева виджетов (§14 F1): только данные, без UI.
// id = index+1 (как Entity в scene.h): стабилен, пока слот не переиспользован.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_widget.h"

#define WIDGET_FILE_CAP 65536

void widget_tree_init(UiWidgetTree *t) {
    if (!t) return;
    memset(t, 0, sizeof *t);
}

static int slot_of(int id) {
    if (id < 1 || id > UI_MAX_WIDGETS) return -1;
    return id - 1;
}

UiWidget *widget_get(UiWidgetTree *t, int id) {
    int s = slot_of(id);
    if (!t || s < 0 || !t->alive[s]) return 0;
    return &t->items[s];
}

const UiWidget *widget_cget(const UiWidgetTree *t, int id) {
    int s = slot_of(id);
    if (!t || s < 0 || !t->alive[s]) return 0;
    return &t->items[s];
}

int widget_find(const UiWidgetTree *t, const char *name) {
    if (!t || !name || !name[0]) return 0;
    for (int i = 0; i < UI_MAX_WIDGETS; i++)
        if (t->alive[i] && !strcmp(t->items[i].name, name)) return i + 1;
    return 0;
}

void widget_name_unique(UiWidgetTree *t, int id, char *name) {
    if (!t || !name || !name[0]) return;
    // Свободно (0) или это наше собственное имя — уже уникально. ВАЖНО: кандидата
    // в модели ещё НЕТ (он в буфере вызывающего), поэтому «занято» = нашли ЧУЖОГО.
    int owner = widget_find(t, name);
    if (owner == 0 || owner == id) return;
    char base[UI_WIDGET_NAME];
    snprintf(base, sizeof base, "%s", name);
    size_t bl = strlen(base); // оставляем место под суффикс: буфер имени 48
    if (bl > UI_WIDGET_NAME - 6) bl = UI_WIDGET_NAME - 6;
    for (int k = 2; k < 1000; k++) {
        snprintf(name, UI_WIDGET_NAME, "%.*s_%d", (int)bl, base, k);
        owner = widget_find(t, name);
        if (owner == 0 || owner == id) return;
    }
}

// Отцепить от родителя (молча, если сирота). Возвращает был-родитель.
static int detach(UiWidgetTree *t, int id) {
    UiWidget *w = widget_get(t, id);
    if (!w || !w->parent) return 0;
    UiWidget *p = widget_get(t, w->parent);
    int was = w->parent;
    w->parent = 0;
    if (p) {
        for (int i = 0; i < p->nchildren; i++) {
            if (p->children[i] == id) {
                memmove(&p->children[i], &p->children[i + 1],
                        (size_t)(p->nchildren - i - 1) * sizeof(int));
                p->nchildren--;
                break;
            }
        }
    }
    return was;
}

int widget_add(UiWidgetTree *t, int type, const char *name, int parent,
               const UiWidgetRect *rect, const UiWidgetStyle *style) {
    if (!t || !name || !name[0]) return 0;
    if (type != UI_W_PANEL && type != UI_W_BUTTON && type != UI_W_LABEL) return 0;
    // Имена идут в JSON сырым текстом (без экранирования): структурные
    // символы запрещаем здесь, иначе файл побьётся (честный отказ).
    for (const char *c = name; *c; c++) {
        if (*c == '"' || *c == '\\' || *c == '{' || *c == '}' || *c == '[' || *c == ']')
            return 0;
    }
    if (parent && !widget_get(t, parent)) return 0;
    int s = -1;
    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        if (!t->alive[i]) {
            s = i;
            break;
        }
    }
    if (s < 0) return 0;
    if (parent) {
        UiWidget *p = widget_get(t, parent);
        if (p->nchildren >= UI_MAX_WIDGET_KIDS) return 0;
        p->children[p->nchildren++] = s + 1;
    }
    UiWidget *w = &t->items[s];
    memset(w, 0, sizeof *w);
    w->id = s + 1;
    w->type = type;
    strncpy(w->name, name, UI_WIDGET_NAME - 1);
    w->name[UI_WIDGET_NAME - 1] = 0;
    w->parent = parent;
    if (rect) w->rect = *rect;
    if (style) w->style = *style;
    // Слой: новый виджет ложится ПОВЕРХ соседей (тот же parent).
    w->z = 0;
    for (int i = 0; i < UI_MAX_WIDGETS; i++)
        if (t->alive[i] && t->items[i].parent == parent && t->items[i].z >= w->z)
            w->z = t->items[i].z + 1;
    t->alive[s] = 1;
    t->count++;
    return w->id;
}

// Слой среди соседей: нормализуем z в порядок 0..n-1 (по (z,id)) и меняем
// местами с соседом в направлении dir. 1 = изменилось.
int widget_layer(UiWidgetTree *t, int id, int dir) {
    UiWidget *w = widget_get(t, id);
    if (!t || !w || !dir) return 0;
    int sib[UI_MAX_WIDGETS];
    int n = 0;
    for (int i = 0; i < UI_MAX_WIDGETS; i++)
        if (t->alive[i] && t->items[i].parent == w->parent) sib[n++] = i + 1;
    if (n < 2) return 0;
    for (int i = 1; i < n; i++) { // вставками по (z, id)
        int k = sib[i], j = i - 1;
        while (j >= 0) {
            const UiWidget *a = widget_cget(t, sib[j]);
            const UiWidget *b = widget_cget(t, k);
            if (a->z > b->z || (a->z == b->z && sib[j] > k)) {
                sib[j + 1] = sib[j];
                j--;
            } else
                break;
        }
        sib[j + 1] = k;
    }
    int pos = -1;
    for (int i = 0; i < n; i++)
        if (sib[i] == id) { pos = i; break; }
    if (pos < 0) return 0;
    int np = pos + dir;
    if (np < 0 || np >= n) return 0; // уже край
    for (int i = 0; i < n; i++) widget_get(t, sib[i])->z = i; // нормализация
    UiWidget *A = widget_get(t, sib[pos]);
    UiWidget *B = widget_get(t, sib[np]);
    int tmp = A->z;
    A->z = B->z;
    B->z = tmp;
    return 1;
}

// Рекурсия по детям (глубина <= 64, дерево маленькое — стека хватает).
static void remove_sub(UiWidgetTree *t, int id) {
    UiWidget *w = widget_get(t, id);
    if (!w) return;
    int kids[UI_MAX_WIDGET_KIDS];
    int n = w->nchildren;
    memcpy(kids, w->children, (size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) remove_sub(t, kids[i]);
    detach(t, id);
    memset(w, 0, sizeof *w);
    t->alive[id - 1] = 0;
    t->count--;
}

int widget_remove(UiWidgetTree *t, int id) {
    if (!t || !widget_get(t, id)) return 0;
    remove_sub(t, id);
    return 1;
}

int widget_reparent(UiWidgetTree *t, int id, int newParent) {
    UiWidget *w = widget_get(t, id);
    if (!w) return 0;
    if (newParent == id) return 0;
    if (newParent) {
        UiWidget *np = widget_get(t, newParent);
        if (!np || np->nchildren >= UI_MAX_WIDGET_KIDS) return 0;
        // Цикл: новый родитель не может быть нашим потомком (идём вверх).
        for (int cur = newParent; cur; ) {
            if (cur == id) return 0;
            const UiWidget *cw = widget_cget(t, cur);
            if (!cw) break;
            cur = cw->parent;
        }
    }
    if (w->parent == newParent) return 1; // уже там (порядок не меняем)
    detach(t, id);
    w->parent = newParent;
    if (newParent) {
        UiWidget *np = widget_get(t, newParent);
        np->children[np->nchildren++] = id;
    }
    return 1;
}

void widget_rect_px(const UiWidget *w, int W, int H, float *x, float *y, float *ww,
                    float *hh) {
    float ox = 0, oy = 0, ow = 0, oh = 0;
    if (w && W > 0 && H > 0) {
        ow = w->rect.w;
        oh = w->rect.h;
        if (w->rect.anchors & UI_A_LEFT) ox = w->rect.x;
        else if (w->rect.anchors & UI_A_RIGHT) ox = (float)W - w->rect.x - ow;
        else ox = ((float)W - ow) * 0.5f + w->rect.x;
        if (w->rect.anchors & UI_A_TOP) oy = w->rect.y;
        else if (w->rect.anchors & UI_A_BOTTOM) oy = (float)H - w->rect.y - oh;
        else oy = ((float)H - oh) * 0.5f + w->rect.y;
    }
    if (x) *x = ox;
    if (y) *y = oy;
    if (ww) *ww = ow;
    if (hh) *hh = oh;
}

// --- JSON-секция "widgets" в layout.json (ключи доков не трогаем) ---
static int finite_f(float v) {
    return v == v && v < 1e30f && v > -1e30f;
}

static int widget_finite(const UiWidget *w) {
    const float *f = &w->rect.x;
    for (int i = 0; i < 4; i++)
        if (!finite_f(f[i])) return 0;
    const float *s = &w->style.bg[0];
    for (int i = 0; i < 7; i++)
        if (!finite_f(s[i])) return 0;
    return 1;
}

// Найти span секции: "widgets" : [ ... ] с учётом вложенных скобок.
// Возвращает [start,end) тела массива (без внешних []). 1 ок.
static int find_array(const char *s, const char *key, const char **a, const char **b) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    f = strchr(f, '[');
    if (!f) return 0;
    int depth = 0;
    const char *p = f;
    for (; *p; p++) {
        if (*p == '[') depth++;
        else if (*p == ']') {
            depth--;
            if (!depth) {
                *a = f + 1;
                *b = p;
                return 1;
            }
        }
    }
    return 0;
}

static int num_at(const char *s, const char *key, float *out) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    f = strchr(f, ':');
    if (!f) return 0;
    return sscanf(f + 1, "%f", out) == 1;
}

static int str_at(const char *s, const char *key, char *out, int cap) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f || cap <= 0) return 0;
    f = strchr(f, ':');
    if (!f) return 0;
    f++;
    while (*f == ' ' || *f == '\t') f++;
    if (*f != '"') return 0;
    f++;
    int i = 0;
    while (*f && *f != '"' && i < cap - 1) out[i++] = *f++;
    out[i] = 0;
    return *f == '"';
}

int widget_json_save(const UiWidgetTree *t, const char *path) {
    if (!t || !path || !path[0]) return 0;
    // Тело массива widgets (всё дерево, живые по порядку id).
    char body[32768];
    int n = 0;
    n += snprintf(body + n, sizeof body - (size_t)n, "[");
    int first = 1;
    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        if (!t->alive[i]) continue;
        const UiWidget *w = &t->items[i];
        if (!widget_finite(w)) return 0; // битые числа — файл не портим
        n += snprintf(body + (size_t)n, sizeof body - (size_t)n,
                      "%s{\"id\":%d,\"type\":%d,\"name\":\"%.47s\",\"parent\":%d,"
                      "\"a\":%d,\"x\":%.2f,\"y\":%.2f,\"w\":%.2f,\"h\":%.2f,"
                      "\"bg\":[%.3f,%.3f,%.3f,%.3f],\"round\":%.2f,\"alpha\":%.3f,"
                      "\"font\":%.1f,\"z\":%d,\"on\":\"%.63s\"}",
                      first ? "" : ",", w->id, w->type, w->name, w->parent, w->rect.anchors,
                      w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->style.bg[0],
                      w->style.bg[1], w->style.bg[2], w->style.bg[3], w->style.rounding,
                      w->style.alpha, w->style.fontSize, w->z, w->bind.handler);
        if (n >= (int)sizeof body - 512) return 0; // не влезло — честный отказ
        first = 0;
    }
    n += snprintf(body + (size_t)n, sizeof body - (size_t)n, "]");
    // Читаем существующий файл (ключи доков сохраняем как есть).
    char file[WIDGET_FILE_CAP + 1];
    file[0] = 0;
    FILE *ff = fopen(path, "rb");
    if (ff) {
        fseek(ff, 0, SEEK_END);
        long sz = ftell(ff);
        fseek(ff, 0, SEEK_SET);
        if (sz < 0 || sz > WIDGET_FILE_CAP) {
            fclose(ff);
            return 0;
        }
        if (sz > 0 && fread(file, 1, (size_t)sz, ff) != (size_t)sz) {
            fclose(ff);
            return 0;
        }
        file[sz > 0 ? sz : 0] = 0;
        fclose(ff);
    }
    char out[WIDGET_FILE_CAP + 32768 + 1];
    const char *a = 0, *b = 0;
    if (!file[0]) {
        // Нет файла — минимальный каркас (док-ключи подтянет lay_load дефолтом).
        snprintf(out, sizeof out, "{\n  \"widgets\": %s\n}\n", body);
    } else if (find_array(file, "widgets", &a, &b)) {
        // Замена спаном [f..b] целиком: a = f+1, тело уже со своими скобками.
        size_t pref = (size_t)((a - 1) - file);
        size_t post = strlen(b + 1);
        if (pref + (size_t)n + post + 1 > sizeof out) return 0;
        memcpy(out, file, pref);
        memcpy(out + pref, body, (size_t)n);
        memcpy(out + pref + (size_t)n, b + 1, post + 1);
    } else {
        // Файл есть, секции нет: вставить перед финальной } (иначе — отказ).
        size_t L = strlen(file);
        while (L > 0 && (file[L - 1] == ' ' || file[L - 1] == '\t' ||
                         file[L - 1] == '\n' || file[L - 1] == '\r'))
            L--;
        if (L < 2 || file[L - 1] != '}') return 0;
        // Нужна запятая, если объект не пуст ("{" + пробелы уже съедены выше? нет:
        // проверяем, есть ли что-то кроме { ).
        int empty = 1;
        for (size_t i = 0; i < L - 1; i++) {
            if (file[i] != '{' && file[i] != ' ' && file[i] != '\t' && file[i] != '\n' &&
                file[i] != '\r') {
                empty = 0;
                break;
            }
        }
        int m = snprintf(out, sizeof out, "%.*s%s\n  \"widgets\": %s\n}",
                         (int)(L - 1), file, empty ? "" : ",", body);
        if (m <= 0 || (size_t)m >= sizeof out) return 0;
    }
    ff = fopen(path, "wb");
    if (!ff) return 0;
    size_t W = strlen(out);
    int ok = fwrite(out, 1, W, ff) == W;
    fclose(ff);
    return ok;
}

int widget_json_load(UiWidgetTree *t, const char *path) {
    if (!t || !path || !path[0]) return 0;
    FILE *ff = fopen(path, "rb");
    if (!ff) return 0; // нет файла — не ошибка (пустое дерево у вызывателя)
    fseek(ff, 0, SEEK_END);
    long sz = ftell(ff);
    fseek(ff, 0, SEEK_SET);
    if (sz <= 0 || sz > WIDGET_FILE_CAP) {
        fclose(ff);
        return 0;
    }
    static char file[WIDGET_FILE_CAP + 1]; // вне кадра, инструмент/тест
    if (fread(file, 1, (size_t)sz, ff) != (size_t)sz) {
        fclose(ff);
        return 0;
    }
    file[sz] = 0;
    fclose(ff);
    const char *a = 0, *b = 0;
    if (!find_array(file, "widgets", &a, &b)) return 0;
    // Слоты по stored id (дыры ок), связи — вторым проходом.
    int pars[UI_MAX_WIDGETS];
    for (int i = 0; i < UI_MAX_WIDGETS; i++) pars[i] = -2; // -2 = слота нет в файле
    const char *p = a;
    while (p < b) {
        const char *o = strchr(p, '{');
        if (!o || o >= b) break;
        // Конец объекта: первая } при нулевой глубине [...] (свою { пропускаем:
        // старт со следующего символа, иначе +100 съест весь файл).
        // Имена/хендлеры без скобок и кавычек (см. widget_add), строки безопасны.
        int depth = 0;
        const char *q = o + 1;
        for (; q < b; q++) {
            if (*q == '[') depth++;
            else if (*q == ']') {
                depth--;
                if (depth < 0) break;
            } else if (*q == '}' && depth == 0) break;
        }
        if (q >= b || depth != 0) return 0;
        char obj[2048];
        size_t L = (size_t)(q - o + 1);
        if (L >= sizeof obj) return 0;
        memcpy(obj, o, L);
        obj[L] = 0;
        float idf = 0, typf = 0;
        if (!num_at(obj, "id", &idf) || !num_at(obj, "type", &typf)) return 0;
        int id = (int)idf;
        int s = slot_of(id);
        if (s < 0 || t->alive[s]) return 0; // дубль/слот занят — файл чужой
        int type = (int)typf;
        if (type != UI_W_PANEL && type != UI_W_BUTTON && type != UI_W_LABEL) return 0;
        UiWidget *w = &t->items[s];
        memset(w, 0, sizeof *w);
        w->id = id;
        w->type = type;
        if (!str_at(obj, "name", w->name, UI_WIDGET_NAME)) w->name[0] = 0;
        float af = 0, parf = 0;
        num_at(obj, "a", &af);
        num_at(obj, "parent", &parf);
        w->rect.anchors = (int)af;
        num_at(obj, "x", &w->rect.x);
        num_at(obj, "y", &w->rect.y);
        num_at(obj, "w", &w->rect.w);
        num_at(obj, "h", &w->rect.h);
        num_at(obj, "round", &w->style.rounding);
        num_at(obj, "alpha", &w->style.alpha);
        num_at(obj, "font", &w->style.fontSize);
        {
            float zf = 0;
            num_at(obj, "z", &zf); // нет поля (старый файл) = 0, порядок по id
            w->z = (int)zf;
        }
        {
            float r = 0, g = 0, bl = 0, al = 1;
            const char *bf = strstr(obj, "\"bg\"");
            if (bf) {
                bf = strchr(bf, '[');
                if (bf && sscanf(bf, "[%f,%f,%f,%f", &r, &g, &bl, &al) == 4) {
                    w->style.bg[0] = r;
                    w->style.bg[1] = g;
                    w->style.bg[2] = bl;
                    w->style.bg[3] = al;
                }
            }
        }
        str_at(obj, "on", w->bind.handler, UI_WIDGET_HANDLER);
        if (!widget_finite(w)) return 0;
        pars[s] = (int)parf;
        t->alive[s] = 1;
        t->count++;
        p = q + 1;
    }
    // Второй проход: связи (родитель обязан жить, иначе — сирота в корень? нет:
    // чужой parent = отказ, честно).
    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        if (pars[i] == -2) continue;
        if (pars[i] == 0) continue;
        if (pars[i] == i + 1) {
            widget_tree_init(t);
            return 0; // сам себе родитель
        }
        UiWidget *c = widget_get(t, i + 1);
        UiWidget *np = widget_get(t, pars[i]);
        if (!c || !np || np->nchildren >= UI_MAX_WIDGET_KIDS) {
            // Откат: дерево наполовину — чистим всё (атомарность загрузки).
            widget_tree_init(t);
            return 0;
        }
        c->parent = pars[i];
        np->children[np->nchildren++] = i + 1;
    }
    // Третий проход: циклы (файл чужой). Узлов <= 64: цепочка вверх длиннее
    // 64 без корня = точно цикл (возврат не обязательно в старт — ловим длиной).
    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        if (!t->alive[i]) continue;
        int cur = i + 1, steps = 0, rooted = 0;
        for (; steps <= UI_MAX_WIDGETS; steps++) {
            const UiWidget *cw = widget_cget(t, cur);
            if (!cw || !cw->parent) {
                rooted = 1;
                break;
            }
            cur = cw->parent;
        }
        if (!rooted) {
            widget_tree_init(t);
            return 0; // цикл
        }
    }
    return 1;
}
