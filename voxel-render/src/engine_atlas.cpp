#include "engine/atlas.h"
#include <glad/gl.h>
#include <stb/stb_image.h>
#include <iostream>
#include <vector>
#include <cstring>

static void resizeRGBA(const unsigned char* src, int sw, int sh,
                       std::vector<unsigned char>& dst, int dw, int dh) {
    dst.resize(dw * dh * 4);
    for (int y = 0; y < dh; y++) {
        float sy = (y + 0.5f) * sh / dh - 0.5f;
        int y0 = (int)sy; float fy = sy - y0;
        if (y0 < 0) { y0 = 0; fy = 0; }
        if (y0 >= sh - 1) { y0 = sh - 1; fy = 0; }
        for (int x = 0; x < dw; x++) {
            float sx = (x + 0.5f) * sw / dw - 0.5f;
            int x0 = (int)sx; float fx = sx - x0;
            if (x0 < 0) { x0 = 0; fx = 0; }
            if (x0 >= sw - 1) { x0 = sw - 1; fx = 0; }
            for (int c = 0; c < 4; c++) {
                float a = src[(y0 * sw + x0) * 4 + c];
                float b = src[(y0 * sw + x0 + 1) * 4 + c];
                float cc = src[((y0 + 1) * sw + x0) * 4 + c];
                float d = src[((y0 + 1) * sw + x0 + 1) * 4 + c];
                dst[(y * dw + x) * 4 + c] = (unsigned char)(a + (b - a) * fx + (cc - a) * fy + (a - b - cc + d) * fx * fy);
            }
        }
    }
}

unsigned int makeAtlas2(const char* pathA, const char* pathB) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* dA = stbi_load(pathA, &w, &h, &ch, 4);
    if (!dA) { std::cerr << "Atlas FAIL A: " << pathA << "\n"; return 0; }
    int wA = w, hA = h;
    int w2, h2, ch2;
    unsigned char* dB = stbi_load(pathB, &w2, &h2, &ch2, 4);
    if (!dB) { std::cerr << "Atlas FAIL B: " << pathB << "\n"; stbi_image_free(dA); return 0; }
    const int T = 256;
    std::vector<unsigned char> tA, tB;
    resizeRGBA(dA, wA, hA, tA, T, T);
    resizeRGBA(dB, w2, h2, tB, T, T);
    stbi_image_free(dA); stbi_image_free(dB);
    std::vector<unsigned char> atlas(T * T * 2 * 4);
    for (int y = 0; y < T; y++) {
        memcpy(&atlas[(y * T * 2) * 4], &tA[(y * T) * 4], T * 4);
        memcpy(&atlas[(y * T * 2 + T) * 4], &tB[(y * T) * 4], T * 4);
    }
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, T * 2, T, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    std::cout << "Atlas 2x1: " << pathA << " + " << pathB << "\n";
    return tex;
}
