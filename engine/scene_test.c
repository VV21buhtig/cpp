// Тест сцены: иерархия, трансформы, видимость, убийство, переполнение.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <sys/stat.h>
#include "scene.h"
#include "project.h"

int main(void) {
    int bad = 0;
    ScWorld w;
    sc_init(&w);
    Entity root = sc_spawn(&w, "Scene");
    Entity cam = sc_spawn(&w, "Camera");
    Entity light = sc_spawn(&w, "Light");
    Entity mesh = sc_spawn(&w, "Mesh");
    if (!root || !cam || !light || !mesh) { printf("SCENE FAIL: spawn\n"); return 1; }
    if (!sc_parent(&w, cam, root) || !sc_parent(&w, light, root) ||
        !sc_parent(&w, mesh, root)) {
        printf("SCENE FAIL: parent\n");
        bad = 1;
    }
    // Цикл запрещён. Геттер родителя для дерева.
    if (sc_parent(&w, root, cam)) { printf("SCENE FAIL: cycle\n"); bad = 1; }
    if (sc_parent_of(&w, cam) != root || sc_parent_of(&w, root) != 0 ||
        sc_parent_of(&w, 99999) != 0) {
        printf("SCENE FAIL: parent_of\n");
        bad = 1;
    }
    if (sc_parent(&w, cam, cam)) { printf("SCENE FAIL: self\n"); bad = 1; }
    // Трансформ туда-обратно (ползунки инспектора).
    ScTransform t = sc_get(&w, cam);
    t.pos = v3(1.5f, 42.0f, -3.25f);
    t.rot = v3(0.6f, -0.35f, 0.0f);
    sc_set(&w, cam, &t);
    ScTransform back = sc_get(&w, cam);
    if (fabsf(back.pos.x - 1.5f) > 1e-6f || fabsf(back.pos.y - 42.0f) > 1e-6f ||
        fabsf(back.rot.x - 0.6f) > 1e-6f) {
        printf("SCENE FAIL: transform\n");
        bad = 1;
    }
    // Видимость (глаз в иерархии).
    sc_show(&w, mesh, 0);
    if (sc_visible(&w, mesh)) { printf("SCENE FAIL: visible\n"); bad = 1; }
    // Список и убийство (дети сиротеют).
    Entity list[16];
    int n = sc_list(&w, list, 16);
    if (n != 4) { printf("SCENE FAIL: list %d\n", n); bad = 1; }
    sc_kill(&w, root);
    if (sc_alive(&w, root)) { printf("SCENE FAIL: kill\n"); bad = 1; }
    if (!sc_alive(&w, mesh)) { printf("SCENE FAIL: child died\n"); bad = 1; }
    if (strcmp(sc_name(&w, root), "") != 0) { printf("SCENE FAIL: dead name\n"); bad = 1; }
    // Переполнение пула (слоты мёртвых переиспользуются — так задумано).
    int extra = 0;
    for (int i = 0; i < SC_MAX + 10; i++)
        if (sc_spawn(&w, "x")) extra++;
    if (extra != SC_MAX - 3) { printf("SCENE FAIL: pool %d\n", extra); bad = 1; }
    // Мёртвые: тишина, не падение.
    sc_set(&w, root, &t);
    sc_show(&w, 99999, 1);
    // Переименование: копия, пустое и мёртвые — игнор.
    sc_rename(&w, cam, "MainCamera");
    if (strcmp(sc_name(&w, cam), "MainCamera") != 0) { printf("SCENE FAIL: rename\n"); bad = 1; }
    sc_rename(&w, cam, "");
    if (strcmp(sc_name(&w, cam), "MainCamera") != 0) { printf("SCENE FAIL: rename-empty\n"); bad = 1; }
    sc_rename(&w, 99999, "Ghost");
    if (strcmp(sc_name(&w, 99999), "") != 0) { printf("SCENE FAIL: rename-dead\n"); bad = 1; }
    // Этап Б: дефолты компонентов + туда-обратно.
    {
        ScMaterial m0 = sc_mat_get(&w, light);
        if (m0.col.x != 1.0f || m0.a != 1.0f || m0.rough != 0.8f || m0.metal != 0.0f) {
            printf("SCENE FAIL: mat-default\n");
            bad = 1;
        }
        ScAnchor a0 = sc_anchor_get(&w, light);
        if (a0.mode != 0 || a0.follow != 0) {
            printf("SCENE FAIL: anchor-default\n");
            bad = 1;
        }
        ScPhysics p0 = sc_phys_get(&w, light);
        if (p0.mass != 1.0f || p0.dyn != 0 || p0.vel.x != 0.0f) {
            printf("SCENE FAIL: phys-default\n");
            bad = 1;
        }
        ScMaterial m = m0;
        m.col = v3(0.2f, 0.4f, 0.6f);
        m.rough = 0.3f;
        sc_mat_set(&w, light, &m);
        ScMaterial mb = sc_mat_get(&w, light);
        if (mb.col.y != 0.4f || mb.rough != 0.3f) {
            printf("SCENE FAIL: mat-rw\n");
            bad = 1;
        }
        ScAnchor a = {2, (int)mesh, {1.0f, 2.0f, 3.0f}};
        sc_anchor_set(&w, light, &a);
        ScAnchor ab = sc_anchor_get(&w, light);
        if (ab.mode != 2 || ab.follow != (int)mesh || ab.off.z != 3.0f) {
            printf("SCENE FAIL: anchor-rw\n");
            bad = 1;
        }
        ScPhysics ph = {{{0}}};
        ph.vel = v3(1.0f, 2.0f, 3.0f);
        ph.mass = 2.5f;
        ph.dyn = 1;
        sc_phys_set(&w, light, &ph);
        ScPhysics phb = sc_phys_get(&w, light);
        if (phb.vel.y != 2.0f || phb.mass != 2.5f || phb.dyn != 1) {
            printf("SCENE FAIL: phys-rw\n");
            bad = 1;
        }
        // Мёртвым: get нули, set игнор.
        sc_kill(&w, mesh);
        ScMaterial md = sc_mat_get(&w, mesh);
        if (md.col.x != 0.0f || md.a != 0.0f) {
            printf("SCENE FAIL: mat-dead\n");
            bad = 1;
        }
        sc_mat_set(&w, mesh, &m);
        sc_anchor_set(&w, mesh, &a);
        sc_phys_set(&w, mesh, &ph);
        if (sc_mat_get(&w, mesh).col.x != 0.0f) {
            printf("SCENE FAIL: comp-dead-set\n");
            bad = 1;
        }
    }
    // X-списки покрывают структуры (§5.2): паттерн по оффсетам туда-обратно.
    {
        Entity e = sc_spawn(&w, "Pat");
#define X(t, name) offsetof(ScMaterial, name),
        static const size_t mo[] = {SC_MATERIAL_FIELDS(X)};
#undef X
#define X(t, name) offsetof(ScAnchor, name),
        static const size_t ao[] = {SC_ANCHOR_FIELDS(X)};
#undef X
#define X(t, name) offsetof(ScPhysics, name),
        static const size_t po[] = {SC_PHYSICS_FIELDS(X)};
#undef X
        if (sizeof mo / sizeof mo[0] != 6 || sizeof ao / sizeof ao[0] != 5 ||
            sizeof po / sizeof po[0] != 5) {
            printf("SCENE FAIL: xlist-count\n");
            bad = 1;
        } else {
            ScMaterial m;
            memset(&m, 0, sizeof m);
            for (size_t i = 0; i < 6; i++) *(float *)((char *)&m + mo[i]) = 10.0f + (float)i;
            sc_mat_set(&w, e, &m);
            ScMaterial mb = sc_mat_get(&w, e);
            for (size_t i = 0; i < 6; i++)
                if (*(const float *)((const char *)&mb + mo[i]) != 10.0f + (float)i) {
                    printf("SCENE FAIL: xlist-mat %zu\n", i);
                    bad = 1;
                }
            // int-поля якоря/физики через свои оффсеты (первые два/последний).
            ScAnchor a;
            memset(&a, 0, sizeof a);
            *(int *)((char *)&a + ao[0]) = 2;
            *(int *)((char *)&a + ao[1]) = (int)e;
            sc_anchor_set(&w, e, &a);
            ScAnchor ab = sc_anchor_get(&w, e);
            if (ab.mode != 2 || ab.follow != (int)e) {
                printf("SCENE FAIL: xlist-anchor\n");
                bad = 1;
            }
            ScPhysics ph;
            memset(&ph, 0, sizeof ph);
            *(int *)((char *)&ph + po[4]) = 1;
            sc_phys_set(&w, e, &ph);
            if (sc_phys_get(&w, e).dyn != 1) {
                printf("SCENE FAIL: xlist-phys\n");
                bad = 1;
            }
        }
    }
    {
        float tri[3 * 9] = {0};
        if (!sc_mesh_set(&w, cam, tri, 3)) { printf("SCENE FAIL: mesh-set\n"); bad = 1; }
        int mn = 0;
        const float *mg = sc_mesh_get(&w, cam, &mn);
        if (!mg || mn != 3 || mg != sc_mesh_get(&w, cam, 0)) {
            printf("SCENE FAIL: mesh-get\n");
            bad = 1;
        }
        if (!sc_mesh_set(&w, cam, 0, 0) || sc_mesh_get(&w, cam, &mn)) {
            printf("SCENE FAIL: mesh-unset\n");
            bad = 1;
        }
        if (sc_mesh_set(&w, 99999, tri, 3) || sc_mesh_get(&w, 99999, &mn)) {
            printf("SCENE FAIL: mesh-dead\n");
            bad = 1;
        }
        sc_mesh_set(&w, cam, tri, 3);
        sc_kill(&w, cam);
        if (sc_mesh_get(&w, cam, &mn)) { printf("SCENE FAIL: mesh-kill\n"); bad = 1; }
    }
    // Проект в бекенде: create + load + roundtrip + JSON (будущий Electron ест это).
    {
        mkdir("/tmp/opencode", 0755);
        mkdir("/tmp/opencode/tproj", 0755);
        remove("/tmp/opencode/tproj/project.txt"); // повторный прогон — не дубликат
        char err[128] = {0};
        if (!eng_project_create("/tmp/opencode", "tproj", "Voxel Terrain", 2, err, sizeof err)) {
            printf("SCENE FAIL: project %s\n", err);
            bad = 1;
        } else {
            ScWorld a;
            sc_init(&a);
            if (!eng_scene_load(&a, "/tmp/opencode/tproj")) {
                printf("SCENE FAIL: load\n");
                bad = 1;
            } else {
                Entity l[16];
                int n = sc_list(&a, l, 16);
                if (n != 4) { printf("SCENE FAIL: count %d\n", n); bad = 1; }
                char *js = eng_scene_json(&a);
                if (!js || !strstr(js, "Camera") || !strstr(js, "Light")) {
                    printf("SCENE FAIL: json\n");
                    bad = 1;
                }
                // Этап Б: новые ключи в JSON + entity parse туда-обратно.
                if (js && (!strstr(js, "\"mat\"") || !strstr(js, "\"anchor\"") ||
                           !strstr(js, "\"vel\""))) {
                    printf("SCENE FAIL: json-comp\n");
                    bad = 1;
                }
                {
                    Entity le[16];
                    int ln = sc_list(&a, le, 16);
                    Entity hero = 0;
                    for (int i = 0; i < ln; i++)
                        if (!strcmp(sc_name(&a, le[i]), "Light")) hero = le[i];
                    char *ej = eng_scene_entity_json(&a, hero);
                    if (!ej || !strstr(ej, "\"name\":\"Light\"")) {
                        printf("SCENE FAIL: entity-json\n");
                        bad = 1;
                    }
                    if (eng_scene_entity_json(&a, 99999)) {
                        printf("SCENE FAIL: entity-json-dead\n");
                        bad = 1;
                    }
                    if (ej && !eng_scene_entity_parse(&a, hero, "{\"pos\":[1,2,3],"
                                                                     "\"rough\":0.1,"
                                                                     "\"dyn\":1,"
                                                                     "\"mass\":2.0,"
                                                                     "\"name\":\"Lamp\"}")) {
                        printf("SCENE FAIL: entity-parse\n");
                        bad = 1;
                    }
                    if (ej) {
                        ScTransform tt = sc_get(&a, hero);
                        if (tt.pos.x != 1.0f || tt.pos.z != 3.0f) {
                            printf("SCENE FAIL: entity-applied\n");
                            bad = 1;
                        }
                        if (sc_mat_get(&a, hero).rough != 0.1f ||
                            sc_phys_get(&a, hero).dyn != 1) {
                            printf("SCENE FAIL: entity-comp-applied\n");
                            bad = 1;
                        }
                        if (strcmp(sc_name(&a, hero), "Lamp") != 0) {
                            printf("SCENE FAIL: entity-rename\n");
                            bad = 1;
                        }
                        // Битый JSON атомарен: ничего не меняет, 0.
                        ScTransform before = sc_get(&a, hero);
                        if (eng_scene_entity_parse(&a, hero, "{\"pos\":[1,2,3],"
                                                                "\"mass\":-5}") ||
                            eng_scene_entity_parse(&a, hero, "{\"pos\":[1]}") ||
                            eng_scene_entity_parse(&a, hero, "{\"follow\":99999}") ||
                            eng_scene_entity_parse(&a, 99999, "{\"pos\":[1,2,3]}") ||
                            eng_scene_entity_parse(&a, hero, 0)) {
                            printf("SCENE FAIL: entity-parse-bad-ok\n");
                            bad = 1;
                        }
                        ScTransform after = sc_get(&a, hero);
                        if (after.pos.x != before.pos.x ||
                            sc_phys_get(&a, hero).mass != 2.0f) {
                            printf("SCENE FAIL: entity-atomic\n");
                            bad = 1;
                        }
                        // v2 roundtrip компонентов через файл.
                        eng_scene_save(&a, "/tmp/opencode/tproj");
                        ScWorld c2;
                        sc_init(&c2);
                        eng_scene_load(&c2, "/tmp/opencode/tproj");
                        Entity ll[16];
                        int lnc = sc_list(&c2, ll, 16);
                        Entity lamp = 0;
                        for (int i = 0; i < lnc; i++)
                            if (!strcmp(sc_name(&c2, ll[i]), "Lamp")) lamp = ll[i];
                        if (!lamp || sc_mat_get(&c2, lamp).rough != 0.1f ||
                            sc_phys_get(&c2, lamp).mass != 2.0f) {
                            printf("SCENE FAIL: v2-file-comp\n");
                            bad = 1;
                        }
                        free(ej);
                    }
                }
                free(js);
                // Roundtrip побайтово: save->load->save совпадает.
                mkdir("/tmp/opencode/tproj2", 0755);
                ScWorld b;
                sc_init(&b);
                eng_scene_load(&b, "/tmp/opencode/tproj");
                eng_scene_save(&b, "/tmp/opencode/tproj2");
                FILE *f1 = fopen("/tmp/opencode/tproj/scene.txt", "rb");
                FILE *f2 = fopen("/tmp/opencode/tproj2/scene.txt", "rb");
                int same = (f1 && f2);
                if (same) {
                    int c1, c2;
                    do {
                        c1 = fgetc(f1);
                        c2 = fgetc(f2);
                        if (c1 != c2) { same = 0; break; }
                    } while (c1 != EOF);
                }
                if (f1) fclose(f1);
                if (f2) fclose(f2);
                if (!same) { printf("SCENE FAIL: roundtrip\n"); bad = 1; }
            }
        }
    }
    printf(bad ? "SCENE FAIL\n" : "SCENE OK\n");
    return bad;
}
