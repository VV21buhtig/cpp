// Проект и сцена в C-бекенде. Логика переехала из Qt-панелей слово в слово
// (поведение и формат файлов НЕ менялись — только владелец).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "project.h"

int eng_scene_save(const ScWorld *w, const char *dir) {
    if (!w || !dir) return 0;
    char fn[1024];
    snprintf(fn, sizeof fn, "%s/scene.txt", dir);
    FILE *f = fopen(fn, "w");
    if (!f) return 0;
    // Индекс в файле = порядок списка (родители — индексами в него).
    Entity list[1024];
    int n = sc_list(w, list, 1024);
    int idxOf[1025];
    memset(idxOf, -1, sizeof idxOf);
    for (int i = 0; i < n; i++) {
        int id = (int)list[i];
        if (id >= 1 && id <= 1024) idxOf[id] = i;
    }
    fprintf(f, "v 2\n"); // версия формата (v1 без шапки — legacy ниже)
    for (int i = 0; i < n; i++) {
        // Родитель — индексом в этом же файле (-1 корень).
        int pi = -1;
        int internal = (int)list[i] - 1;
        if (internal >= 0 && internal < 1024 && w->parent[internal] >= 0) {
            int pe = w->parent[internal] + 1;
            if (pe >= 1 && pe <= 1024) pi = idxOf[pe];
        }
        ScTransform t = sc_get(w, list[i]);
        ScMaterial m = sc_mat_get(w, list[i]);
        ScAnchor a = sc_anchor_get(w, list[i]);
        ScPhysics ph = sc_phys_get(w, list[i]);
        // v2: Этап Б дописал 16 полей (материал 6, якорь 5, физика 5).
        fprintf(f, "e %s %d %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f",
                sc_name(w, list[i]), pi, sc_visible(w, list[i]), t.pos.x, t.pos.y, t.pos.z,
                t.rot.x, t.rot.y, t.rot.z, t.scale.x, t.scale.y, t.scale.z);
        fprintf(f, " %.4f %.4f %.4f %.4f %.4f %.4f", m.col.x, m.col.y, m.col.z, m.a,
                m.rough, m.metal);
        fprintf(f, " %d %d %.4f %.4f %.4f", a.mode, a.follow, a.off.x, a.off.y, a.off.z);
        fprintf(f, " %d %.4f %.4f %.4f %.4f\n", ph.dyn, ph.mass, ph.vel.x, ph.vel.y,
                ph.vel.z);
    }
    fclose(f);
    return 1;
}

int eng_scene_load(ScWorld *w, const char *dir) {
    if (!w || !dir) return 0;
    char fn[1024];
    snprintf(fn, sizeof fn, "%s/scene.txt", dir);
    FILE *f = fopen(fn, "rb");
    if (!f) return 0; // нет файла — вызыватель решает
    // Чистим текущих.
    Entity list[1024];
    int n = sc_list(w, list, 1024);
    for (int i = 0; i < n; i++) sc_kill(w, list[i]);
    // Проход 1: сущности. Проход 2: родители (порядок в файле любой).
    Entity ids[1024];
    int wantP[1024];
    int nn = 0;
    char name[64];
    int pidx, vis;
    ScTransform t;
    // Версия: "v 2" шапкой, иначе legacy v1 (12 полей, компоненты — дефолт).
    int v2 = 0;
    {
        char tag[8] = {0};
        long at = ftell(f);
        if (fscanf(f, " %7s", tag) == 1 && !strcmp(tag, "v")) {
            int vn = 0;
            if (fscanf(f, " %d", &vn) == 1 && vn == 2) v2 = 1;
        }
        if (!v2) fseek(f, at, SEEK_SET);
    }
    ScMaterial m;
    ScAnchor a;
    ScPhysics ph;
    while (nn < 1024) {
        int got;
        if (v2) {
            // v2: 12 legacy + 16 компонентов (см. save выше).
            memset(&m, 0, sizeof m);
            memset(&a, 0, sizeof a);
            memset(&ph, 0, sizeof ph);
            got = fscanf(f, " e %63s %d %d %f %f %f %f %f %f %f %f %f", name, &pidx,
                         &vis, &t.pos.x, &t.pos.y, &t.pos.z, &t.rot.x, &t.rot.y,
                         &t.rot.z, &t.scale.x, &t.scale.y, &t.scale.z);
            if (got != 12) break;
            got = fscanf(f, " %f %f %f %f %f %f", &m.col.x, &m.col.y, &m.col.z, &m.a,
                         &m.rough, &m.metal);
            if (got != 6) break;
            got = fscanf(f, " %d %d %f %f %f", &a.mode, &a.follow, &a.off.x, &a.off.y,
                         &a.off.z);
            if (got != 5) break;
            got = fscanf(f, " %d %f %f %f %f", &ph.dyn, &ph.mass, &ph.vel.x, &ph.vel.y,
                         &ph.vel.z);
            if (got != 5) break;
        } else {
            got = fscanf(f, " e %63s %d %d %f %f %f %f %f %f %f %f %f", name, &pidx,
                         &vis, &t.pos.x, &t.pos.y, &t.pos.z, &t.rot.x, &t.rot.y,
                         &t.rot.z, &t.scale.x, &t.scale.y, &t.scale.z);
            if (got != 12) break;
        }
        Entity e = sc_spawn(w, name);
        if (!e) break;
        sc_set(w, e, &t);
        sc_show(w, e, vis);
        if (v2) { // v1 оставляет дефолты спавна
            sc_mat_set(w, e, &m);
            sc_anchor_set(w, e, &a);
            if (ph.mass > 0) sc_phys_set(w, e, &ph); // масса обязана быть >0
        }
        ids[nn] = e;
        wantP[nn] = pidx;
        nn++;
    }
    fclose(f);
    for (int i = 0; i < nn; i++)
        if (wantP[i] >= 0 && wantP[i] < nn) sc_parent(w, ids[i], ids[wantP[i]]);
    return 1;
}

// --- JSON для тонкого клиента ---
static void json_grow(char **buf, size_t *cap, size_t need) {
    if (need <= *cap) return;
    size_t ncap = *cap ? *cap : 4096;
    while (ncap < need) ncap *= 2;
    char *nb = (char *)realloc(*buf, ncap);
    if (nb) {
        *buf = nb;
        *cap = ncap;
    }
}

static void json_put(char **buf, size_t *cap, size_t *len, const char *s, size_t n) {
    json_grow(buf, cap, *len + n + 1);
    if (!*buf) return;
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
}

static void json_ch(char **buf, size_t *cap, size_t *len, char c) {
    json_put(buf, cap, len, &c, 1);
}

static void json_str(char **buf, size_t *cap, size_t *len, const char *s) {
    json_ch(buf, cap, len, '"');
    for (const char *p = s; *p; p++) {
        // Имена сущностей без пробелов, но экранируем честно.
        if (*p == '"' || *p == '\\') json_ch(buf, cap, len, '\\');
        json_ch(buf, cap, len, *p);
    }
    json_ch(buf, cap, len, '"');
}

static void json_num(char **buf, size_t *cap, size_t *len, double v) {
    char tmp[32];
    int n = snprintf(tmp, sizeof tmp, "%.4f", v);
    json_put(buf, cap, len, tmp, (size_t)(n > 0 ? n : 0));
}

// Одна сущность (Этап Б): тот же набор ключей что сцена целиком.
// Вызывающий: eng_scene_json (массив) и eng_scene_entity_json (штучно).
static void json_vec3(char **buf, size_t *cap, size_t *len, const char *key, size_t klen,
                      float x, float y, float z) {
    json_put(buf, cap, len, key, klen);
    json_num(buf, cap, len, x);
    json_ch(buf, cap, len, ',');
    json_num(buf, cap, len, y);
    json_ch(buf, cap, len, ',');
    json_num(buf, cap, len, z);
    json_ch(buf, cap, len, ']');
}

static void entity_put(char **buf, size_t *cap, size_t *len, const ScWorld *w, Entity e) {
    int internal = (int)e - 1;
    int pe = (internal >= 0 && internal < 1024) ? (int)w->parent[internal] : -1;
    ScTransform t = sc_get(w, e);
    ScMaterial m = sc_mat_get(w, e);
    ScAnchor a = sc_anchor_get(w, e);
    ScPhysics ph = sc_phys_get(w, e);
    json_put(buf, cap, len, "{\"id\":", 6);
    char idb[16];
    int idn = snprintf(idb, sizeof idb, "%d", (int)e);
    json_put(buf, cap, len, idb, (size_t)(idn > 0 ? idn : 0));
    json_put(buf, cap, len, ",\"name\":", 8);
    json_str(buf, cap, len, sc_name(w, e));
    json_put(buf, cap, len, ",\"parent\":", 10);
    {
        char pb[16];
        int pn = snprintf(pb, sizeof pb, "%d", pe >= 0 ? pe + 1 : 0);
        json_put(buf, cap, len, pb, (size_t)(pn > 0 ? pn : 0));
    }
    json_put(buf, cap, len, ",\"visible\":", 11);
    json_ch(buf, cap, len, sc_visible(w, e) ? '1' : '0');
    json_vec3(buf, cap, len, ",\"pos\":[", 8, t.pos.x, t.pos.y, t.pos.z);
    json_vec3(buf, cap, len, ",\"rot\":[", 8, t.rot.x, t.rot.y, t.rot.z);
    json_vec3(buf, cap, len, ",\"scale\":[", 10, t.scale.x, t.scale.y, t.scale.z);
    // Этап Б: материал/якорь/физика.
    json_vec3(buf, cap, len, ",\"mat\":[", 8, m.col.x, m.col.y, m.col.z);
    json_put(buf, cap, len, ",\"alpha\":", 9);
    json_num(buf, cap, len, m.a);
    json_put(buf, cap, len, ",\"rough\":", 9);
    json_num(buf, cap, len, m.rough);
    json_put(buf, cap, len, ",\"metal\":", 9);
    json_num(buf, cap, len, m.metal);
    {
        char ab[64];
        int an = snprintf(ab, sizeof ab, ",\"anchor\":%d,\"follow\":%d", a.mode, a.follow);
        json_put(buf, cap, len, ab, (size_t)(an > 0 ? an : 0));
    }
    json_vec3(buf, cap, len, ",\"off\":[", 8, a.off.x, a.off.y, a.off.z);
    {
        char pb2[64];
        int pn2 = snprintf(pb2, sizeof pb2, ",\"dyn\":%d", ph.dyn ? 1 : 0);
        json_put(buf, cap, len, pb2, (size_t)(pn2 > 0 ? pn2 : 0));
    }
    json_put(buf, cap, len, ",\"mass\":", 8);
    json_num(buf, cap, len, ph.mass);
    json_vec3(buf, cap, len, ",\"vel\":[", 8, ph.vel.x, ph.vel.y, ph.vel.z);
    json_put(buf, cap, len, "}", 1);
}

char *eng_scene_json(const ScWorld *w) {
    if (!w) return 0;
    char *buf = 0;
    size_t cap = 0, len = 0;
    Entity list[1024];
    int n = sc_list(w, list, 1024);
    json_put(&buf, &cap, &len, "{\"entities\":[", 13);
    for (int i = 0; i < n; i++) {
        if (i) json_ch(&buf, &cap, &len, ',');
        entity_put(&buf, &cap, &len, w, list[i]);
        if (!buf) return 0; // память не дали — честный NULL
    }
    json_put(&buf, &cap, &len, "]}", 2);
    return buf;
}

char *eng_scene_entity_json(const ScWorld *w, Entity e) {
    if (!w || !sc_alive(w, e)) return 0;
    char *buf = 0;
    size_t cap = 0, len = 0;
    entity_put(&buf, &cap, &len, w, e);
    return buf; // NULL если памяти нет
}

// --- Парсер одной сущности (плоские ключи; частичное обновление) ---
static const char *key_at(const char *s, const char *key) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    f += strlen(pat);
    while (*f == ' ' || *f == '\t') f++;
    if (*f != ':') return 0;
    f++;
    while (*f == ' ' || *f == '\t') f++;
    return f;
}

static int num_at(const char *s, float *out) {
    if (!s || !out) return 0;
    char *end = 0;
    double v = strtod(s, &end);
    if (end == s) return 0;
    if (v != v || v > 1e30 || v < -1e30) return 0; // NaN/inf/бред — отказ
    *out = (float)v;
    return 1;
}

static int arr3_at(const char *s, const char *key, float out[3]) {
    const char *f = key_at(s, key);
    if (!f || *f != '[') return 0;
    f++;
    for (int i = 0; i < 3; i++) {
        while (*f == ' ' || *f == '\t') f++;
        if (!num_at(f, &out[i])) return 0;
        while (*f && *f != ',' && *f != ']') f++;
        if (i < 2) {
            if (*f != ',') return 0;
            f++;
        }
    }
    return 1;
}

// Применить JSON к сущности (только present-ключи). Атомарно: битый JSON
// ничего не меняет. 1 ок, 0 отказ (нет сущности/JSON/числа, масса <= 0).
int eng_scene_entity_parse(ScWorld *w, Entity e, const char *js) {
    if (!w || !js || !sc_alive(w, e)) return 0;
    while (*js == ' ' || *js == '\t' || *js == '\n' || *js == '\r') js++;
    if (*js != '{') return 0; // не объект — отказ сразу (а не тихий no-op)
    ScTransform t = sc_get(w, e);
    ScMaterial m = sc_mat_get(w, e);
    ScAnchor a = sc_anchor_get(w, e);
    ScPhysics ph = sc_phys_get(w, e);
    int vis = sc_visible(w, e);
    char name[SC_NAME];
    strncpy(name, sc_name(w, e), sizeof name - 1);
    name[sizeof name - 1] = 0;
    int haveName = 0;
    float v3[3];
    float v;
    int iv;
    const char *f;
    if (arr3_at(js, "pos", v3)) {
        t.pos.x = v3[0];
        t.pos.y = v3[1];
        t.pos.z = v3[2];
    }
    if (arr3_at(js, "rot", v3)) {
        t.rot.x = v3[0];
        t.rot.y = v3[1];
        t.rot.z = v3[2];
    }
    if (arr3_at(js, "scale", v3)) {
        t.scale.x = v3[0];
        t.scale.y = v3[1];
        t.scale.z = v3[2];
    }
    if (arr3_at(js, "mat", v3)) {
        m.col.x = v3[0];
        m.col.y = v3[1];
        m.col.z = v3[2];
    }
    if (arr3_at(js, "vel", v3)) {
        ph.vel.x = v3[0];
        ph.vel.y = v3[1];
        ph.vel.z = v3[2];
    }
    if (arr3_at(js, "off", v3)) {
        a.off.x = v3[0];
        a.off.y = v3[1];
        a.off.z = v3[2];
    }
    // Скаляры: присутствующий, но битый ключ = отказ всего (атомарность).
    if ((f = key_at(js, "alpha")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v)) return 0;
        m.a = v;
    }
    if ((f = key_at(js, "rough")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v)) return 0;
        m.rough = v;
    }
    if ((f = key_at(js, "metal")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v)) return 0;
        m.metal = v;
    }
    if ((f = key_at(js, "mass")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v) || v <= 0) return 0;
        ph.mass = v;
    }
    if ((f = key_at(js, "visible")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v)) return 0;
        vis = v != 0;
    }
    if ((f = key_at(js, "anchor")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v) || v < 0 || v > 2) return 0;
        a.mode = (int)v;
    }
    if ((f = key_at(js, "follow")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v) || v < 0) return 0;
        iv = (int)v;
        if (iv != 0 && !sc_alive(w, (Entity)iv)) return 0; // цель обязана жить
        a.follow = iv;
    }
    if ((f = key_at(js, "dyn")) && *f != '}' && *f != ',') {
        if (!num_at(f, &v)) return 0;
        ph.dyn = v != 0;
    }
    if ((f = key_at(js, "name")) && *f == '"') {
        f++;
        int ni = 0;
        while (*f && *f != '"' && ni < SC_NAME - 1) name[ni++] = *f++;
        if (*f != '"') return 0;
        name[ni] = 0;
        if (!ni) return 0;
        haveName = 1;
    }
    // Битые массивы при present-ключах: отказ (проверяем только те, что есть).
    if (strstr(js, "\"pos\"") && !arr3_at(js, "pos", v3)) return 0;
    if (strstr(js, "\"rot\"") && !arr3_at(js, "rot", v3)) return 0;
    if (strstr(js, "\"scale\"") && !arr3_at(js, "scale", v3)) return 0;
    if (strstr(js, "\"mat\"") && !arr3_at(js, "mat", v3)) return 0;
    if (strstr(js, "\"vel\"") && !arr3_at(js, "vel", v3)) return 0;
    if (strstr(js, "\"off\"") && !arr3_at(js, "off", v3)) return 0;
    sc_set(w, e, &t);
    sc_mat_set(w, e, &m);
    sc_anchor_set(w, e, &a);
    sc_phys_set(w, e, &ph);
    sc_show(w, e, vis);
    if (haveName) sc_rename(w, e, name);
    return 1;
}

// --- Создание проекта ---
static int mkone(const char *path) {
    if (mkdir(path, 0755) && errno != EEXIST) return 0;
    return 1;
}

int eng_project_create(const char *root, const char *name, const char *tpl, int backend, char *err,
                       int errcap) {
    // Валидация та же что была в хабе (формат scene.txt не терпит пробелов).
    if (!name || !name[0]) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "пустое имя");
        return 0;
    }
    if (strlen(name) > 64) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "имя длиннее 64");
        return 0;
    }
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ' ' || *p == '\t' ||
            (unsigned char)*p < 32) {
            if (err && errcap > 0) snprintf(err, (size_t)errcap, "имя: без / \\ и пробелов");
            return 0;
        }
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "плохое имя");
        return 0;
    }
    if (!root || !root[0]) root = ".";
    if (!tpl) tpl = "Empty";
    if (backend < 1 || backend > 3) backend = 2;
    char dir[1024], depot[1024];
    snprintf(dir, sizeof dir, "%s/%s", root, name);
    char proj[1024];
    snprintf(proj, sizeof proj, "%s/project.txt", dir);
    FILE *probe = fopen(proj, "rb");
    if (probe) {
        fclose(probe);
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "уже есть");
        return 0;
    }
    snprintf(depot, sizeof depot, "%s/depot", dir);
    if (!mkone(dir) || !mkone(depot)) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "каталог не создать");
        return 0;
    }
    FILE *f = fopen(proj, "w");
    if (!f) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "project.txt не записать");
        return 0;
    }
    fprintf(f, "name=%s\ntemplate=%s\nbackend=%d\nversion=0.1.0\n", name, tpl, backend);
    fclose(f);
    ScWorld w;
    sc_init(&w);
    sc_spawn(&w, "Scene");
    sc_spawn(&w, "Camera");
    if (strcmp(tpl, "Empty")) {
        ScTransform t;
        memset(&t, 0, sizeof t);
        t.scale.x = t.scale.y = t.scale.z = 1.0f;
        Entity light = sc_spawn(&w, "Light");
        if (light) {
            t.pos.x = 10;
            t.pos.y = 20;
            t.pos.z = 10;
            sc_set(&w, light, &t);
        }
        sc_spawn(&w, "Mesh");
    }
    if (!eng_scene_save(&w, dir)) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "scene.txt не записать");
        return 0;
    }
    return 1;
}
