#include "engine/world.h"
#include <algorithm>
#include <cmath>

static const float kWF[6][6][8] = {
    {{0,1,0, 0,0,-1, 0,1},{1,1,0, 0,0,-1, 1,1},{1,0,0, 0,0,-1, 1,0},
     {1,0,0, 0,0,-1, 1,0},{0,0,0, 0,0,-1, 0,0},{0,1,0, 0,0,-1, 0,1}},
    {{0,0,1, 0,0,1, 0,0},{1,0,1, 0,0,1, 1,0},{1,1,1, 0,0,1, 1,1},
     {1,1,1, 0,0,1, 1,1},{0,1,1, 0,0,1, 0,1},{0,0,1, 0,0,1, 0,0}},
    {{0,1,1, -1,0,0, 1,0},{0,1,0, -1,0,0, 1,1},{0,0,0, -1,0,0, 0,1},
     {0,0,0, -1,0,0, 0,1},{0,0,1, -1,0,0, 0,0},{0,1,1, -1,0,0, 1,0}},
    {{1,0,0, 1,0,0, 0,1},{1,1,0, 1,0,0, 1,1},{1,1,1, 1,0,0, 1,0},
     {1,1,1, 1,0,0, 1,0},{1,0,1, 1,0,0, 0,0},{1,0,0, 1,0,0, 0,1}},
    {{0,0,0, 0,-1,0, 0,1},{1,0,0, 0,-1,0, 1,1},{1,0,1, 0,-1,0, 1,0},
     {1,0,1, 0,-1,0, 1,0},{0,0,1, 0,-1,0, 0,0},{0,0,0, 0,-1,0, 0,1}},
    {{1,1,1, 0,1,0, 1,0},{1,1,0, 0,1,0, 1,1},{0,1,0, 0,1,0, 0,1},
     {0,1,0, 0,1,0, 0,1},{0,1,1, 0,1,0, 0,0},{1,1,1, 0,1,0, 1,0}},
};
static const int kWN[6][3] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};

World::World() {
    for (int cz = 0; cz < CZ; cz++)
        for (int cx = 0; cx < CX; cx++)
            for (int z = 0; z < Chunk::SZ; z++)
                for (int x = 0; x < Chunk::SX; x++)
                    chunks[cx][cz].set(x, 0, z, 1); // чистый пол, без столбиков и висяков
}

unsigned char World::getBlock(int wx, int y, int wz) const {
    if (y < 0) return 1;
    if (y >= Chunk::SY) return 0;
    if (!inXZ(wx, wz)) return 0;
    return chunks[wx / Chunk::SX][wz / Chunk::SZ].get(wx % Chunk::SX, y, wz % Chunk::SZ);
}

void World::setBlock(int wx, int y, int wz, unsigned char v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    chunks[wx / Chunk::SX][wz / Chunk::SZ].set(wx % Chunk::SX, y, wz % Chunk::SZ, v);
}

std::vector<float> World::buildChunk(int cx, int cz) const {
    std::vector<float> out;
    out.reserve(8192 * 8);
    for (int z = 0; z < Chunk::SZ; z++)
    for (int y = 0; y < Chunk::SY; y++)
    for (int x = 0; x < Chunk::SX; x++) {
        int wx = cx * Chunk::SX + x, wz = cz * Chunk::SZ + z;
        if (getBlock(wx, y, wz) == 0) continue;
        for (int f = 0; f < 6; f++) {
            if (getBlock(wx + kWN[f][0], y + kWN[f][1], wz + kWN[f][2]) != 0) continue;
            for (int v = 0; v < 6; v++) {
                out.push_back(kWF[f][v][0] + x);
                out.push_back(kWF[f][v][1] + y);
                out.push_back(kWF[f][v][2] + z);
                out.push_back(kWF[f][v][3]);
                out.push_back(kWF[f][v][4]);
                out.push_back(kWF[f][v][5]);
                out.push_back(kWF[f][v][6]);
                out.push_back(kWF[f][v][7]);
            }
        }
    }
    return out;
}

static float rayBox(glm::vec3 o, glm::vec3 d, glm::vec3 mn, glm::vec3 mx, glm::vec3& n) {
    float tmin = 0.0f, tmax = 1e9f;
    glm::vec3 nn(0.0f);
    for (int a = 0; a < 3; a++) {
        float dd = d[a], oo = o[a];
        if (fabs(dd) < 1e-8f) {
            if (oo < mn[a] || oo > mx[a]) return -1.0f;
        } else {
            float t1 = (mn[a] - oo) / dd, t2 = (mx[a] - oo) / dd;
            glm::vec3 n1(0.0f), n2(0.0f);
            n1[a] = -1.0f; n2[a] = 1.0f;
            if (t1 > t2) { std::swap(t1, t2); std::swap(n1, n2); }
            // направление: если луч идёт +, вход через min (норма -), иначе через max
            if (dd < 0) std::swap(n1, n2);
            if (t1 > tmin) { tmin = t1; nn = (dd > 0) ? glm::vec3(n1) : glm::vec3(n2); }
            // проще: нормаль по доминирующей оси входа
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1.0f;
        }
    }
    // нормаль: ось с максимальным t1
    float best = -1e9f; int ax = -1; float s = 0;
    for (int a = 0; a < 3; a++) {
        float t1 = (d[a] > 0) ? (mn[a]-o[a])/d[a] : (mx[a]-o[a])/d[a];
        if (fabs(d[a]) < 1e-8f) continue;
        if (t1 > best) { best = t1; ax = a; s = (d[a] > 0) ? -1.0f : 1.0f; }
    }
    n = glm::vec3(0.0f);
    if (ax >= 0) n[ax] = s;
    (void)nn;
    return tmin;
}

float World::pick(glm::vec3 o, glm::vec3 d, float maxDist,
                  int& wx, int& wy, int& wz, glm::vec3& normal) const {
    float best = maxDist;
    bool found = false;
    glm::vec3 bn(0.0f);
    for (int z = 0; z < CZ * Chunk::SZ; z++)
    for (int y = 0; y < Chunk::SY; y++)
    for (int x = 0; x < CX * Chunk::SX; x++) {
        if (getBlock(x, y, z) == 0) continue;
        glm::vec3 mn(x, y, z), mx(x + 1, y + 1, z + 1);
        glm::vec3 n(0.0f);
        float t = rayBox(o, d, mn, mx, n);
        if (t > 0.0f && t < best) { best = t; wx = x; wy = y; wz = z; bn = n; found = true; }
    }
    normal = bn;
    return found ? best : -1.0f;
}
