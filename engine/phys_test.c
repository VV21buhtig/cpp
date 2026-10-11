// Тест L4: падение тела сверяем с аналитикой, часы — с арифметикой.
#include <stdio.h>
#include <math.h>
#include "phys.h"

int main(void) {
    int bad = 0;
    // Падение 1с при g=-9.8 шагом 1/60 (полунеявный: y ~= -4.98, v = -9.8).
    {
        PhysDomainCfg cfg = {-9.8f, 0.0f};
        Vec3 p = v3(0, 0, 0), v = v3(0, 0, 0);
        for (int i = 0; i < 60; i++) phys_step(&p, &v, &cfg, 1.0f / 60.0f);
        printf("fall: y=%.3f v=%.3f (want ~-4.98/-9.80)\n", p.y, v.y);
        if (fabsf(p.y + 4.98f) > 0.1f || fabsf(v.y + 9.8f) > 0.01f) {
            printf("PHYS FAIL: fall\n");
            bad = 1;
        }
    }
    // Предел скорости.
    {
        PhysDomainCfg cfg = {-9.8f, 5.0f};
        Vec3 p = v3(0, 0, 0), v = v3(0, 0, 0);
        for (int i = 0; i < 600; i++) phys_step(&p, &v, &cfg, 1.0f / 60.0f);
        printf("terminal: v=%.3f (want -5.0)\n", v.y);
        if (fabsf(v.y + 5.0f) > 0.01f) { printf("PHYS FAIL: terminal\n"); bad = 1; }
    }
    // Часы: 3 тика по 1/60 из кадра 0.05, защита от спирали.
    {
        PhysClock c = {0};
        int n = phys_ticks(&c, 0.05, 1.0 / 60.0);
        printf("ticks=%d acc=%.4f (want 3, ~0.0)\n", n, c.acc);
        if (n != 3 || fabs(c.acc) > 0.001) { printf("PHYS FAIL: ticks\n"); bad = 1; }
        n = phys_ticks(&c, 10.0, 1.0 / 60.0); // spike режем
        if (n != 5) { printf("PHYS FAIL: spiral\n"); bad = 1; }
    }
    printf(bad ? "PHYS FAIL\n" : "PHYS OK\n");
    return bad;
}
