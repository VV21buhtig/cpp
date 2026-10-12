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

int main(void) {
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
    eng_view_close(vw);
    printf(bad ? "VIEW FAIL\n" : "VIEW OK\n");
    return bad;
}
