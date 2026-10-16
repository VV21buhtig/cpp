// project.db (Этап Б2): SQLite-хранилище проекта — entities + журнал.
// Soft delete (deleted=1), undo/redo по журналу (rev-флаг; новая мутация
// убивает redo как ui_undo). Вызовы редкие (не кадр) — open/close на операцию
// нет: хендл EngDb живёт у вызывателя. Malloc только тут.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sqlite3.h>
#include "project.h"
#include "graph.h"

struct EngDb {
    sqlite3 *db;
};

#define DBF_NAMELEN 16

// Поле мира: компонент 0=T 1=M 2=A 3=P, 4=спец (name/visible/parent);
// off — оффсет в компоненте (спец: 0/1/2). Таблицу проверяет db_test:
// число записей == сумме X-списков + 3 (дрейф ловится).
typedef struct {
    char name[DBF_NAMELEN];
    int comp;
    size_t off;
    int isInt;
} DbField;

#define DB_XM(t, n) {#n, 1, offsetof(ScMaterial, n), !strcmp(#t, "int")},
#define DB_XA(t, n) {#n, 2, offsetof(ScAnchor, n), !strcmp(#t, "int")},
#define DB_XP(t, n) {#n, 3, offsetof(ScPhysics, n), !strcmp(#t, "int")},
#define DB_XT(t, n) {#n, 0, offsetof(ScTransform, n), !strcmp(#t, "int")},
static const DbField dbfields[] = {
    DB_XT(float, pos.x) DB_XT(float, pos.y) DB_XT(float, pos.z) DB_XT(float, rot.x)
        DB_XT(float, rot.y) DB_XT(float, rot.z) DB_XT(float, scale.x)
            DB_XT(float, scale.y) DB_XT(float, scale.z)
    SC_MATERIAL_FIELDS(DB_XM) SC_ANCHOR_FIELDS(DB_XA) SC_PHYSICS_FIELDS(DB_XP)
    {"name", 4, 0, 0},
    {"visible", 4, 1, 1},
    {"parent", 4, 2, 1},
};
#undef DB_XM
#undef DB_XA
#undef DB_XP
#undef DB_XT
#define DB_NFIELDS (int)(sizeof dbfields / sizeof dbfields[0])

int eng_db_nfields(void) {
    return DB_NFIELDS;
}

static const DbField *db_find(const char *field) {
    if (!field) return 0;
    for (int i = 0; i < DB_NFIELDS; i++)
        if (!strcmp(dbfields[i].name, field)) return &dbfields[i];
    return 0;
}

static const char *db_col(const DbField *f) {
    // Колонка = имя поля без точки (pos.x -> posx, col.x -> colx; quoted).
    // Имена только из таблицы выше — инъекции неоткуда.
    static char col[DBF_NAMELEN + 2];
    if (f->comp == 4) {
        snprintf(col, sizeof col, "\"%s\"", f->name);
        return col;
    }
    char tmp[DBF_NAMELEN];
    int j = 0;
    for (int i = 0; f->name[i] && j < DBF_NAMELEN - 1; i++)
        if (f->name[i] != '.') tmp[j++] = f->name[i];
    tmp[j] = 0;
    snprintf(col, sizeof col, "\"%s\"", tmp);
    return col;
}

EngDb *eng_db_open(const char *dir) {
    if (!dir || !dir[0]) return 0;
    char path[1152];
    snprintf(path, sizeof path, "%s/project.db", dir);
    EngDb *d = (EngDb *)calloc(1, sizeof *d);
    if (!d) return 0;
    if (sqlite3_open(path, &d->db) != SQLITE_OK) {
        free(d);
        return 0;
    }
    const char *schema =
        "CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY,name TEXT,"
        "parent INTEGER,visible INTEGER,deleted INTEGER DEFAULT 0,"
        "posx REAL,posy REAL,posz REAL,rotx REAL,roty REAL,rotz REAL,"
        "scalex REAL,scaley REAL,scalez REAL,"
        "colx REAL,coly REAL,colz REAL,alpha REAL,rough REAL,metal REAL,"
        "mode INTEGER,follow INTEGER,offx REAL,offy REAL,offz REAL,"
        "dyn INTEGER,mass REAL,velx REAL,vely REAL,velz REAL);"
        "CREATE TABLE IF NOT EXISTS journal(seq INTEGER PRIMARY KEY "
        "AUTOINCREMENT,op TEXT,ent INTEGER,field TEXT,before TEXT,after TEXT,"
        "rev INTEGER DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS nodes(id INTEGER PRIMARY KEY,type INTEGER,"
        "p0 REAL,p1 REAL,x REAL,y REAL);"
        "CREATE TABLE IF NOT EXISTS links(id INTEGER PRIMARY KEY "
        "AUTOINCREMENT,fn INTEGER,fp INTEGER,tn INTEGER,tp INTEGER);";
    char *err = 0;
    if (sqlite3_exec(d->db, schema, 0, 0, &err) != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(d->db);
        free(d);
        return 0;
    }
    return d;
}

void eng_db_close(EngDb *db) {
    if (!db) return;
    sqlite3_close(db->db);
    free(db);
}

int eng_db_count(EngDb *db, int *nents, int *njournal) {
    if (!db) return 0;
    sqlite3_stmt *st = 0;
    if (nents) {
        *nents = -1;
        if (sqlite3_prepare_v2(db->db, "SELECT COUNT(*) FROM entities WHERE deleted=0;",
                               -1, &st, 0) == SQLITE_OK &&
            sqlite3_step(st) == SQLITE_ROW)
            *nents = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        st = 0;
    }
    if (njournal) {
        *njournal = -1;
        if (sqlite3_prepare_v2(db->db, "SELECT COUNT(*) FROM journal;", -1, &st, 0) ==
                SQLITE_OK &&
            sqlite3_step(st) == SQLITE_ROW)
            *njournal = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return 1;
}

// --- значения полей миром <-> текст ---
static void field_get_text(const ScWorld *w, int internal, const DbField *f, char *out,
                           int cap) {
    if (f->comp == 4) { // спец
        if (f->off == 0) snprintf(out, (size_t)cap, "%s", w->name[internal]);
        else if (f->off == 1) snprintf(out, (size_t)cap, "%d", w->visible[internal]);
        else {
            int p = w->parent[internal];
            snprintf(out, (size_t)cap, "%d", p >= 0 ? p + 1 : 0);
        }
        return;
    }
    const char *base = 0;
    if (f->comp == 0) base = (const char *)&w->tr[internal];
    else if (f->comp == 1) base = (const char *)&w->mat[internal];
    else if (f->comp == 2) base = (const char *)&w->anchor[internal];
    else base = (const char *)&w->phys[internal];
    if (f->isInt) snprintf(out, (size_t)cap, "%d", *(const int *)(base + f->off));
    else snprintf(out, (size_t)cap, "%.4f", *(const float *)(base + f->off));
}

// Применить текст к миру (валидация как parse: масса>0, follow живёт,
// anchor 0..2, имя непустое). 1 ок, 0 отказ (мир не тронут вызывателем? нет:
// вызыватель сам решает — тут только пишем, валидация до записи снаружи).
static int field_set_text(ScWorld *w, int internal, const DbField *f, const char *v) {
    if (!v) return 0;
    if (f->comp == 4) {
        if (f->off == 0) {
            if (!v[0] || strlen(v) >= SC_NAME) return 0;
            snprintf(w->name[internal], SC_NAME, "%s", v);
            return 1;
        }
        char *end = 0;
        long iv = strtol(v, &end, 10);
        if (end == v || *end) return 0;
        if (f->off == 1) {
            w->visible[internal] = iv ? 1 : 0;
            return 1;
        }
        if (iv == 0) {
            w->parent[internal] = -1;
            return 1;
        }
        int pi = (int)iv - 1;
        if (pi < 0 || pi >= SC_MAX || !w->alive[pi] || pi == internal) return 0;
        w->parent[internal] = pi;
        return 1;
    }
    if (f->isInt) {
        char *end = 0;
        long iv = strtol(v, &end, 10);
        if (end == v || *end) return 0;
        if (!strcmp(f->name, "mode") && (iv < 0 || iv > 2)) return 0;
        if (!strcmp(f->name, "follow") && iv != 0) {
            int pi = (int)iv - 1;
            if (pi < 0 || pi >= SC_MAX || !w->alive[pi]) return 0;
        }
        char *base = 0;
        if (f->comp == 0) base = (char *)&w->tr[internal];
        else if (f->comp == 1) base = (char *)&w->mat[internal];
        else if (f->comp == 2) base = (char *)&w->anchor[internal];
        else base = (char *)&w->phys[internal];
        *(int *)(base + f->off) = (int)iv;
        return 1;
    }
    char *end = 0;
    double dv = strtod(v, &end);
    if (end == v || *end || dv != dv || dv > 1e30 || dv < -1e30) return 0;
    if (!strcmp(f->name, "mass") && dv <= 0) return 0;
    char *base = 0;
    if (f->comp == 0) base = (char *)&w->tr[internal];
    else if (f->comp == 1) base = (char *)&w->mat[internal];
    else if (f->comp == 2) base = (char *)&w->anchor[internal];
    else base = (char *)&w->phys[internal];
    *(float *)(base + f->off) = (float)dv;
    return 1;
}

static int db_exec(EngDb *db, const char *sql) {
    char *err = 0;
    int rc = sqlite3_exec(db->db, sql, 0, 0, &err);
    sqlite3_free(err);
    return rc == SQLITE_OK;
}

int eng_db_sync(EngDb *db, const ScWorld *w) {
    if (!db || !w) return 0;
    if (!db_exec(db, "BEGIN;")) return 0;
    if (!db_exec(db, "DELETE FROM entities;")) {
        db_exec(db, "ROLLBACK;");
        return 0;
    }
    const char *ins =
        "INSERT INTO entities(id,name,parent,visible,deleted,"
        "posx,posy,posz,rotx,roty,rotz,scalex,scaley,scalez,"
        "colx,coly,colz,alpha,rough,metal,mode,follow,offx,offy,offz,"
        "dyn,mass,velx,vely,velz) VALUES("
        "?1,?2,?3,?4,0,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,"
        "?20,?21,?22,?23,?24,?25,?26,?27,?28,?29);";
    Entity list[1024];
    int n = sc_list(w, list, 1024);
    for (int k = 0; k < n; k++) {
        int i = (int)list[k] - 1;
        sqlite3_stmt *st = 0;
        if (sqlite3_prepare_v2(db->db, ins, -1, &st, 0) != SQLITE_OK) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
        int p = w->parent[i];
        sqlite3_bind_int(st, 1, (int)list[k]);
        sqlite3_bind_text(st, 2, w->name[i], -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, p >= 0 ? p + 1 : 0);
        sqlite3_bind_int(st, 4, w->visible[i]);
        const float *t = (const float *)&w->tr[i]; // 9 float подряд
        for (int q = 0; q < 9; q++) sqlite3_bind_double(st, 5 + q, t[q]);
        const ScMaterial *m = &w->mat[i];
        sqlite3_bind_double(st, 14, m->col.x);
        sqlite3_bind_double(st, 15, m->col.y);
        sqlite3_bind_double(st, 16, m->col.z);
        sqlite3_bind_double(st, 17, m->a);
        sqlite3_bind_double(st, 18, m->rough);
        sqlite3_bind_double(st, 19, m->metal);
        sqlite3_bind_int(st, 20, w->anchor[i].mode);
        sqlite3_bind_int(st, 21, w->anchor[i].follow);
        sqlite3_bind_double(st, 22, w->anchor[i].off.x);
        sqlite3_bind_double(st, 23, w->anchor[i].off.y);
        sqlite3_bind_double(st, 24, w->anchor[i].off.z);
        sqlite3_bind_int(st, 25, w->phys[i].dyn);
        sqlite3_bind_double(st, 26, w->phys[i].mass);
        sqlite3_bind_double(st, 27, w->phys[i].vel.x);
        sqlite3_bind_double(st, 28, w->phys[i].vel.y);
        sqlite3_bind_double(st, 29, w->phys[i].vel.z);
        int ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
    }
    return db_exec(db, "COMMIT;");
}

int eng_db_load(EngDb *db, ScWorld *w) {
    if (!db || !w) return 0;
    Entity old[1024];
    int on = sc_list(w, old, 1024);
    for (int i = 0; i < on; i++) sc_kill(w, old[i]);
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db->db,
                           "SELECT id,name,parent,visible,posx,posy,posz,rotx,roty,rotz,"
                           "scalex,scaley,scalez,colx,coly,colz,alpha,rough,metal,mode,follow,"
                           "offx,offy,offz,dyn,mass,velx,vely,velz FROM entities "
                           "WHERE deleted=0 ORDER BY id;",
                           -1, &st, 0) != SQLITE_OK)
        return 0;
    // rowid -> новая сущность (id при дырах едут — родителей вяжем картой).
    int rowIds[1024];
    Entity fresh[1024];
    int nn = 0;
    int wantP[1024];
    while (nn < 1024 && sqlite3_step(st) == SQLITE_ROW) {
        rowIds[nn] = sqlite3_column_int(st, 0);
        Entity e = sc_spawn(w, (const char *)sqlite3_column_text(st, 1));
        if (!e) break;
        int i = (int)e - 1;
        w->visible[i] = sqlite3_column_int(st, 3) ? 1 : 0;
        float *t = (float *)&w->tr[i];
        for (int q = 0; q < 9; q++) t[q] = (float)sqlite3_column_double(st, 4 + q);
        ScMaterial *m = &w->mat[i];
        m->col.x = (float)sqlite3_column_double(st, 13);
        m->col.y = (float)sqlite3_column_double(st, 14);
        m->col.z = (float)sqlite3_column_double(st, 15);
        m->a = (float)sqlite3_column_double(st, 16);
        m->rough = (float)sqlite3_column_double(st, 17);
        m->metal = (float)sqlite3_column_double(st, 18);
        w->anchor[i].mode = sqlite3_column_int(st, 19);
        w->anchor[i].follow = sqlite3_column_int(st, 20);
        w->anchor[i].off.x = (float)sqlite3_column_double(st, 21);
        w->anchor[i].off.y = (float)sqlite3_column_double(st, 22);
        w->anchor[i].off.z = (float)sqlite3_column_double(st, 23);
        w->phys[i].dyn = sqlite3_column_int(st, 24);
        w->phys[i].mass = (float)sqlite3_column_double(st, 25);
        if (w->phys[i].mass <= 0) w->phys[i].mass = 1.0f;
        w->phys[i].vel.x = (float)sqlite3_column_double(st, 26);
        w->phys[i].vel.y = (float)sqlite3_column_double(st, 27);
        w->phys[i].vel.z = (float)sqlite3_column_double(st, 28);
        wantP[nn] = sqlite3_column_int(st, 2);
        fresh[nn] = e;
        nn++;
    }
    sqlite3_finalize(st);
    for (int k = 0; k < nn; k++) {
        if (wantP[k] <= 0) continue;
        int pi = -1;
        for (int j = 0; j < nn; j++)
            if (rowIds[j] == wantP[k]) {
                pi = (int)fresh[j] - 1;
                break;
            }
        int ci = (int)fresh[k] - 1;
        if (pi >= 0 && pi != ci && w->alive[pi]) w->parent[ci] = pi;
    }
    return 1;
}

// Журнал: новая мутация убивает redo (rev=1 чистим, как ui_undo).
static int journal_drop_redo(EngDb *db) {
    return db_exec(db, "DELETE FROM journal WHERE rev=1;");
}

static int journal_add(EngDb *db, const char *op, int ent, const char *field,
                       const char *before, const char *after) {
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db->db,
                           "INSERT INTO journal(op,ent,field,before,after,rev) "
                           "VALUES(?1,?2,?3,?4,?5,0);",
                           -1, &st, 0) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, op, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, ent);
    sqlite3_bind_text(st, 3, field ? field : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, before ? before : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, after ? after : "", -1, SQLITE_TRANSIENT);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

int eng_db_set(EngDb *db, ScWorld *w, unsigned ent, const char *field, const char *value) {
    if (!db || !w || !field || !value) return 0;
    int i = (int)ent - 1;
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    const DbField *f = db_find(field);
    if (!f) return 0;
    char before[64];
    field_get_text(w, i, f, before, sizeof before);
    // Сухая валидация на копии значимых полей (мир правим только валидным).
    if (f->comp == 4 && f->off == 0) {
        if (!value[0] || strlen(value) >= SC_NAME) return 0;
    } else if (f->isInt || f->comp == 4) {
        char *end = 0;
        long iv = strtol(value, &end, 10);
        (void)iv;
        if (end == value || *end) return 0;
        if (!strcmp(f->name, "mode") && (iv < 0 || iv > 2)) return 0;
        if (!strcmp(f->name, "follow") && iv != 0) {
            int pi = (int)iv - 1;
            if (pi < 0 || pi >= SC_MAX || !w->alive[pi]) return 0;
        }
        if (!strcmp(f->name, "parent") && iv != 0) {
            int pi = (int)iv - 1;
            if (pi < 0 || pi >= SC_MAX || !w->alive[pi] || pi == i) return 0;
        }
    } else {
        char *end = 0;
        double dv = strtod(value, &end);
        if (end == value || *end || dv != dv || dv > 1e30 || dv < -1e30) return 0;
        if (!strcmp(f->name, "mass") && dv <= 0) return 0;
    }
    if (!journal_drop_redo(db)) return 0;
    if (!field_set_text(w, i, f, value)) return 0;
    char after[64];
    field_get_text(w, i, f, after, sizeof after);
    if (!strcmp(before, after)) return 1; // значение то же — журнал не пачкаем
    if (!journal_add(db, "field", (int)ent, field, before, after)) return 0;
    // Зеркалим в entities (мир — правда, база — копия).
    char sql[256];
    snprintf(sql, sizeof sql, "UPDATE entities SET %s=?1 WHERE id=%d;", db_col(f),
             (int)ent);
    sqlite3_stmt *st = 0;
    int ok = 0;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, 0) == SQLITE_OK) {
        if (f->comp == 4 && f->off == 0) sqlite3_bind_text(st, 1, after, -1, SQLITE_TRANSIENT);
        else if (f->isInt || (f->comp == 4))
            sqlite3_bind_int(st, 1, (int)strtol(after, 0, 10));
        else sqlite3_bind_double(st, 1, strtod(after, 0));
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    return ok;
}

int eng_db_kill(EngDb *db, ScWorld *w, unsigned ent) {
    if (!db || !w) return 0;
    int i = (int)ent - 1;
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    char *snap = eng_scene_entity_json(w, (Entity)ent);
    if (!snap) return 0;
    if (!journal_drop_redo(db)) {
        free(snap);
        return 0;
    }
    if (!journal_add(db, "kill", (int)ent, "", snap, "DELETED")) {
        free(snap);
        return 0;
    }
    free(snap);
    char sql[128];
    snprintf(sql, sizeof sql, "UPDATE entities SET deleted=1 WHERE id=%d;", (int)ent);
    int ok = db_exec(db, sql);
    sc_kill(w, (Entity)ent);
    return ok;
}

// Применить значение поля (undo/redo 경로, без журнала).
static int db_apply(ScWorld *w, int internal, const char *field, const char *value) {
    const DbField *f = db_find(field);
    if (!f) return 0;
    return field_set_text(w, internal, f, value);
}

static int journal_top(EngDb *db, int rev, int *seq, char *op, int oplen, int *ent,
                       char *field, int fieldlen, char *val, int vallen, int wantBefore) {
    const char *col = wantBefore ? "before" : "after";
    char sql[256];
    snprintf(sql, sizeof sql,
             "SELECT seq,op,ent,field,%s FROM journal WHERE rev=%d ORDER BY seq DESC "
             "LIMIT 1;",
             col, rev);
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, 0) != SQLITE_OK) return 0;
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        *seq = sqlite3_column_int(st, 0);
        snprintf(op, (size_t)oplen, "%s", sqlite3_column_text(st, 1));
        *ent = sqlite3_column_int(st, 2);
        snprintf(field, (size_t)fieldlen, "%s", sqlite3_column_text(st, 3));
        snprintf(val, (size_t)vallen, "%s", sqlite3_column_text(st, 4));
        got = 1;
    }
    sqlite3_finalize(st);
    return got;
}

static int journal_set_rev(EngDb *db, int seq, int rev) {
    char sql[128];
    snprintf(sql, sizeof sql, "UPDATE journal SET rev=%d WHERE seq=%d;", rev, seq);
    return db_exec(db, sql);
}

static int db_mirror_field(EngDb *db, int ent, const char *field, const char *value) {
    const DbField *f = db_find(field);
    if (!f) return 0;
    char sql[256];
    snprintf(sql, sizeof sql, "UPDATE entities SET %s=?1 WHERE id=%d;", db_col(f), ent);
    sqlite3_stmt *st = 0;
    int ok = 0;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, 0) == SQLITE_OK) {
        if (f->comp == 4 && f->off == 0) sqlite3_bind_text(st, 1, value, -1, SQLITE_TRANSIENT);
        else if (f->isInt || f->comp == 4)
            sqlite3_bind_int(st, 1, (int)strtol(value, 0, 10));
        else sqlite3_bind_double(st, 1, strtod(value, 0));
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    return ok;
}

int eng_db_undo(EngDb *db, ScWorld *w) {
    if (!db || !w) return 0;
    int seq = 0, ent = 0;
    char op[16] = {0}, field[DBF_NAMELEN] = {0}, val[4096] = {0};
    if (!journal_top(db, 0, &seq, op, sizeof op, &ent, field, sizeof field, val, sizeof val,
                     1))
        return 0; // пусто
    if (!strcmp(op, "kill")) {
        // Воскрешение из снимка (id новый — честно, данные те же).
        Entity e = sc_spawn(w, "tmp");
        if (!e) return 0;
        if (!eng_scene_entity_parse(w, e, val)) {
            sc_kill(w, e);
            return 0;
        }
        int i = (int)e - 1;
        // Родителя из снимка: ключ parent может отсутствовать — ищем.
        const char *pp = strstr(val, "\"parent\":");
        if (pp) {
            int pid = atoi(pp + 9);
            if (pid > 0) {
                int pi = pid - 1;
                if (pi >= 0 && pi < SC_MAX && w->alive[pi] && pi != i) w->parent[i] = pi;
            }
        }
        // Воскрешение = новая строка (id новый); старую чистим.
        {
            char sql[128];
            snprintf(sql, sizeof sql, "DELETE FROM entities WHERE id=%d;", ent);
            db_exec(db, sql);
        }
        // Вставляем новую строку полным sync одной сущности: проще sync всего.
        // (Мир маленький; транзакция одна.)
        journal_set_rev(db, seq, 1);
        return eng_db_sync(db, w);
    }
    int i = ent - 1;
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    if (!db_apply(w, i, field, val)) return 0;
    db_mirror_field(db, ent, field, val);
    journal_set_rev(db, seq, 1);
    return 1;
}

int eng_db_redo(EngDb *db, ScWorld *w) {    if (!db || !w) return 0;
    int seq = 0, ent = 0;
    char op[16] = {0}, field[DBF_NAMELEN] = {0}, val[4096] = {0};
    if (!journal_top(db, 1, &seq, op, sizeof op, &ent, field, sizeof field, val, sizeof val,
                     0))
        return 0; // нечего повторять
    if (!strcmp(op, "kill")) {
        // Повтор убийства: ищем по имени из снимка (ровно одно совпадение).
        const char *np = strstr(val, "\"name\":\"");
        char nm[SC_NAME] = {0};
        if (np) {
            np += 8;
            int ni = 0;
            while (*np && *np != '"' && ni < SC_NAME - 1) nm[ni++] = *np++;
        }
        if (!nm[0]) return 0;
        Entity list[1024];
        int n = sc_list(w, list, 1024);
        Entity victim = 0;
        int hits = 0;
        for (int k = 0; k < n; k++)
            if (!strcmp(w->name[(int)list[k] - 1], nm)) {
                victim = list[k];
                hits++;
            }
        if (hits != 1) return 0;
        int i = (int)victim - 1;
        (void)i;
        char sql[128];
        snprintf(sql, sizeof sql, "UPDATE entities SET deleted=1 WHERE id=%d;",
                 (int)victim);
        db_exec(db, sql);
        sc_kill(w, victim);
        journal_set_rev(db, seq, 0);
        return 1;
    }
    int i = ent - 1;
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    if (!db_apply(w, i, field, val)) return 0;
    db_mirror_field(db, ent, field, val);
    journal_set_rev(db, seq, 0);
    return 1;
}

// --- Этап В: граф в project.db (замена таблиц целиком, одна транзакция) ---
int eng_graph_save(EngDb *db, const EngGraph *gr) {
    if (!db || !gr) return 0;
    if (!db_exec(db, "BEGIN;")) return 0;
    if (!db_exec(db, "DELETE FROM nodes;") || !db_exec(db, "DELETE FROM links;")) {
        db_exec(db, "ROLLBACK;");
        return 0;
    }
    int nn = eng_graph_nnodes(gr);
    for (int i = 0; i < nn; i++) {
        int id = 0, type = 0;
        float p0 = 0, p1 = 0;
        if (!eng_graph_node_at(gr, i, &id, &type, &p0, &p1)) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
        sqlite3_stmt *st = 0;
        if (sqlite3_prepare_v2(db->db, "INSERT INTO nodes(id,type,p0,p1,x,y) "
                                       "VALUES(?1,?2,?3,?4,0,0);",
                               -1, &st, 0) != SQLITE_OK) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
        sqlite3_bind_int(st, 1, id);
        sqlite3_bind_int(st, 2, type);
        sqlite3_bind_double(st, 3, p0);
        sqlite3_bind_double(st, 4, p1);
        int ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
    }
    int nl = eng_graph_nlinks(gr);
    for (int i = 0; i < nl; i++) {
        int fn = 0, fp = 0, tn = 0, tp = 0;
        if (!eng_graph_link_at(gr, i, &fn, &fp, &tn, &tp)) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
        sqlite3_stmt *st = 0;
        if (sqlite3_prepare_v2(db->db, "INSERT INTO links(fn,fp,tn,tp) "
                                       "VALUES(?1,?2,?3,?4);",
                               -1, &st, 0) != SQLITE_OK) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
        sqlite3_bind_int(st, 1, fn);
        sqlite3_bind_int(st, 2, fp);
        sqlite3_bind_int(st, 3, tn);
        sqlite3_bind_int(st, 4, tp);
        int ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) {
            db_exec(db, "ROLLBACK;");
            return 0;
        }
    }
    return db_exec(db, "COMMIT;");
}

int eng_graph_load(EngDb *db, EngGraph *gr) {
    if (!db || !gr) return 0;
    // Чистим граф: load = замена (как сцена).
    EngGraph *fresh = eng_graph_create();
    if (!fresh) return 0;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db->db, "SELECT id,type,p0,p1 FROM nodes ORDER BY id;", -1,
                           &st, 0) != SQLITE_OK) {
        eng_graph_destroy(fresh);
        return 0;
    }
    // id файла -> id графа (порядок вставки сохраняет Justin? нет: id задаём
    // линкуемым — вставляем с теми же id через add+remap).
    int fids[256];
    int gids[256];
    int nn = 0;
    int ok = 1;
    while (ok && nn < 256 && sqlite3_step(st) == SQLITE_ROW) {
        int fid = sqlite3_column_int(st, 0);
        int gid = eng_graph_add_node(fresh, sqlite3_column_int(st, 1),
                                     (float)sqlite3_column_double(st, 2),
                                     (float)sqlite3_column_double(st, 3));
        if (!gid) ok = 0;
        else {
            fids[nn] = fid;
            gids[nn] = gid;
            nn++;
        }
    }
    sqlite3_finalize(st);
    if (ok) {
        if (sqlite3_prepare_v2(db->db, "SELECT fn,fp,tn,tp FROM links ORDER BY id;", -1,
                               &st, 0) != SQLITE_OK)
            ok = 0;
        else {
            while (ok && sqlite3_step(st) == SQLITE_ROW) {
                int fn = sqlite3_column_int(st, 0), tn = sqlite3_column_int(st, 2);
                int gfn = 0, gtn = 0;
                for (int i = 0; i < nn; i++) {
                    if (fids[i] == fn) gfn = gids[i];
                    if (fids[i] == tn) gtn = gids[i];
                }
                if (!gfn || !gtn ||
                    !eng_graph_link(fresh, gfn, sqlite3_column_int(st, 1), gtn,
                                    sqlite3_column_int(st, 3)))
                    ok = 0;
            }
            sqlite3_finalize(st);
        }
    }
    if (!ok) {
        eng_graph_destroy(fresh);
        return 0;
    }
    // Коммит в целевой: замена целиком через клон (id сохраняются).
    if (!eng_graph_clone(gr, fresh)) {
        eng_graph_destroy(fresh);
        return 0;
    }
    eng_graph_destroy(fresh);
    return 1;
}
