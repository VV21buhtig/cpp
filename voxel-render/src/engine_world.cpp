#include "engine/world.h"
#include <algorithm>
#include <cmath>

World::World(int s) : seed(s) {
    // Холмы: value-noise 2 октавы + сид. h=1..4.
    auto hash01 = [s](int x, int z) -> float {
        int h = (x + s * 131) * 374761393 + (z + s * 57) * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        h = h ^ (h >> 16);
        return (float)(h & 0xffff) / 65535.0f;
    };
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    auto noise2 = [&](float fx, float fz) {
        int x0 = (int)floor(fx), z0 = (int)floor(fz);
        float tx = smooth(fx - x0), tz = smooth(fz - z0);
        float a = hash01(x0, z0), b = hash01(x0 + 1, z0);
        float c = hash01(x0, z0 + 1), d = hash01(x0 + 1, z0 + 1);
        return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
    };
    for (int wz = 0; wz < CZ * Chunk::SZ; wz++)
        for (int wx = 0; wx < CX * Chunk::SX; wx++) {
            float n = 0.65f * noise2(wx / 9.0f, wz / 9.0f)
                    + 0.35f * noise2(wx / 4.0f + 13.7f, wz / 4.0f + 7.3f);
            int h = 1 + (int)(n * 3.0f); // 1..4
            for (int y = 0; y <= h && y < Chunk::SY; y++)
                setBlock(wx, y, wz, 1);
        }
}

unsigned char World::getBlock(int wx, int y, int wz) const {
    if (y < 0) return inXZ(wx, wz) ? 1 : 0; // под миром solid (дно не мешим), за миром пустота
    if (y >= Chunk::SY) return 0;
    if (!inXZ(wx, wz)) return 0;
    return chunks[wx / Chunk::SX][wz / Chunk::SZ].get(wx % Chunk::SX, y, wz % Chunk::SZ);
}

void World::setBlock(int wx, int y, int wz, unsigned char v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    chunks[wx / Chunk::SX][wz / Chunk::SZ].set(wx % Chunk::SX, y, wz % Chunk::SZ, v);
}

std::vector<float> World::buildChunk(int cx, int cz) const {
    // Greedy: по каждой оси и направлению строим маску 16x16 на слайс и сливаем
    // в прямоугольники. UV — в мировых координатах блоков (шов бесшовный, REPEAT).
    std::vector<float> out;
    out.reserve(4096 * 8);
    auto pushV = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v) {
        out.push_back(x); out.push_back(y); out.push_back(z);
        out.push_back(nx); out.push_back(ny); out.push_back(nz);
        out.push_back(u); out.push_back(v);
    };
    const int S = Chunk::SX;
    int wx0 = cx * S, wz0 = cz * S;
    // dir: 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z
    for (int d = 0; d < 6; d++) {
        int axis = d / 2;       // 0=x 1=y 2=z
        int sign = (d % 2 == 0) ? 1 : -1;
        for (int s = 0; s < S; s++) {
            bool mask[16][16] = {};
            for (int v = 0; v < S; v++)
                for (int u = 0; u < S; u++) {
                    int bx, by, bz, ox = 0, oy = 0, oz = 0;
                    if (axis == 0)      { bx = wx0 + s; by = v; bz = wz0 + u; ox = sign; }
                    else if (axis == 1) { bx = wx0 + u; by = s; bz = wz0 + v; oy = sign; }
                    else                { bx = wx0 + u; by = v; bz = wz0 + s; oz = sign; }
                    mask[v][u] = getBlock(bx, by, bz) != 0 && getBlock(bx + ox, by + oy, bz + oz) == 0;
                }
            bool done[16][16] = {};
            for (int v = 0; v < S; v++)
                for (int u = 0; u < S; u++) {
                    if (!mask[v][u] || done[v][u]) continue;
                    int w = 1;
                    while (u + w < S && mask[v][u + w] && !done[v][u + w]) w++;
                    int h = 1;
                    bool grow = true;
                    while (v + h < S && grow) {
                        for (int k = 0; k < w; k++)
                            if (!mask[v + h][u + k] || done[v + h][u + k]) { grow = false; break; }
                        if (grow) h++;
                    }
                    for (int dv = 0; dv < h; dv++)
                        for (int du = 0; du < w; du++) done[v + dv][u + du] = true;
                    float N[3] = {0, 0, 0};
                    N[axis] = (float)sign;
                    // P00=(u,v) P10=(u+w,v) P11=(u+w,v+h) P01=(u,v+h); UV мировые
                    auto vert = [&](int du, int dv) {
                        float x, y, z, uu, vv;
                        if (axis == 0)      { x = (float)(s + (sign > 0 ? 1 : 0)); y = (float)(v + dv); z = (float)(u + du); uu = (float)(wz0 + u + du); vv = (float)(v + dv); }
                        else if (axis == 1) { x = (float)(u + du); y = (float)(s + (sign > 0 ? 1 : 0)); z = (float)(v + dv); uu = (float)(wx0 + u + du); vv = (float)(wz0 + v + dv); }
                        else                { x = (float)(u + du); y = (float)(v + dv); z = (float)(s + (sign > 0 ? 1 : 0)); uu = (float)(wx0 + u + du); vv = (float)(v + dv); }
                        pushV(x, y, z, N[0], N[1], N[2], uu, vv);
                    };
                    if (sign > 0) {
                        if (axis == 2) { vert(0,0); vert(w,0); vert(w,h); vert(0,0); vert(w,h); vert(0,h); }
                        else           { vert(0,0); vert(w,h); vert(w,0); vert(0,0); vert(0,h); vert(w,h); }
                    } else {
                        if (axis == 2) { vert(0,0); vert(w,h); vert(w,0); vert(0,0); vert(0,h); vert(w,h); }
                        else           { vert(0,0); vert(w,0); vert(w,h); vert(0,0); vert(w,h); vert(0,h); }
                    }
                }
        }
    }
    return out;
}

float World::pick(glm::vec3 o, glm::vec3 d, float maxDist,
                  int& wx, int& wy, int& wz, glm::vec3& normal) const {
    // DDA (Amanatides & Woo): шагаем по сетке, а не по всем блокам.
    int x = (int)floor(o.x), y = (int)floor(o.y), z = (int)floor(o.z);
    int sx = (d.x > 0) ? 1 : -1, sy = (d.y > 0) ? 1 : -1, sz = (d.z > 0) ? 1 : -1;
    const float INF = 1e9f;
    float tdx = (fabs(d.x) < 1e-8f) ? INF : fabs(1.0f / d.x);
    float tdy = (fabs(d.y) < 1e-8f) ? INF : fabs(1.0f / d.y);
    float tdz = (fabs(d.z) < 1e-8f) ? INF : fabs(1.0f / d.z);
    float tmx = (fabs(d.x) < 1e-8f) ? INF : ((sx > 0 ? (x + 1 - o.x) : (o.x - x)) * tdx);
    float tmy = (fabs(d.y) < 1e-8f) ? INF : ((sy > 0 ? (y + 1 - o.y) : (o.y - y)) * tdy);
    float tmz = (fabs(d.z) < 1e-8f) ? INF : ((sz > 0 ? (z + 1 - o.z) : (o.z - z)) * tdz);
    if (getBlock(x, y, z)) { wx = x; wy = y; wz = z; normal = glm::vec3(0.0f); return 0.0f; }
    float t = 0.0f;
    glm::vec3 n(0.0f);
    while (t <= maxDist) {
        if (tmx < tmy && tmx < tmz)      { x += sx; t = tmx; tmx += tdx; n = glm::vec3((float)-sx, 0, 0); }
        else if (tmy < tmz)              { y += sy; t = tmy; tmy += tdy; n = glm::vec3(0, (float)-sy, 0); }
        else                             { z += sz; t = tmz; tmz += tdz; n = glm::vec3(0, 0, (float)-sz); }
        if (t > maxDist) break;
        if (getBlock(x, y, z)) { wx = x; wy = y; wz = z; normal = n; return t; }
    }
    return -1.0f;
}
