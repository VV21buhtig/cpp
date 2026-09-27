#include "engine/texture.h"
#include <glad/gl.h>
#include <stb/stb_image.h>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstring>

unsigned int loadTexture(const char* path) {
    unsigned int tex; glGenTextures(1, &tex);
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 0);
    if (data) {
        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        std::cout << "Loaded: " << path << "\n";
    } else std::cerr << "Failed: " << path << "\n";
    stbi_image_free(data);
    return tex;
}

unsigned int loadTileArray(const char* dir) {
    // tiles/grass_top,grass_side,dirt,stone.png по 16x16. Нет файла = маджента.
    // наш layout tiles/*.png либо ванильный MC: assets/minecraft/textures/block/*.png
    const char* ours[13] = {"grass_top.png", "grass_side.png", "dirt.png", "stone.png", "water.png", "lava.png", "leaves.png", "log_side.png", "log_top.png", "ore_coal.png", "ore_iron.png", "ore_gold.png", "ore_diamond.png"};
    const char* mc[13] = {"grass_block_top.png", "grass_block_side.png", "dirt.png", "stone.png", "", "", "oak_leaves.png", "oak_log.png", "oak_log_top.png", "coal_ore.png", "iron_ore.png", "gold_ore.png", "diamond_ore.png"};
    const int T = 16, NL = 13;
    std::vector<unsigned char> all(T * T * 4 * NL);
    stbi_set_flip_vertically_on_load(true);
    for (int i = 0; i < NL; i++) {
        char path[1024], mcpath[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ours[i]);
        snprintf(mcpath, sizeof(mcpath), "%s/assets/minecraft/textures/block/%s", dir, mc[i]);
        int w, h, ch;
        unsigned char* d = nullptr;
        FILE* probe = fopen(path, "rb");
        if (probe) { fclose(probe); }
        else { snprintf(path, sizeof(path), "%s", mcpath); }
        d = stbi_load(path, &w, &h, &ch, 4);
        if (d && w == T && h == T) {
            memcpy(&all[i * T * T * 4], d, T * T * 4);
            std::cout << "Tile " << i << ": " << path << "\n";
        } else {
            if (d) { std::cerr << "Tile BAD SIZE " << path << " (" << w << "x" << h << ")\n"; stbi_image_free(d); }
            else std::cerr << "Tile MISSING " << path << " -> magenta\n";
            unsigned char fr = 255, fg = 0, fb = 255; // нет файла = маджента
            if (i == 4) { fr = 40; fg = 100; fb = 220; } // вода fallback синяя
            for (int p = 0; p < T * T; p++) {
                bool odd = ((p / T) + p) % 2;
                all[(i * T * T + p) * 4 + 0] = fr;
                all[(i * T * T + p) * 4 + 1] = fg;
                all[(i * T * T + p) * 4 + 2] = (unsigned char)(odd && fr == 255 ? 255 : fb);
                all[(i * T * T + p) * 4 + 3] = 255;
            }
        }
        if (d) stbi_image_free(d);
    }
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    // tile0 grass_top в паках под OptiFine идёт ч/б (красит колормапа) — печём plains-tint #91BD59
    for (int p = 0; p < T * T; p++) {
        all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 145 / 255);
        all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 189 / 255);
        all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 89 / 255);
    }
    // tile 6 (листва) тоже ч/б под колормапу — foliage plains #77AB2F
    for (int p = 6 * T * T; p < 7 * T * T; p++) {
        all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 119 / 255);
        all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 171 / 255);
        all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 47 / 255);
    }
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, T, T, NL, 0, GL_RGBA, GL_UNSIGNED_BYTE, all.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

void setTileArrayFilter(unsigned int tex, int mode) {
    // Математика: Nearest=1 тексел; Bilinear=2x2 lerp в одном мипе;
    // Trilinear=2 мипа x bilinear + lerp по lod; Aniso=N выборок вдоль следа
    // (лечит мыло на скользящих углах — главный кейс воксельного террейна).
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    if (mode >= 3) {
        float mx = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &mx);
        if (mx >= 2.0f) {
            float a = mx < 8.0f ? mx : 8.0f;
            glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, a);
        } else mode = 2; // нет EXT — откат на трилиней
    }
    if (mode <= 0) {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else if (mode == 1) {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}
