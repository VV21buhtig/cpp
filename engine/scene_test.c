// Тест сцены: иерархия, трансформы, видимость, убийство, переполнение.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "scene.h"

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
    printf(bad ? "SCENE FAIL\n" : "SCENE OK\n");
    return bad;
}
