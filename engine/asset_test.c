// Тест склада: дедуп, резолв, байты. Депо — /tmp (не мусорим в репо).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "asset.h"
#include "vfs.h"

#define DEPOT "/tmp/opencode/depot"

int main(void) {
    int bad = 0;
    system("rm -rf " DEPOT);
    system("mkdir -p " DEPOT);
    const char *a = "engine/assets/tiles/grass_top.png";
    const char *b = "engine/assets/tiles/dirt.png";
    AssetId i1 = asset_import(DEPOT, a);
    AssetId i2 = asset_import(DEPOT, a); // дубль
    AssetId i3 = asset_import(DEPOT, b);
    printf("ids: %llx %llx %llx (1==2, 1!=3)\n",
           (unsigned long long)i1, (unsigned long long)i2, (unsigned long long)i3);
    if (!i1 || i1 != i2 || i1 == i3) { printf("ASSET FAIL: dedup\n"); bad = 1; }
    int c = asset_count(DEPOT);
    printf("count=%d (want 2)\n", c);
    if (c != 2) { printf("ASSET FAIL: count\n"); bad = 1; }
    char p[512] = {0};
    if (!asset_path(DEPOT, i1, p, sizeof p)) { printf("ASSET FAIL: path\n"); bad = 1; }
    else {
        // Байты из депо = исходнику.
        size_t n0 = vfs_size(a);
        uint8_t *b0 = (uint8_t *)malloc(n0 + 1);
        uint8_t *b1 = (uint8_t *)malloc(n0 + 1);
        size_t r0 = b0 ? vfs_read(a, b0, n0) : 0;
        size_t r1 = b1 ? vfs_read(p, b1, n0) : 0;
        if (!b0 || !b1 || r0 != n0 || r1 != n0 || memcmp(b0, b1, n0)) {
            printf("ASSET FAIL: bytes\n");
            bad = 1;
        } else {
            printf("bytes OK (%zu)\n", n0);
        }
        free(b0);
        free(b1);
    }
    if (asset_path(DEPOT, 0x1234, p, sizeof p)) { printf("ASSET FAIL: ghost\n"); bad = 1; }
    printf(bad ? "ASSET FAIL\n" : "ASSET OK\n");
    return bad;
}
