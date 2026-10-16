// Move-гизмо: 3 стрелки из позиции сущности, длина 8 мировых единиц.
// Drag: параметр t точки мыши вдоль экранной проекции оси -> мировые единицы
// (t в долях экранной длины = долях L). Коммит на UP одной записью undo.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ui_gizmo.h"
#include "mat4.h"
#include "vec.h"
#include "ui_reflect.h" // transform_fields[0..2] = pos.x/y/z

#define GIZMO_HIT 8.0f
#define GIZMO_MINPX 20.0f
// Размер гизмо — КОНСТАНТА ЭКРАНА как в UE (а не мира): ~140px независимо
// от дистанции (см. gizmo_len). Ховер подсвечивает, плоскости таскают 2 оси,
// снеп: move 1.0, rot 10°; Ctrl инвертирует (как UE).
#define GIZMO_PX 200.0f
#define GIZMO_PLANE_K 0.35f // квадрат плоскости = 0.35 длины оси
#define GIZMO_SNAP_MOVE 1.0f
#define GIZMO_SNAP_ROT 0.1745329f // 10°
static const int PLANE_FIELDS[3][2] = {{1, 2}, {0, 2}, {0, 1}};
#define GIZMO_RING_SEG 24
#define GIZMO_RING_HIT 10.0f

struct UiGizmo {
    int tool;
    unsigned sel; // сущность под гизмо (ставит вызывающий)
    int vpx, vpy, vpw, vph; // rect вьюпорта (док-модель UE; 0,0,0,0 = весь кадр)
    int axis; // -1 нет захвата, 0/1/2 ось
    int dragKind; // -1 нет, 0 move-стрелка, 1 rotate-кольцо, 2 move-плоскость
    int hoverKind; // -1 нет, 0 ось/кольцо, 1 плоскость (подсветка как в UE)
    int hoverAxis;
    unsigned ent;
    float grabT; // t точки DOWN (доля длины / экранный угол кольца)
    float grabCoord; // координата/угол сущности на DOWN
    float grabCoord2; // вторая координата (плоскость: 2 оси)
    float grabU; // доля s точки DOWN в базисе плоскости
    // Frozen frame драга (фикс на DOWN — иначе feedback: проекция против уже
    // сдвинутой стрелки даёт усиление >1 и разнос у бортиков).
    float frz_ox, frz_oy; // origin экрана на DOWN (общий для оси/плоскости)
    float frz_ux, frz_uy; // направление оси на экране, unit (стрелка)
    float frz_wpp; // мировых на пиксель вдоль оси (стрелка)
    float frz_grab; // проекция точки DOWN в px от origin (стрелка)
    float frz_dux, frz_duy, frz_dvx, frz_dvy; // экранный базис (плоскость)
    float frz_det; // det базиса (плоскость)
    float frz_s; // мировой масштаб S на DOWN (плоскость)
    float rotSign; // знак экранного угла -> мировых радиан (калибровка на DOWN)
    float was, was2; // значения для undo-коммита (плоскость: два поля)
    int posed;
    int snap; // снеп (шаги: move 1.0, rot 10°); выкл по умолчанию, Ctrl инвертирует
};

UiGizmo *ui_gizmo_create(void) {
    UiGizmo *g = (UiGizmo *)calloc(1, sizeof *g);
    if (!g) return 0;
    g->axis = -1;
    g->dragKind = -1;
    g->hoverKind = -1;
    g->hoverAxis = -1;
    g->snap = 0; // выкл по умолчанию: драг плавный; Ctrl даёт снеп (инверсия)
    return g;
}

// Длина гизмо в мировых = константа экрана GIZMO_PX (как UE: размер не зависит
// от дистанции). L = PX * dist * tanHalf / (H/2), tanHalf из fov (0.5/1.6).
static float gizmo_len(const UiGizmo *g, const RcView *v, const float o[3]) {
    float dx = o[0] - v->camPos.x, dy = o[1] - v->camPos.y, dz = o[2] - v->camPos.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist < 1.0f) dist = 1.0f;
    float fov = v->fov > 0.2f ? v->fov : 1.6f;
    int vph = (g && g->vph > 0) ? g->vph : 720;
    float L = GIZMO_PX * dist * (0.5f / fov) / ((float)vph * 0.5f);
    if (L < 0.5f) L = 0.5f;
    return L;
}

// Снеп значения (Ctrl инвертирует текущий флаг, как в UE).
static float gizmo_snap(const UiGizmo *g, float v, float step, int mods) {
    int on = g->snap;
    if (mods & 2) on = !on; // Ctrl
    if (!on || step <= 0.0f) return v;
    return roundf(v / step) * step;
}

void ui_gizmo_destroy(UiGizmo *g) {
    free(g);
}

void ui_gizmo_tool(UiGizmo *g, int tool) {
    if (!g) return;
    if (g->tool != tool) {
        g->tool = tool;
        g->axis = -1;
        g->dragKind = -1;
        g->hoverKind = -1;
        g->hoverAxis = -1;
    }
}

void ui_gizmo_select(UiGizmo *g, unsigned ent) {
    if (!g) return;
    if (g->sel != ent) {
        g->sel = ent;
        g->axis = -1; // смена цели сбрасывает захват
        g->hoverKind = -1;
        g->hoverAxis = -1;
    }
}

void ui_gizmo_viewport(UiGizmo *g, int x, int y, int w, int h) {
    if (!g) return;
    g->vpx = x;
    g->vpy = y;
    g->vpw = w;
    g->vph = h;
}

int ui_gizmo_posed(UiGizmo *g) {
    if (!g) return 0;
    int v = g->posed;
    g->posed = 0;
    return v;
}

// Down-хит стрелков (вперёд: event шлёт сюда до определения).
UiReply ui_gizmo_pick(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H,
                      unsigned ent, float x, float y);

// PV кадра (та же сборка что mesh_draw: persp * look по yaw/pitch).
static void gizmo_pv(const RcView *v, int W, int H, Mat4 *pv) {
    Vec3 f, r, u;
    cam_basis(v->yaw, v->pitch, &f, &r, &u);
    float fov = v->fov > 0.2f ? v->fov : 1.6f;
    Mat4 proj = m4persp(2.0f * atanf(0.5f / fov), (float)W / (float)H, 0.1f, 600.0f);
    Mat4 view = m4look(v->camPos.x, v->camPos.y, v->camPos.z, v->camPos.x + f.x,
                       v->camPos.y + f.y, v->camPos.z + f.z);
    *pv = m4mul(&proj, &view);
}

int ui_gizmo_project(const RcView *v, int W, int H, const float w[3], float *sx, float *sy) {
    if (!v || W <= 0 || H <= 0) return 0;
    Mat4 pv;
    gizmo_pv(v, W, H, &pv);
    float x = w[0], y = w[1], z = w[2];
    const float *m = pv.m; // column-major
    float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
    float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (cw <= 0.0f) return 0;
    if (sx) *sx = (cx / cw * 0.5f + 0.5f) * W;
    if (sy) *sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * H; // y вниз
    return 1;
}

// Проекция ВНУТРЬ rect вьюпорта (док-модель): аспект = размер rect (камера
// рендерит с тем же аспектом), NDC маппится в rect. Пустой rect = весь кадр.
static int vp_project(const UiGizmo *g, const RcView *v, const float w[3], float *sx,
                      float *sy) {
    int vpx = g->vpx, vpy = g->vpy, vpw = g->vpw, vph = g->vph;
    if (vpw <= 0 || vph <= 0) { // весь кадр
        vpx = 0;
        vpy = 0;
        vpw = 1280;
        vph = 720;
    }
    float fx, fy; // 0..1
    {
        Mat4 pv;
        gizmo_pv(v, vpw, vph, &pv); // аспект вьюпорта, не окна
        float x = w[0], y = w[1], z = w[2];
        const float *m = pv.m;
        float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
        float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
        float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
        if (cw <= 0.0f) return 0;
        fx = cx / cw * 0.5f + 0.5f;
        fy = 1.0f - (cy / cw * 0.5f + 0.5f); // y вниз
    }
    if (sx) *sx = vpx + fx * vpw;
    if (sy) *sy = vpy + fy * vph;
    return 1;
}

// Клиппинг отрезка в rect вьюпорта (как клип виджета SViewport: гизмо не
// лезет на панели). Liang–Barsky, y вниз — знакам всё равно. 1 виден.
static int clip_seg(float x0, float y0, float x1, float y1, float rx, float ry, float rw,
                    float rh, float *ox0, float *oy0, float *ox1, float *oy1) {
    float t0 = 0.0f, t1 = 1.0f;
    float dx = x1 - x0, dy = y1 - y0;
    float px[4] = {-dx, dx, -dy, dy};
    float qx[4] = {x0 - rx, rx + rw - x0, y0 - ry, ry + rh - y0};
    for (int i = 0; i < 4; i++) {
        if (px[i] == 0.0f) {
            if (qx[i] < 0.0f) return 0;
        } else {
            float r = qx[i] / px[i];
            if (px[i] < 0.0f) {
                if (r > t1) return 0;
                if (r > t0) t0 = r;
            } else {
                if (r < t0) return 0;
                if (r < t1) t1 = r;
            }
        }
    }
    if (ox0) *ox0 = x0 + dx * t0;
    if (oy0) *oy0 = y0 + dy * t0;
    if (ox1) *ox1 = x0 + dx * t1;
    if (oy1) *oy1 = y0 + dy * t1;
    return 1;
}

// Базис плоскости кольца (ось a -> два орта). Ось X: плоскость YZ и т.д.
static void ring_basis(int a, float u[3], float v[3]) {
    if (a == 0) { // YZ
        u[0] = 0; u[1] = 1; u[2] = 0;
        v[0] = 0; v[1] = 0; v[2] = 1;
    } else if (a == 1) { // XZ
        u[0] = 1; u[1] = 0; u[2] = 0;
        v[0] = 0; v[1] = 0; v[2] = 1;
    } else { // XY
        u[0] = 1; u[1] = 0; u[2] = 0;
        v[0] = 0; v[1] = 1; v[2] = 0;
    }
}

// Поле rot под ось В ИНДЕКСАХ СПИСКА (не структуры!): pos.x=0..scale.z=8.
// Порядок soup_pose: rot.x=yaw(Y), rot.y=pitch(X), rot.z=roll(Z), т.е.
// X-кольцо крутит rot.y (индекс 4), Y-кольцо rot.x (3), Z-кольцо rot.z (5).
static int rot_field(int axis) {
    static const int F[3] = {4, 3, 5};
    return (axis >= 0 && axis < 3) ? F[axis] : 3;
}

// Точка кольца: o + R*(cos(t)*u + sin(t)*v), t радианы.
static void ring_pt(const float o[3], int axis, float t, float R, float out[3]) {
    float u[3], v[3];
    ring_basis(axis, u, v);
    float c = cosf(t) * R, s = sinf(t) * R;
    out[0] = o[0] + c * u[0] + s * v[0];
    out[1] = o[1] + c * u[1] + s * v[1];
    out[2] = o[2] + c * u[2] + s * v[2];
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by, float *t) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float tt = l2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    if (t) *t = tt;
    float cx = ax + dx * tt, cy = ay + dy * tt;
    float ex = px - cx, ey = py - cy;
    return sqrtf(ex * ex + ey * ey);
}

// Хит кольца (вперёд: event шлёт сюда до определения).
static UiReply ring_pick(UiGizmo *g, ScWorld *scene, const RcView *v, unsigned ent, float x,
                         float y);

// Чистый хит стрелки: ось или -1 (без состояния — для ховера и pick).
static int arrow_hit(UiGizmo *g, const RcView *v, const float o[3], float L, float x,
                     float y) {
    float ox, oy;
    if (!vp_project(g, v, o, &ox, &oy)) return -1;
    static const float AX[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; a++) {
        float tip[3] = {o[0] + AX[a][0] * L, o[1] + AX[a][1] * L, o[2] + AX[a][2] * L};
        float tx, ty;
        if (!vp_project(g, v, tip, &tx, &ty)) continue;
        float dx = tx - ox, dy = ty - oy;
        if (dx * dx + dy * dy < GIZMO_MINPX * GIZMO_MINPX) continue;
        float t = 0;
        if (seg_dist(x, y, ox, oy, tx, ty, &t) > GIZMO_HIT) continue;
        if (t < -0.1f || t > 1.3f) continue;
        return a;
    }
    return -1;
}

// Чистый хит плоскости ⊥ оси a (квадрат S=L*0.35 от origin). Возвращает ось
// или -1; s,t — доли в базисе (u,v) для grab.
static int plane_hit(UiGizmo *g, const RcView *v, const float o[3], float L, float x, float y,
                     float *s, float *t) {
    float S = L * GIZMO_PLANE_K;
    float u[3], vv[3];
    for (int a = 0; a < 3; a++) {
        ring_basis(a, u, vv);
        float c[4][3];
        for (int k = 0; k < 3; k++) c[0][k] = o[k];
        for (int k = 0; k < 3; k++) c[1][k] = o[k] + u[k] * S;
        for (int k = 0; k < 3; k++) c[2][k] = o[k] + (u[k] + vv[k]) * S;
        for (int k = 0; k < 3; k++) c[3][k] = o[k] + vv[k] * S;
        float p[4][2];
        int ok = 1;
        for (int k = 0; k < 4; k++)
            if (!vp_project(g, v, c[k], &p[k][0], &p[k][1])) ok = 0;
        if (!ok) continue;
        // Внутри выпуклого четырёхугольника: один знак обхода.
        int pos = 0, neg = 0;
        for (int k = 0; k < 4; k++) {
            float ax = p[(k + 1) % 4][0] - p[k][0], ay = p[(k + 1) % 4][1] - p[k][1];
            float cr = ax * (y - p[k][1]) - ay * (x - p[k][0]);
            if (cr > 0) pos = 1;
            else if (cr < 0) neg = 1;
        }
        if (pos && neg) continue;
        if (s || t) { // доли в экранном базисе (du=p1-p0, dv=p3-p0)
            float dux = p[1][0] - p[0][0], duy = p[1][1] - p[0][1];
            float dvx = p[3][0] - p[0][0], dvy = p[3][1] - p[0][1];
            float det = dux * dvy - duy * dvx;
            if (det > -1e-6f && det < 1e-6f) continue;
            float mx = x - p[0][0], my = y - p[0][1];
            if (s) *s = (mx * dvy - my * dvx) / det;
            if (t) *t = (dux * my - duy * mx) / det;
        }
        return a;
    }
    return -1;
}

UiReply ui_gizmo_event(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H,
                       UiUndo *undo, const UiEvent *e) {
    (void)W;
    (void)H; // геометрия из g->vp* (док-модель), не из окна
    if (!g || !e) return ui_unhandled();
    // Модальный drag: 0 вдоль стрелки, 1 по кольцу (угол).
    if (g->axis >= 0) {
        if (e->kind == UI_EV_MOVE) {
            if (!scene || !sc_alive(scene, g->ent)) {
                g->axis = -1;
                g->dragKind = -1;
                return ui_handled(0, 0);
            }
            if (g->dragKind == 1) {
                // Угол мыши вокруг центра в экранных (y вниз) * знак калибровки.
                ScTransform tr0 = sc_get(scene, g->ent);
                float o[3] = {tr0.pos.x, tr0.pos.y, tr0.pos.z};
                float ox, oy;
                if (!vp_project(g, v, o, &ox, &oy)) return ui_handled(0, 1);
                float aNow = atan2f(e->y - oy, e->x - ox);
                float dA = aNow - g->grabT;
                if (dA > 3.14159265f) dA -= 6.2831853f;
                if (dA < -3.14159265f) dA += 6.2831853f;
                int f = rot_field(g->axis);
                ScTransform tr = sc_get(scene, g->ent);
                float nv = gizmo_snap(g, g->grabCoord + dA * g->rotSign, GIZMO_SNAP_ROT,
                                      e->mods);
                ui_field_set_f(&tr, &transform_fields[f], nv);
                sc_set(scene, g->ent, &tr);
                g->posed = 1;
                return ui_handled(1, 1);
            }
            if (g->dragKind == 2) {
                // Плоскость: мышь в ЗАМОРОЖЕННОМ экранном базисе (с DOWN).
                // Живой пересчёт давал feedback (стрелка уезжает из-под мыши).
                float mx = e->x - g->frz_ox, my = e->y - g->frz_oy;
                float s = (mx * g->frz_dvy - my * g->frz_dvx) / g->frz_det;
                float t2 = (g->frz_dux * my - g->frz_duy * mx) / g->frz_det;
                int f0 = PLANE_FIELDS[g->axis][0], f1 = PLANE_FIELDS[g->axis][1];
                float w0 = gizmo_snap(g, g->grabCoord + (s - g->grabT) * g->frz_s,
                                      GIZMO_SNAP_MOVE, e->mods);
                float w1 = gizmo_snap(g, g->grabCoord2 + (t2 - g->grabU) * g->frz_s,
                                      GIZMO_SNAP_MOVE, e->mods);
                ScTransform trp = sc_get(scene, g->ent);
                ui_field_set_f(&trp, &transform_fields[f0], w0);
                ui_field_set_f(&trp, &transform_fields[f1], w1);
                sc_set(scene, g->ent, &trp);
                g->posed = 1;
                return ui_handled(1, 1);
            }
            // Стрелка: дельта мыши в ЗАМОРОЖЕННОМ кадре (origin+ось+wpp с DOWN).
            {
                float tpx = (e->x - g->frz_ox) * g->frz_ux + (e->y - g->frz_oy) * g->frz_uy;
                float coord = gizmo_snap(g,
                                         g->grabCoord + (tpx - g->frz_grab) * g->frz_wpp,
                                         GIZMO_SNAP_MOVE, e->mods);
                ScTransform tr = sc_get(scene, g->ent);
                ui_field_set_f(&tr, &transform_fields[g->axis], coord);
                sc_set(scene, g->ent, &tr);
                g->posed = 1;
                return ui_handled(1, 1); // держим захват; стрелки едут за сущностью
            }
        }
        if (e->kind == UI_EV_UP) {
            if (scene && sc_alive(scene, g->ent)) {
                if (g->dragKind == 2) {
                    // Плоскость: две записи (по полю на ось).
                    int f0 = PLANE_FIELDS[g->axis][0], f1 = PLANE_FIELDS[g->axis][1];
                    ScTransform tr = sc_get(scene, g->ent);
                    if (undo) {
                        ui_undo_push(undo, g->ent, f0, g->was,
                                     ui_field_get_f(&tr, &transform_fields[f0]));
                        ui_undo_push(undo, g->ent, f1, g->was2,
                                     ui_field_get_f(&tr, &transform_fields[f1]));
                    }
                } else {
                    int f = g->dragKind == 1 ? rot_field(g->axis) : g->axis;
                    ScTransform tr = sc_get(scene, g->ent);
                    float now = ui_field_get_f(&tr, &transform_fields[f]);
                    if (undo) ui_undo_push(undo, g->ent, f, g->was, now);
                }
            }
            g->axis = -1;
            g->dragKind = -1;
            return ui_handled(1, 0);
        }
        // Ховер без захвата: кто под курсором (подсветка как в UE).
        if (e->kind == UI_EV_MOVE && g->tool == 1 && scene && g->sel &&
            sc_alive(scene, g->sel)) {
            ScTransform trh = sc_get(scene, g->sel);
            float oh[3] = {trh.pos.x, trh.pos.y, trh.pos.z};
            float glh = gizmo_len(g, v, oh);
            int hk = -1, ha = -1;
            int pa = plane_hit(g, v, oh, glh, e->x, e->y, 0, 0);
            if (pa >= 0) {
                hk = 1;
                ha = pa;
            } else {
                int axh = arrow_hit(g, v, oh, glh, e->x, e->y);
                if (axh >= 0) {
                    hk = 0;
                    ha = axh;
                }
            }
            if (hk != g->hoverKind || ha != g->hoverAxis) {
                g->hoverKind = hk;
                g->hoverAxis = ha;
                return ui_handled(1, 0); // перестроить с подсветкой
            }
            return ui_handled(0, 0);
        }
        return ui_unhandled();
    }
    if ((g->tool != 1 && g->tool != 2) || e->kind != UI_EV_DOWN || e->button != 0)
        return ui_unhandled();
    // Хит selected (приоритет выше панелей — вызывающий шлёт сюда первым).
    // tool 1: стрелки; tool 2: кольца.
    if (!scene || !g->sel) return ui_unhandled();
    if (g->tool == 2) return ring_pick(g, scene, v, g->sel, e->x, e->y);
    return ui_gizmo_pick(g, scene, v, W, H, g->sel, e->x, e->y);
}

// Точка ручки для тестов/автоматизации: axis 0/1/2, kind 0 стрелка / 1 кольцо,
// angleDeg вдоль ручки (стрелка: 0 начало 100 конец; кольцо: градусы).
// Возвращает 1 + экранные x/y.
int ui_gizmo_handle(UiGizmo *g, ScWorld *scene, const RcView *v, int axis, int kind,
                    float angleDeg, float *x, float *y) {
    if (!g || !scene || axis < 0 || axis > 2) return 0;
    if (!g->sel || !sc_alive(scene, g->sel)) return 0;
    ScTransform tr = sc_get(scene, g->sel);
    float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
    if (kind == 1) {
        float p[3];
        ring_pt(o, axis, angleDeg * 0.0174533f, gizmo_len(g, v, o), p);
        return vp_project(g, v, p, x, y);
    }
    float tip[3] = {o[0], o[1], o[2]};
    static const float AX[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float gl = gizmo_len(g, v, o);
    tip[0] += AX[axis][0] * gl;
    tip[1] += AX[axis][1] * gl;
    tip[2] += AX[axis][2] * gl;
    float ox, oy, tx, ty;
    if (!vp_project(g, v, o, &ox, &oy) || !vp_project(g, v, tip, &tx, &ty)) return 0;
    float t = angleDeg / 100.0f;
    if (x) *x = ox + (tx - ox) * t;
    if (y) *y = oy + (ty - oy) * t;
    return 1;
}

// DOWN-хит move-тулзы: сначала плоскости (маленькие, у origin), потом стрелки.
UiReply ui_gizmo_pick(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H, unsigned ent,
                      float x, float y) {
    (void)W;
    (void)H; // см. event
    if (!g || g->tool != 1 || !scene || !sc_alive(scene, ent)) return ui_unhandled();
    ScTransform tr = sc_get(scene, ent);
    float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
    float gl = gizmo_len(g, v, o);
    float ps, pt;
    int pa = plane_hit(g, v, o, gl, x, y, &ps, &pt);
    if (pa >= 0) {
        g->axis = pa;
        g->dragKind = 2; // плоскость: drag в 2 осях
        g->ent = ent;
        g->grabT = ps;
        g->grabU = pt;
        g->grabCoord = ui_field_get_f(&tr, &transform_fields[PLANE_FIELDS[pa][0]]);
        g->grabCoord2 = ui_field_get_f(&tr, &transform_fields[PLANE_FIELDS[pa][1]]);
        g->was = g->grabCoord;
        g->was2 = g->grabCoord2;
        // Заморозить экранный базис (тот же что видел plane_hit).
        {
            float S = gl * GIZMO_PLANE_K;
            float uu[3], vvv[3];
            ring_basis(pa, uu, vvv);
            float q0[3] = {o[0], o[1], o[2]};
            float qu[3] = {o[0] + uu[0] * S, o[1] + uu[1] * S, o[2] + uu[2] * S};
            float qv[3] = {o[0] + vvv[0] * S, o[1] + vvv[1] * S, o[2] + vvv[2] * S};
            float ox, oy, ux, uy, vx, vy;
            if (!vp_project(g, v, q0, &ox, &oy) || !vp_project(g, v, qu, &ux, &uy) ||
                !vp_project(g, v, qv, &vx, &vy))
                return ui_unhandled();
            g->frz_ox = ox;
            g->frz_oy = oy;
            g->frz_dux = ux - ox;
            g->frz_duy = uy - oy;
            g->frz_dvx = vx - ox;
            g->frz_dvy = vy - oy;
            g->frz_det = g->frz_dux * g->frz_dvy - g->frz_duy * g->frz_dvx;
            g->frz_s = S;
        }
        return ui_handled(0, 1);
    }
    int a = arrow_hit(g, v, o, gl, x, y);
    if (a < 0) return ui_unhandled();
    // t для drag считаем тут (arrow_hit чистый, состояния не ставит).
    float ox, oy;
    vp_project(g, v, o, &ox, &oy);
    static const float AX[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float tip[3] = {o[0] + AX[a][0] * gl, o[1] + AX[a][1] * gl, o[2] + AX[a][2] * gl};
    float tx, ty, t = 0;
    if (vp_project(g, v, tip, &tx, &ty)) seg_dist(x, y, ox, oy, tx, ty, &t);
    g->axis = a;
    g->dragKind = 0; // стрелка: drag вдоль оси
    g->ent = ent;
    g->grabT = t;
    g->grabCoord = o[a];
    g->was = o[a];
    // Заморозить кадр: origin + направление + мировых/px (с этого DOWN).
    {
        float dx = tx - ox, dy = ty - oy;
        float alen = sqrtf(dx * dx + dy * dy);
        if (alen < 1.0f) { // ось в точку (смотрит в камеру) — драг по X экрана
            dx = 1.0f;
            dy = 0.0f;
            alen = 1.0f;
        }
        g->frz_ox = ox;
        g->frz_oy = oy;
        g->frz_ux = dx / alen;
        g->frz_uy = dy / alen;
        g->frz_wpp = gl / alen;
        g->frz_grab = t * alen;
    }
    return ui_handled(0, 1); // захват; грязь будет на MOVE
}

// Хит кольца: дистанция до СЕГМЕНТОВ отрисованного кольца (<10px).
// Тот же сэмплинг что build (эллипс под перспективой, не средний радиус).
// Калибровка знака: +0.05 мировых радиан -> экранная дельта.
static UiReply ring_pick(UiGizmo *g, ScWorld *scene, const RcView *v, unsigned ent, float x,
                         float y) {
    ScTransform tr = sc_get(scene, ent);
    float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
    float ox, oy;
    if (!vp_project(g, v, o, &ox, &oy)) return ui_unhandled();
    for (int a = 0; a < 3; a++) {
        float u[3], vv[3];
        ring_basis(a, u, vv);
        float rl = gizmo_len(g, v, o);
        float pu[3] = {o[0] + u[0] * rl, o[1] + u[1] * rl,
                       o[2] + u[2] * rl};
        float pv[3] = {o[0] + vv[0] * rl, o[1] + vv[1] * rl,
                       o[2] + vv[2] * rl};
        float ux, uy, vx, vy;
        if (!vp_project(g, v, pu, &ux, &uy) || !vp_project(g, v, pv, &vx, &vy))
            continue;
        // Кольцо ребром (оба базиса короткие) — не взять.
        float lu = sqrtf((ux - ox) * (ux - ox) + (uy - oy) * (uy - oy));
        float lv = sqrtf((vx - ox) * (vx - ox) + (vy - oy) * (vy - oy));
        if (lu < 25.0f && lv < 25.0f) continue;
        float q0x = 0, q0y = 0;
        int first = 1, hit = 0;
        for (int s = 0; s <= GIZMO_RING_SEG; s++) {
            float t = (float)s / GIZMO_RING_SEG * 6.2831853f;
            float p[3];
            ring_pt(o, a, t, rl, p);
            float sx, sy;
            if (!vp_project(g, v, p, &sx, &sy)) {
                first = 1;
                continue;
            }
            if (!first && seg_dist(x, y, q0x, q0y, sx, sy, 0) < GIZMO_RING_HIT) hit = 1;
            q0x = sx;
            q0y = sy;
            first = 0;
        }
        if (!hit) continue;
        // Знак: крутим точку на +0.05 мировых, смотрим экранную дельту.
        float p0[3], p1[3];
        ring_pt(o, a, 0.0f, rl, p0);
        ring_pt(o, a, 0.05f, rl, p1);
        float s0x, s0y, s1x, s1y;
        if (!vp_project(g, v, p0, &s0x, &s0y) || !vp_project(g, v, p1, &s1x, &s1y))
            continue;
        float dCal = atan2f(s1y - oy, s1x - ox) - atan2f(s0y - oy, s0x - ox);
        if (dCal > 3.14159265f) dCal -= 6.2831853f;
        if (dCal < -3.14159265f) dCal += 6.2831853f;
        if (fabsf(dCal) < 1e-4f) continue;
        g->axis = a;
        g->dragKind = 1; // кольцо: drag по углу
        g->ent = ent;
        g->grabT = atan2f(y - oy, x - ox);
        int f = rot_field(a);
        g->grabCoord = ui_field_get_f(&tr, &transform_fields[f]);
        g->was = g->grabCoord;
        g->rotSign = 0.05f / dCal;
        return ui_handled(0, 1);
    }
    return ui_unhandled();
}

int ui_gizmo_build(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H, UiPrim *out,
                   int cap) {
    (void)W;
    (void)H; // см. выше
    if (!g || (g->tool != 1 && g->tool != 2) || !scene || !out || cap <= 0) return 0;
    // Рисуем для selected (ставит вызывающий).
    if (!g->sel || !sc_alive(scene, g->sel)) return 0;
    ScTransform tr = sc_get(scene, g->sel);
    float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
    float ox, oy;
    if (!vp_project(g, v, o, &ox, &oy)) return 0;
    static const float AX[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const float COL[3][4] = {{1, 0.15f, 0.1f, 1}, {0.15f, 0.75f, 0.15f, 1},
                                    {0.2f, 0.4f, 1, 1}};
    int n = 0;
    // Кольца поворота (ArcHandles-паттерн): 24 сегмента на кольцо, клип в rect.
    if (g->tool == 2) {
        float vpx = (float)g->vpx, vpy = (float)g->vpy, vpw = (float)g->vpw,
              vph = (float)g->vph;
        if (vpw <= 0 || vph <= 0) {
            vpx = 0;
            vpy = 0;
            vpw = 1280;
            vph = 720;
        }
        for (int a = 0; a < 3; a++) {
            float gl = gizmo_len(g, v, o);
            float p1[3];
            float q0x = 0, q0y = 0;
            int first = 1;
            for (int s = 0; s <= GIZMO_RING_SEG && n < cap; s++) {
                float t = (float)s / GIZMO_RING_SEG * 6.2831853f;
                ring_pt(o, a, t, gl, p1);
                float sx, sy;
                if (!vp_project(g, v, p1, &sx, &sy)) {
                    first = 1;
                    continue; // за камерой — рвём кольцо
                }
                if (!first && n < cap) {
                    float cx0, cy0, cx1, cy1;
                    if (clip_seg(q0x, q0y, sx, sy, vpx, vpy, vpw, vph, &cx0, &cy0,
                                 &cx1, &cy1)) {
                        UiPrim *q = &out[n++];
                        q->kind = 2;
                        q->a = cx0;
                        q->b = cy0;
                        q->c = cx1;
                        q->d = cy1;
                        memcpy(q->rgba, COL[a], sizeof q->rgba);
                    }
                }
                q0x = sx;
                q0y = sy;
                first = 0;
            }
        }
        return n;
    }
    for (int a = 0; a < 3 && n + 3 <= cap; a++) {
        float gl = gizmo_len(g, v, o);
        float tip[3] = {o[0] + AX[a][0] * gl, o[1] + AX[a][1] * gl,
                        o[2] + AX[a][2] * gl};
        float tx, ty;
        if (!vp_project(g, v, tip, &tx, &ty)) continue;
        float dx = tx - ox, dy = ty - oy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < GIZMO_MINPX) continue;
        // Стрела + наконечник (две короткие под 25°, 12px).
        // Всё клипим в rect вьюпорта (иначе лезет на панели).
        float vpx = (float)g->vpx, vpy = (float)g->vpy, vpw = (float)g->vpw,
              vph = (float)g->vph;
        if (vpw <= 0 || vph <= 0) {
            vpx = 0;
            vpy = 0;
            vpw = 1280;
            vph = 720;
        }
        float ux = dx / len, uy = dy / len;
        float hx = -uy, hy = ux;
        float hl = 12.0f;
        float c1 = 0.42f, s1 = 0.42f; // cos/sin 25° экономно
        float sg[3][4] = {
            {ox, oy, tx, ty},
            {tx, ty, tx - (ux * c1 - hx * s1) * hl, ty - (uy * c1 - hy * s1) * hl},
            {tx, ty, tx - (ux * c1 + hx * s1) * hl, ty - (uy * c1 + hy * s1) * hl},
        };
        for (int sgi = 0; sgi < 3 && n < cap; sgi++) {
            float cx0, cy0, cx1, cy1;
            if (!clip_seg(sg[sgi][0], sg[sgi][1], sg[sgi][2], sg[sgi][3], vpx, vpy, vpw,
                          vph, &cx0, &cy0, &cx1, &cy1))
                continue;
            UiPrim *q = &out[n++];
            q->kind = 2;
            q->a = cx0;
            q->b = cy0;
            q->c = cx1;
            q->d = cy1;
            if (g->hoverKind == 0 && g->hoverAxis == a) {
                // Ховер как в UE: ярче (без утолщения — линии 1px).
                q->rgba[0] = COL[a][0] * 1.6f > 1.0f ? 1.0f : COL[a][0] * 1.6f;
                q->rgba[1] = COL[a][1] * 1.6f > 1.0f ? 1.0f : COL[a][1] * 1.6f;
                q->rgba[2] = COL[a][2] * 1.6f > 1.0f ? 1.0f : COL[a][2] * 1.6f;
                q->rgba[3] = 1.0f;
            } else {
                memcpy(q->rgba, COL[a], sizeof q->rgba);
            }
        }
    }
    // Плоскости перемещения (квадраты у origin, 2 триса; как UE move-planes).
    // Поверх стрел (порядок массива = порядок рисования).
    if (g->tool == 1) {
        float vpx = (float)g->vpx, vpy = (float)g->vpy, vpw = (float)g->vpw,
              vph = (float)g->vph;
        if (vpw <= 0 || vph <= 0) {
            vpx = 0;
            vpy = 0;
            vpw = 1280;
            vph = 720;
        }
        float gl = gizmo_len(g, v, o);
        float S = gl * GIZMO_PLANE_K;
        for (int a = 0; a < 3 && n + 2 <= cap; a++) {
            float u[3], vv[3];
            ring_basis(a, u, vv);
            float c[4][3];
            for (int k = 0; k < 3; k++) c[0][k] = o[k];
            for (int k = 0; k < 3; k++) c[1][k] = o[k] + u[k] * S;
            for (int k = 0; k < 3; k++) c[2][k] = o[k] + (u[k] + vv[k]) * S;
            for (int k = 0; k < 3; k++) c[3][k] = o[k] + vv[k] * S;
            float p[4][2];
            int ok = 1;
            for (int k = 0; k < 4; k++)
                if (!vp_project(g, v, c[k], &p[k][0], &p[k][1])) ok = 0;
            if (!ok) continue;
            // Цвет пары осей, тусклый; ховер — яркий.
            float cr = (COL[(a + 1) % 3][0] + COL[(a + 2) % 3][0]) * 0.5f;
            float cg = (COL[(a + 1) % 3][1] + COL[(a + 2) % 3][1]) * 0.5f;
            float cb = (COL[(a + 1) % 3][2] + COL[(a + 2) % 3][2]) * 0.5f;
            float mul = (g->hoverKind == 1 && g->hoverAxis == a) ? 1.6f : 0.7f;
            // Два триса (kind=3: p0=(a,b) p1=(c,d) p2=(e,f)), клип по рёбрам.
            int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int ti = 0; ti < 2 && n < cap; ti++) {
                float ax = p[tris[ti][0]][0], ay = p[tris[ti][0]][1];
                float bx = p[tris[ti][1]][0], by = p[tris[ti][1]][1];
                float cx = p[tris[ti][2]][0], cy = p[tris[ti][2]][1];
                // Грубая отбраковка целиком вне rect (точное — ниже по рёбрам).
                float mnx = ax, mxx = ax, mny = ay, mxy = ay;
                if (bx < mnx) mnx = bx;
                if (bx > mxx) mxx = bx;
                if (cx < mnx) mnx = cx;
                if (cx > mxx) mxx = cx;
                if (by < mny) mny = by;
                if (by > mxy) mxy = by;
                if (cy < mny) mny = cy;
                if (cy > mxy) mxy = cy;
                if (mxx < vpx || mnx > vpx + vpw || mxy < vpy || mny > vpy + vph)
                    continue;
                UiPrim *q = &out[n++];
                q->kind = 3;
                q->a = ax;
                q->b = ay;
                q->c = bx;
                q->d = by;
                q->e = cx;
                q->f = cy;
                q->rgba[0] = cr * mul > 1.0f ? 1.0f : cr * mul;
                q->rgba[1] = cg * mul > 1.0f ? 1.0f : cg * mul;
                q->rgba[2] = cb * mul > 1.0f ? 1.0f : cb * mul;
                q->rgba[3] = 0.85f;
            }
        }
    }
    return n;
}
