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
    for (int i = 0; i < n; i++) {
        // Родитель — индексом в этом же файле (-1 корень).
        int pi = -1;
        int internal = (int)list[i] - 1;
        if (internal >= 0 && internal < 1024 && w->parent[internal] >= 0) {
            int pe = w->parent[internal] + 1;
            if (pe >= 1 && pe <= 1024) pi = idxOf[pe];
        }
        ScTransform t = sc_get(w, list[i]);
        fprintf(f, "e %s %d %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                sc_name(w, list[i]), pi, sc_visible(w, list[i]), t.pos.x, t.pos.y, t.pos.z,
                t.rot.x, t.rot.y, t.rot.z, t.scale.x, t.scale.y, t.scale.z);
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
    while (nn < 1024 &&
           fscanf(f, " e %63s %d %d %f %f %f %f %f %f %f %f %f", name, &pidx, &vis, &t.pos.x,
                  &t.pos.y, &t.pos.z, &t.rot.x, &t.rot.y, &t.rot.z, &t.scale.x, &t.scale.y,
                  &t.scale.z) == 12) {
        Entity e = sc_spawn(w, name);
        if (!e) break;
        sc_set(w, e, &t);
        sc_show(w, e, vis);
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

char *eng_scene_json(const ScWorld *w) {
    if (!w) return 0;
    char *buf = 0;
    size_t cap = 0, len = 0;
    Entity list[1024];
    int n = sc_list(w, list, 1024);
    json_put(&buf, &cap, &len, "{\"entities\":[", 13);
    for (int i = 0; i < n; i++) {
        Entity e = list[i];
        int internal = (int)e - 1;
        int pe = (internal >= 0 && internal < 1024) ? (int)w->parent[internal] : -1;
        ScTransform t = sc_get(w, e);
        if (i) json_ch(&buf, &cap, &len, ',');
        json_put(&buf, &cap, &len, "{\"id\":", 6);
        char idb[16];
        int idn = snprintf(idb, sizeof idb, "%d", (int)e);
        json_put(&buf, &cap, &len, idb, (size_t)(idn > 0 ? idn : 0));
        json_put(&buf, &cap, &len, ",\"name\":", 8);
        json_str(&buf, &cap, &len, sc_name(w, e));
        json_put(&buf, &cap, &len, ",\"parent\":", 10);
        {
            char pb[16];
            int pn = snprintf(pb, sizeof pb, "%d", pe >= 0 ? pe + 1 : 0);
            json_put(&buf, &cap, &len, pb, (size_t)(pn > 0 ? pn : 0));
        }
        json_put(&buf, &cap, &len, ",\"visible\":", 11);
        json_ch(&buf, &cap, &len, sc_visible(w, e) ? '1' : '0');
        json_put(&buf, &cap, &len, ",\"pos\":[", 8);
        json_num(&buf, &cap, &len, t.pos.x);
        json_ch(&buf, &cap, &len, ',');
        json_num(&buf, &cap, &len, t.pos.y);
        json_ch(&buf, &cap, &len, ',');
        json_num(&buf, &cap, &len, t.pos.z);
        json_ch(&buf, &cap, &len, ']');
        json_put(&buf, &cap, &len, ",\"rot\":[", 8);
        json_num(&buf, &cap, &len, t.rot.x);
        json_ch(&buf, &cap, &len, ',');
        json_num(&buf, &cap, &len, t.rot.y);
        json_ch(&buf, &cap, &len, ',');
        json_num(&buf, &cap, &len, t.rot.z);
        json_ch(&buf, &cap, &len, ']');
        json_put(&buf, &cap, &len, ",\"scale\":[", 10);
        json_num(&buf, &cap, &len, t.scale.x);
        json_ch(&buf, &cap, &len, ',');
        json_num(&buf, &cap, &len, t.scale.y);
        json_ch(&buf, &cap, &len, ',');
        json_num(&buf, &cap, &len, t.scale.z);
        json_put(&buf, &cap, &len, "]}", 2);
        if (!buf) return 0; // память не дали — честный NULL
    }
    json_put(&buf, &cap, &len, "]}", 2);
    return buf;
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
