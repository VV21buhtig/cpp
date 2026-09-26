#ifndef WORLD_H
#define WORLD_H

#include "engine/chunk.h"
#include <glm/glm.hpp>
#include <vector>

// Engine: мир 3x3 чанка, плоский пол y=0. Соседи видны через границы (без швов).
struct World {
    static const int CX = 3, CZ = 3;
    Chunk chunks[CX][CZ];

    World();
    bool inXZ(int wx, int wz) const {
        return wx >= 0 && wx < CX * Chunk::SX && wz >= 0 && wz < CZ * Chunk::SZ;
    }
    unsigned char getBlock(int wx, int y, int wz) const;
    void setBlock(int wx, int y, int wz, unsigned char v);
    // меш одного чанка с учётом соседних чанков (без швов)
    std::vector<float> buildChunk(int cx, int cz) const;
    // пик по всем вокселям мира. Возвращает t или -1; +норма грани для place.
    float pick(glm::vec3 o, glm::vec3 d, float maxDist,
               int& wx, int& wy, int& wz, glm::vec3& normal) const;
};

#endif
