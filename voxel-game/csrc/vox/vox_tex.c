#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "vox_tex.h"
#include <stdio.h>
#include <string.h>

int vox_tex_load(const char *dir, uint8_t *out) {
    // Как у них: строки PNG снизу вверх (v=0 = низ картинки).
    // Без этого кайма grass_side оказывается внизу грани.
    stbi_set_flip_vertically_on_load(1);
    static const char *names[VOX_LAYERS] = {
        "grass_top", "grass_side", "dirt", "stone",
        "water", "lava", "leaves",
        "log_side", "log_top",
    };
    for (int l = 0; l < VOX_LAYERS; l++) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s.png", dir, names[l]);
        int w = 0, h = 0, ch = 0;
        uint8_t *px = stbi_load(path, &w, &h, &ch, 4);
        if (!px || w != VOX_TILE || h != VOX_TILE) {
            fprintf(stderr, "tile fail: %s (%dx%d)\n", path, w, h);
            if (px) stbi_image_free(px);
            return 0;
        }
        memcpy(out + (size_t)l * VOX_TILE * VOX_TILE * 4, px, VOX_TILE * VOX_TILE * 4);
        stbi_image_free(px);
    }
    return 1;
}
