#ifndef CHUNK_H
#define CHUNK_H

#include <vector>

// Engine: один чанк 16x16x16. 0=air, 1=solid. Мешим только видимые грани (CCW).
struct Chunk {
    static const int SX = 16, SY = 16, SZ = 16;
    unsigned char blocks[SX * SY * SZ] = {};

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
    // строит interleaved pos3+norm3+uv2 только для exposed-граней
    std::vector<float> buildMesh() const;
};

// Engine: динамический VAO/VBO чанка. Отдельно от CubeMesh чтобы не трогать буферы кубов.
class ChunkMesh {
public:
    unsigned int VAO = 0, VBO = 0;
    int vertexCount = 0;
    void upload(const std::vector<float>& data);
    void draw() const;
    void destroy();
};

#endif
