#ifndef WORLD_H
#define WORLD_H

#include "engine/chunk.h"
#include <glm/glm.hpp>
#include <vector>

// Engine: ограниченный мир nx*nz чанков по сиду (граница = пустота).
// Данные всех чанков всегда в RAM, стримятся только МЕШИ вокруг игрока.
// 0=air, 1=grass, 2=dirt, 3=stone.
struct World {
    int cx_ = 0, cz_ = 0, seed_ = 1337;
    std::vector<Chunk> chunks;

    World() {}
    World(int ncx, int ncz, int s) { init(ncx, ncz, s); }
    void init(int ncx, int ncz, int s); // resize + генерация холмов
    void clear();                       // вся air (под load)

    int ncx() const { return cx_; }
    int ncz() const { return cz_; }
    int sizeX() const { return cx_ * Chunk::SX; }
    int sizeZ() const { return cz_ * Chunk::SZ; }
    Chunk& at(int cx, int cz) { return chunks[cz * cx_ + cx]; }
    const Chunk& at(int cx, int cz) const { return chunks[cz * cx_ + cx]; }

    bool inXZ(int wx, int wz) const {
        return wx >= 0 && wx < sizeX() && wz >= 0 && wz < sizeZ();
    }
    unsigned char getBlock(int wx, int y, int wz) const;
    static bool isSolid(unsigned char id) { return id != 0 && id < 6; } // флюиды не твердые
    void setBlock(int wx, int y, int wz, unsigned char v);
    // меш одного чанка с учётом соседних чанков (без швов)
    std::vector<float> buildChunk(int cx, int cz) const;
    // флюиды отдельно: waterVerts (tile 4) + lavaVerts (tile 5), только грани к воздуху
    void buildFluids(int cx, int cz, std::vector<float>& water, std::vector<float>& lava) const;
    // пик DDA по всем вокселям. Возвращает t или -1; +норма грани для place.
    float pick(glm::vec3 o, glm::vec3 d, float maxDist,
               int& wx, int& wy, int& wz, glm::vec3& normal) const;
};

#endif
