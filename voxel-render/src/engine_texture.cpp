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
    const char* names[4] = {"grass_top.png", "grass_side.png", "dirt.png", "stone.png"};
    const int T = 16;
    std::vector<unsigned char> all(T * T * 4 * 4);
    stbi_set_flip_vertically_on_load(true);
    for (int i = 0; i < 4; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        int w, h, ch;
        unsigned char* d = stbi_load(path, &w, &h, &ch, 4);
        if (d && w == T && h == T) {
            memcpy(&all[i * T * T * 4], d, T * T * 4);
            std::cout << "Tile " << i << ": " << path << "\n";
        } else {
            if (d) { std::cerr << "Tile BAD SIZE " << path << " (" << w << "x" << h << ")\n"; stbi_image_free(d); }
            else std::cerr << "Tile MISSING " << path << " -> magenta\n";
            for (int p = 0; p < T * T; p++) {
                bool odd = ((p / T) + p) % 2;
                all[(i * T * T + p) * 4 + 0] = 255;
                all[(i * T * T + p) * 4 + 1] = 0;
                all[(i * T * T + p) * 4 + 2] = (unsigned char)(odd ? 255 : 0);
                all[(i * T * T + p) * 4 + 3] = 255;
            }
        }
        if (d) stbi_image_free(d);
    }
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, T, T, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, all.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}
