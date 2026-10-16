// Headless-тест вьювера бекенда: окно+ядро+кадр в CPU без GUI.
// Гоняет то, что потом ест Electron (eng_view_frame + eng_view_scene).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view.h"

static int brightness(const uint8_t *px) {
    // Центр 100x100: средняя сумма RGB (как Qt-стенд hasPicture).
    long sum = 0;
    int n = 0;
    for (int y = 310; y < 410; y += 2)
        for (int x = 590; x < 690; x += 2) {
            const uint8_t *p = px + ((size_t)y * 1280 + x) * 4;
            sum += p[0] + p[1] + p[2];
            n++;
        }
    return n ? (int)(sum / n) : 0;
}

int main(int argc, char **argv) {
    // Матрица бэкендов (замена Qt-матрице после сноса Qt):
    // view_test <backend 1|2|3> [domain] -> 8 кадров + вердикт одной строкой.
    if (argc > 1) {
        int be = atoi(argv[1]);
        unsigned dom = argc > 2 ? (unsigned)atoi(argv[2]) : 1u;
        if (be < 1 || be > 3) {
            printf("MATRIX FAIL: backend %d\n", be);
            return 1;
        }
        static uint8_t mp[(size_t)1280 * 720 * 4];
        EngView *m = eng_view_open(dom, (unsigned)be);
        if (!m) {
            printf("MATRIX FAIL: open %u/%d (нет ячейки?)\n", dom, be);
            return 1;
        }
        int f0 = eng_view_frames(m);
        int ok = 1;
        for (int i = 0; i < 8; i++)
            if (!eng_view_frame(m, mp, 1280, 720)) ok = 0;
        int f1 = eng_view_frames(m);
        int br = brightness(mp);
        printf("matrix dom=%u be=%d: frames %d->%d %s %s\n", dom, be, f0, f1,
               f1 > f0 && ok ? "ADVANCE-OK" : "STUCK-FAIL",
               br > 36 ? "PICTURE-OK" : "BLACK-FAIL");
        eng_view_close(m);
        return (f1 > f0 && ok && br > 36) ? 0 : 1;
    }
    int bad = 0;
    static uint8_t px[(size_t)1280 * 720 * 4]; // статика, не malloc в цикле
    // Воксель GL: греем подачу (12 чанков/кадр, мир 25) + проверяем картинку.
    EngView *vw = eng_view_open(1u, 2u);
    if (!vw) {
        printf("VIEW FAIL: open voxel-gl\n");
        return 1;
    }
    for (int i = 0; i < 8; i++)
        if (!eng_view_frame(vw, px, 1280, 720)) {
            printf("VIEW FAIL: frame %d\n", i);
            bad = 1;
        }
    if (eng_view_frames(vw) != 8) {
        printf("VIEW FAIL: count %d\n", eng_view_frames(vw));
        bad = 1;
    }
    int br = brightness(px);
    if (br <= 36) {
        printf("VIEW FAIL: black %d\n", br);
        bad = 1;
    }
    const char *js = eng_view_scene(vw);
    if (!js || !strstr(js, "Hero") || !strstr(js, "Sword")) {
        printf("VIEW FAIL: json\n");
        bad = 1;
    }
    // Горячая замена шейдера: тот же исходник (ок), битый (отказ+лог,
    // старая программа жива — кадр всё ещё яркий), чужой id (отказ).
    {
        char src[1 << 16] = {0};
        FILE *f = fopen("engine/shaders/gl/lighting.fs", "rb");
        int ok = 0;
        char log[2048] = {0};
        if (f) {
            size_t n = fread(src, 1, sizeof src - 1, f);
            fclose(f);
            ok = n > 0;
        }
        if (!ok) {
            printf("VIEW FAIL: shader file\n");
            bad = 1;
        } else {
            if (!eng_view_shader(vw, "engine/shaders/gl/lighting.fs", src, log, sizeof log)) {
                printf("VIEW FAIL: reload-same %s\n", log);
                bad = 1;
            }
            if (eng_view_shader(vw, "nope.fs", src, log, sizeof log)) {
                printf("VIEW FAIL: reload-unknown-ok\n");
                bad = 1;
            } else if (!log[0]) {
                printf("VIEW FAIL: reload-unknown-nolog\n");
                bad = 1;
            }
            if (eng_view_shader(vw, "lighting.fs", "this is not glsl {{{", log, sizeof log)) {
                printf("VIEW FAIL: reload-broken-ok\n");
                bad = 1;
            }
            if (!log[0] || !strstr(log, "fs:")) {
                printf("VIEW FAIL: reload-broken-nolog [%s]\n", log);
                bad = 1;
            }
            // Старая жива: кадр после битой попытки всё ещё яркий.
            if (!eng_view_frame(vw, px, 1280, 720) || brightness(px) <= 36) {
                printf("VIEW FAIL: frame-after-broken\n");
                bad = 1;
            }
        }
    }
    eng_view_close(vw);
    // Пиксель GL: паттерн zeer быстро (1 кадр).
    vw = eng_view_open(8u, 2u);
    if (!vw) {
        printf("VIEW FAIL: open pixel-gl\n");
        return 1;
    }
    if (!eng_view_frame(vw, px, 1280, 720) || brightness(px) <= 36) {
        printf("VIEW FAIL: pixel\n");
        bad = 1;
    }
    eng_view_close(vw);
    // Воксель WebGPU: пайпы из файлов (Этап 3.1) + reload пересборка.
    vw = eng_view_open(1u, 1u);
    if (!vw) {
        printf("VIEW FAIL: open voxel-wg\n");
        bad = 1;
    } else {
        if (!eng_view_frame(vw, px, 1280, 720) || brightness(px) <= 36) {
            printf("VIEW FAIL: wg frame (files?)\n");
            bad = 1;
        } else {
            char log[2048] = {0};
            if (!eng_view_shader(vw, "sdf", 0, log, sizeof log)) {
                printf("VIEW FAIL: wg reload-files [%s]\n", log);
                bad = 1;
            }
            if (eng_view_shader(vw, "nope", "x", log, sizeof log)) {
                printf("VIEW FAIL: wg reload-unknown-ok\n");
                bad = 1;
            }
            if (!eng_view_frame(vw, px, 1280, 720) || brightness(px) <= 36) {
                printf("VIEW FAIL: wg frame-after-reload\n");
                bad = 1;
            }
        }
        eng_view_close(vw);
    }
    // Этап Б: шов сущности — get/set JSON на Hero.
    vw = eng_view_open(1u, 2u);
    if (!vw) {
        printf("VIEW FAIL: open entity\n");
        bad = 1;
    } else {
        const char *sj = eng_view_scene(vw);
        unsigned hero = 0;
        if (sj) {
            const char *h = strstr(sj, "\"name\":\"Hero\"");
            // id идёт раньше имени: {"id":N,"name":"Hero".
            if (h && h > sj + 20) {
                const char *ip = h - 1;
                while (ip > sj && *ip != '{') ip--;
                if (sscanf(ip, "{\"id\":%u", &hero) != 1) hero = 0;
            }
        }
        if (!hero) {
            printf("VIEW FAIL: hero id\n");
            bad = 1;
        } else {
            const char *ej = eng_entity_get_json(vw, hero);
            if (!ej || !strstr(ej, "\"name\":\"Hero\"") || !strstr(ej, "\"mat\"")) {
                printf("VIEW FAIL: entity-get\n");
                bad = 1;
            }
            if (eng_entity_get_json(vw, 99999)) {
                printf("VIEW FAIL: entity-get-dead\n");
                bad = 1;
            }
            if (!eng_entity_set_json(vw, hero, "{\"pos\":[7,8,9],\"metal\":0.5}")) {
                printf("VIEW FAIL: entity-set\n");
                bad = 1;
            } else {
                const char *ej2 = eng_entity_get_json(vw, hero);
                if (!ej2 || !strstr(ej2, "7.0000") || !strstr(ej2, "\"metal\":0.5000")) {
                    printf("VIEW FAIL: entity-applied [%s]\n", ej2 ? ej2 : "null");
                    bad = 1;
                }
            }
            if (eng_entity_set_json(vw, hero, "not json") ||
                eng_entity_set_json(vw, 99999, "{\"pos\":[1,2,3]}")) {
                printf("VIEW FAIL: entity-set-bad-ok\n");
                bad = 1;
            }
        }
        eng_view_close(vw);
    }
    // Меш GL + пикинг: центр — Hero, угол — мимо, повтор стабилен.
    vw = eng_view_open(16u, 2u);
    if (!vw) {
        printf("VIEW FAIL: open mesh-gl\n");
        return 1;
    }
    for (int i = 0; i < 3; i++)
        if (!eng_view_frame(vw, px, 1280, 720)) {
            printf("VIEW FAIL: mesh frame\n");
            bad = 1;
        }
    {
        // id Hero/Sword — из JSON дерева (не хардкодим).
        const char *js = eng_view_scene(vw);
        int heroId = 0, swordId = 0;
        if (js) {
            const char *hp = strstr(js, "\"name\":\"Hero\"");
            if (hp) {
                const char *ip = hp;
                while (ip > js && strncmp(ip, "\"id\":", 5)) ip--;
                if (!strncmp(ip, "\"id\":", 5)) heroId = atoi(ip + 5);
            }
            const char *sp = strstr(js, "\"name\":\"Sword\"");
            if (sp) {
                const char *ip = sp;
                while (ip > js && strncmp(ip, "\"id\":", 5)) ip--;
                if (!strncmp(ip, "\"id\":", 5)) swordId = atoi(ip + 5);
            }
        }
        // Окклюзия (Б-пик): Sword concentric 1.5x вокруг Hero — nearer,
        // побеждает хоть и рисуется позже (глубина, не порядок).
        unsigned hit = eng_view_pick(vw, 640, 360);
        if (!swordId || hit != (unsigned)swordId) {
            printf("VIEW FAIL: pick-occl got=%u want=%d\n", hit, swordId);
            bad = 1;
        }
        // Невидимый окклюдер пропускается — снова Hero.
        if (swordId && heroId) {
            char cmd[64];
            snprintf(cmd, sizeof cmd, "{\"visible\":0}");
            if (!eng_entity_set_json(vw, (unsigned)swordId, cmd)) {
                printf("VIEW FAIL: hide-sword\n");
                bad = 1;
            } else if (eng_view_pick(vw, 640, 360) != (unsigned)heroId) {
                printf("VIEW FAIL: pick-hidden\n");
                bad = 1;
            }
            snprintf(cmd, sizeof cmd, "{\"visible\":1}");
            if (!eng_entity_set_json(vw, (unsigned)swordId, cmd) ||
                eng_view_pick(vw, 640, 360) != (unsigned)swordId) {
                printf("VIEW FAIL: pick-unhide\n");
                bad = 1;
            }
        }
        if (eng_view_pick(vw, 1279, 719)) {
            printf("VIEW FAIL: pick-corner\n");
            bad = 1;
        }
        // Не-меш домен: честный 0 (пикинга нет).
        EngView *wv = eng_view_open(1u, 2u);
        if (wv) {
            eng_view_frame(wv, px, 1280, 720);
            if (eng_view_pick(wv, 640, 360)) {
                printf("VIEW FAIL: pick-voxel\n");
                bad = 1;
            }
            eng_view_close(wv);
        }
    }
    eng_view_close(vw);
    // Нет ячейки: честный NULL (вектор под Vulkan).
    vw = eng_view_open(4u, 3u);
    if (vw) {
        printf("VIEW FAIL: bad-cell-open\n");
        eng_view_close(vw);
        bad = 1;
    }
    // Чужой размер: честный 0.
    vw = eng_view_open(1u, 2u);
    if (!vw) {
        printf("VIEW FAIL: reopen\n");
        return 1;
    }
    if (eng_view_frame(vw, px, 640, 480)) {
        printf("VIEW FAIL: bad-size-ok\n");
        bad = 1;
    }
    // Динамика: 960x540 и 1920x1080 рисуют (центр яркий), назад — тоже.
    // 0x0 и 9999x9999 — clamp, не отказ.
    if (!eng_view_resize(vw, 960, 540)) {
        printf("VIEW FAIL: resize-960\n");
        bad = 1;
    } else {
        static uint8_t px2[(size_t)960 * 540 * 4];
        int ok = 0;
        for (int i = 0; i < 8 && !ok; i++)
            ok = eng_view_frame(vw, px2, 960, 540);
        if (!ok) {
            printf("VIEW FAIL: frame-960\n");
            bad = 1;
        } else {
            long sum = 0;
            int n = 0;
            for (int y = 220; y < 320; y += 2)
                for (int x = 430; x < 530; x += 2) {
                    const uint8_t *p = px2 + ((size_t)y * 960 + x) * 4;
                    sum += p[0] + p[1] + p[2];
                    n++;
                }
            if (n == 0 || sum / n <= 36) {
                printf("VIEW FAIL: black-960\n");
                bad = 1;
            }
        }
    }
    if (!eng_view_resize(vw, 0, 0) || !eng_view_resize(vw, 9999, 9999)) {
        printf("VIEW FAIL: clamp\n");
        bad = 1;
    }
    if (!eng_view_resize(vw, 1280, 720)) {
        printf("VIEW FAIL: resize-back\n");
        bad = 1;
    } else if (!eng_view_frame(vw, px, 1280, 720)) {
        printf("VIEW FAIL: frame-back\n");
        bad = 1;
    }
    eng_view_close(vw);
    printf(bad ? "VIEW FAIL\n" : "VIEW OK\n");
    return bad;
}
