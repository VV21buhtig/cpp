#ifndef CHUNK_H
#define CHUNK_H

#include <vector>

// Engine: один чанк 16x16x16. Свойства ids — в blocks.json (см. BlockId в engine/blocks.h).
// Мешим только видимые грани (CCW).
struct Chunk {
    static const int SX = 16, SY = 64, SZ = 16;
    // Свет baked (P1, Luanti-идея): младшие 4 бита = день (солнце), старшие = ночь (блок).
    // SUN=15 идёт столбом вниз, остальное flood fill -1/воксель. В solid всегда 0.
    static const int LIGHT_MAX = 14, LIGHT_SUN = 15;
    unsigned char blocks[SX * SY * SZ] = {};
    unsigned char flow[SX * SY * SZ] = {}; // 0 нет, 1..8 уровень (8 источник)
    unsigned char light[SX * SY * SZ] = {}; // day:4 | night:4<<4

    int idx(int x, int y, int z) const { return x + SX * (y + SY * z); }
    bool inBounds(int x, int y, int z) const {
        return x >= 0 && x < SX && y >= 0 && y < SY && z >= 0 && z < SZ;
    }
    unsigned char get(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return 0; // за границей = воздух -> грань видима
        return blocks[idx(x, y, z)];
    }
    void set(int x, int y, int z, unsigned char v) {
        if (inBounds(x, y, z)) blocks[idx(x, y, z)] = v;
    }
    int getDay(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return 0;
        return light[idx(x, y, z)] & 0xF;
    }
    int getNight(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return 0;
        return (light[idx(x, y, z)] >> 4) & 0xF;
    }
    void setDay(int x, int y, int z, int v) {
        if (!inBounds(x, y, z)) return;
        unsigned char& c = light[idx(x, y, z)];
        c = (unsigned char)((c & 0xF0) | (v & 0xF));
    }
    void setNight(int x, int y, int z, int v) {
        if (!inBounds(x, y, z)) return;
        unsigned char& c = light[idx(x, y, z)];
        c = (unsigned char)((c & 0x0F) | ((v & 0xF) << 4));
    }
};

// Engine: динамический VAO/VBO чанка (формат pos3+norm3+uv2+tile1+ao1+day1+night1 = 12).
class ChunkMesh {
public:
    unsigned int VAO = 0, VBO = 0;
    int vertexCount = 0;
    void upload(const std::vector<float>& data);
    void draw() const;
    void destroy();
};

#endif
