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

#define GIZMO_LEN 8.0f
#define GIZMO_HIT 8.0f
#define GIZMO_MINPX 20.0f

struct UiGizmo {
    int tool;
    unsigned sel; // сущность под гизмо (ставит вызывающий)
    int axis; // -1 нет захвата, 0/1/2 ось
    unsigned ent;
    float grabT; // t точки DOWN вдоль оси (в долях длины)
    float grabCoord; // координата сущности по оси на DOWN
    float was; // значение для undo-коммита
    int posed;
};

UiGizmo *ui_gizmo_create(void) {
    UiGizmo *g = (UiGizmo *)calloc(1, sizeof *g);
    if (!g) return 0;
    g->axis = -1;
    return g;
}

void ui_gizmo_destroy(UiGizmo *g) {
    free(g);
}

void ui_gizmo_tool(UiGizmo *g, int tool) {
    if (g) g->tool = tool;
}

void ui_gizmo_select(UiGizmo *g, unsigned ent) {
    if (!g) return;
    if (g->sel != ent) {
        g->sel = ent;
        g->axis = -1; // смена цели сбрасывает захват
    }
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

static float seg_dist(float px, float py, float ax, float ay, float bx, float by, float *t) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float tt = l2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    if (t) *t = tt;
    float cx = ax + dx * tt, cy = ay + dy * tt;
    float ex = px - cx, ey = py - cy;
    return sqrtf(ex * ex + ey * ey);
}

UiReply ui_gizmo_event(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H,
                       UiUndo *undo, const UiEvent *e) {
    if (!g || !e) return ui_unhandled();
    // Модальный drag вдоль оси.
    if (g->axis >= 0) {
        if (e->kind == UI_EV_MOVE) {
            if (!scene || !sc_alive(scene, g->ent)) {
                g->axis = -1;
                return ui_handled(0, 0);
            }
            float ox, oy, axx, ayy;
            ScTransform tr = sc_get(scene, g->ent);
            float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
            float tip[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
            tip[g->axis] += GIZMO_LEN;
            if (!ui_gizmo_project(v, W, H, o, &ox, &oy) ||
                !ui_gizmo_project(v, W, H, tip, &axx, &ayy))
                return ui_handled(0, 1);
            float t = 0;
            seg_dist(e->x, e->y, ox, oy, axx, ayy, &t);
            float coord = g->grabCoord + (t - g->grabT) * GIZMO_LEN;
            tr = sc_get(scene, g->ent);
            ui_field_set_f(&tr, &transform_fields[g->axis], coord);
            sc_set(scene, g->ent, &tr);
            g->posed = 1;
            return ui_handled(1, 1); // держим захват; стрелки едут за сущностью
        }
        if (e->kind == UI_EV_UP) {
            if (scene && sc_alive(scene, g->ent)) {
                ScTransform tr = sc_get(scene, g->ent);
                float now = ui_field_get_f(&tr, &transform_fields[g->axis]);
                if (undo) ui_undo_push(undo, g->ent, g->axis, g->was, now);
            }
            g->axis = -1;
            return ui_handled(1, 0);
        }
        return ui_unhandled();
    }
    if (g->tool != 1 || e->kind != UI_EV_DOWN || e->button != 0) return ui_unhandled();
    // Хит по стрелкам selected (приоритет выше панелей — вызывающий шлёт сюда первым).
    if (!scene || !g->sel) return ui_unhandled();
    return ui_gizmo_pick(g, scene, v, W, H, g->sel, e->x, e->y);
}

// DOWN-хит по конкретной сущности (вызывающий знает selected из панелей).
UiReply ui_gizmo_pick(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H, unsigned ent,
                      float x, float y) {
    if (!g || g->tool != 1 || !scene || !sc_alive(scene, ent)) return ui_unhandled();
    ScTransform tr = sc_get(scene, ent);
    float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
    float ox, oy;
    if (!ui_gizmo_project(v, W, H, o, &ox, &oy)) return ui_unhandled();
    static const float AX[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; a++) {
        float tip[3] = {o[0] + AX[a][0] * GIZMO_LEN, o[1] + AX[a][1] * GIZMO_LEN,
                        o[2] + AX[a][2] * GIZMO_LEN};
        float tx, ty;
        if (!ui_gizmo_project(v, W, H, tip, &tx, &ty)) continue;
        float dx = tx - ox, dy = ty - oy;
        if (dx * dx + dy * dy < GIZMO_MINPX * GIZMO_MINPX) continue; // ось в камеру
        float t = 0;
        if (seg_dist(x, y, ox, oy, tx, ty, &t) > GIZMO_HIT) continue;
        if (t < -0.1f || t > 1.3f) continue; // мимо отрезка (с хвостом на origin)
        g->axis = a;
        g->ent = ent;
        g->grabT = t;
        g->grabCoord = o[a];
        g->was = o[a];
        return ui_handled(0, 1); // захват; грязь будет на MOVE
    }
    return ui_unhandled();
}

int ui_gizmo_build(UiGizmo *g, ScWorld *scene, const RcView *v, int W, int H, UiPrim *out,
                   int cap) {
    if (!g || g->tool != 1 || !scene || !out || cap <= 0) return 0;
    // Рисуем для selected (ставит вызывающий).
    if (!g->sel || !sc_alive(scene, g->sel)) return 0;
    ScTransform tr = sc_get(scene, g->sel);
    float o[3] = {tr.pos.x, tr.pos.y, tr.pos.z};
    float ox, oy;
    if (!ui_gizmo_project(v, W, H, o, &ox, &oy)) return 0;
    static const float AX[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const float COL[3][4] = {{1, 0.15f, 0.1f, 1}, {0.15f, 0.75f, 0.15f, 1},
                                    {0.2f, 0.4f, 1, 1}};
    int n = 0;
    for (int a = 0; a < 3 && n + 3 <= cap; a++) {
        float tip[3] = {o[0] + AX[a][0] * GIZMO_LEN, o[1] + AX[a][1] * GIZMO_LEN,
                        o[2] + AX[a][2] * GIZMO_LEN};
        float tx, ty;
        if (!ui_gizmo_project(v, W, H, tip, &tx, &ty)) continue;
        float dx = tx - ox, dy = ty - oy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < GIZMO_MINPX) continue;
        // Стрела + наконечник (две короткие под 25°, 12px).
        float ux = dx / len, uy = dy / len;
        float hx = -uy, hy = ux;
        float hl = 12.0f;
        UiPrim *p = &out[n++];
        p->kind = 2;
        p->a = ox;
        p->b = oy;
        p->c = tx;
        p->d = ty;
        memcpy(p->rgba, COL[a], sizeof p->rgba);
        float c1 = 0.42f, s1 = 0.42f; // cos/sin 25° экономно
        UiPrim *q1 = &out[n++];
        q1->kind = 2;
        q1->a = tx;
        q1->b = ty;
        q1->c = tx - (ux * c1 - hx * s1) * hl;
        q1->d = ty - (uy * c1 - hy * s1) * hl;
        memcpy(q1->rgba, COL[a], sizeof q1->rgba);
        UiPrim *q2 = &out[n++];
        q2->kind = 2;
        q2->a = tx;
        q2->b = ty;
        q2->c = tx - (ux * c1 + hx * s1) * hl;
        q2->d = ty - (uy * c1 + hy * s1) * hl;
        memcpy(q2->rgba, COL[a], sizeof q2->rgba);
    }
    return n;
}
