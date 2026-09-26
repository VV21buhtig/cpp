#include "engine/world.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
                setBlock(wx, y, wz, y == h ? 1 : (y >= h - 2 ? 2 : 3)); // grass/dirt/stone
        }
}

unsigned char World::getBlock(int wx, int y, int wz) const {
    if (y < 0 || y >= Chunk::SY) return 0; // под миром пустота (дно острова мешится и видно снизу)
    if (!inXZ(wx, wz)) return 0;
    if (!inXZ(wx, wz)) return 0;
    return chunks[wx / Chunk::SX][wz / Chunk::SZ].get(wx % Chunk::SX, y, wz % Chunk::SZ);
}

void World::setBlock(int wx, int y, int wz, unsigned char v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    chunks[wx / Chunk::SX][wz / Chunk::SZ].set(wx % Chunk::SX, y, wz % Chunk::SZ, v);
}

std::vector<float> World::buildChunk(int cx, int cz) const {
    // Greedy + вершинное AO (0fps): маска хранит id, слияние равных,
    // углы семплят соседей, триангуляция с flip по AO. UV мировые (REPEAT).
    std::vector<float> out;
    out.reserve(4096 * 10);
    auto pushV = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v, float tile, float ao) {
        out.push_back(x); out.push_back(y); out.push_back(z);
        out.push_back(nx); out.push_back(ny); out.push_back(nz);
        out.push_back(u); out.push_back(v); out.push_back(tile); out.push_back(ao);
    };
    auto tileFor = [](unsigned char id, int axis, int sign) -> float {
        if (id == 1) return (axis == 1) ? (sign > 0 ? 0.0f : 2.0f) : 1.0f; // grass: top/side/bottom(dirt)
        if (id == 2) return 2.0f;
        return 3.0f; // stone и всё остальное
    };
    const int S = Chunk::SX;
    int wx0 = cx * S, wz0 = cz * S;
    // corner AO: 3 клетки снаружи грани (статья 0fps). A/B — касательные, o — наружу.
    auto occ = [&](int x, int y, int z) -> int { return getBlock(x, y, z) ? 1 : 0; };
    for (int d = 0; d < 6; d++) {
        int axis = d / 2;       // 0=x 1=y 2=z
        int sign = (d % 2 == 0) ? 1 : -1;
        for (int s = 0; s < S; s++) {
            unsigned char mask[16][16] = {};
            for (int v = 0; v < S; v++)
                for (int u = 0; u < S; u++) {
                    int bx, by, bz, ox = 0, oy = 0, oz = 0;
                    if (axis == 0)      { bx = wx0 + s; by = v; bz = wz0 + u; ox = sign; }
                    else if (axis == 1) { bx = wx0 + u; by = s; bz = wz0 + v; oy = sign; }
                    else                { bx = wx0 + u; by = v; bz = wz0 + s; oz = sign; }
                    unsigned char id = getBlock(bx, by, bz);
                    mask[v][u] = (id != 0 && getBlock(bx + ox, by + oy, bz + oz) == 0) ? id : 0;
                }
            bool done[16][16] = {};
            for (int v = 0; v < S; v++)
                for (int u = 0; u < S; u++) {
                    unsigned char id = mask[v][u];
                    if (!id || done[v][u]) continue;
                    int w = 1;
                    while (u + w < S && mask[v][u + w] == id && !done[v][u + w]) w++;
                    int h = 1;
                    bool grow = true;
                    while (v + h < S && grow) {
                        for (int k = 0; k < w; k++)
                            if (mask[v + h][u + k] != id || done[v + h][u + k]) { grow = false; break; }
                        if (grow) h++;
                    }
                    for (int dv = 0; dv < h; dv++)
                        for (int du = 0; du < w; du++) done[v + dv][u + du] = true;
                    float N[3] = {0, 0, 0};
                    N[axis] = (float)sign;
                    float tile = tileFor(id, axis, sign);
                    // AO четырёх углов; a0/b0: клетка снаружи прямоугольника
                    auto cornerAO = [&](int du, int dv) -> float {
                        int a0 = (du == 0) ? -1 : 0, b0 = (dv == 0) ? -1 : 0;
                        int o = (sign > 0) ? 0 : -1;
                        int gx, gy, gz, ax, ay, az, bx, by, bz, nx, ny, nz;
                        if (axis == 0)      { gx = wx0 + s + (sign > 0 ? 1 : 0); gy = v + dv; gz = wz0 + u + du;
                                              ax = 0; ay = 0; az = 1; bx = 0; by = 1; bz = 0; nx = 1; ny = 0; nz = 0; }
                        else if (axis == 1) { gx = wx0 + u + du; gy = s + (sign > 0 ? 1 : 0); gz = wz0 + v + dv;
                                              ax = 1; ay = 0; az = 0; bx = 0; by = 0; bz = 1; nx = 0; ny = 1; nz = 0; }
                        else                { gx = wx0 + u + du; gy = v + dv; gz = wz0 + s + (sign > 0 ? 1 : 0);
                                              ax = 1; ay = 0; az = 0; bx = 0; by = 1; bz = 0; nx = 0; ny = 0; nz = 1; }
                        int s1 = occ(gx + nx*o + ax*a0, gy + ny*o + ay*a0, gz + nz*o + az*a0);
                        int s2 = occ(gx + nx*o + bx*b0, gy + ny*o + by*b0, gz + nz*o + bz*b0);
                        int cc = occ(gx + nx*o + ax*a0 + bx*b0, gy + ny*o + ay*a0 + by*b0, gz + nz*o + az*a0 + bz*b0);
                        return (s1 && s2) ? 0.0f : (float)(3 - (s1 + s2 + cc));
                    };
                    float a00 = cornerAO(0, 0), a10 = cornerAO(w, 0);
                    float a11 = cornerAO(w, h), a01 = cornerAO(0, h);
                    auto vert = [&](int du, int dv, float ao) {
                        float x, y, z, uu, vv;
                        if (axis == 0)      { x = (float)(s + (sign > 0 ? 1 : 0)); y = (float)(v + dv); z = (float)(u + du); uu = (float)(wz0 + u + du); vv = (float)(v + dv); }
                        else if (axis == 1) { x = (float)(u + du); y = (float)(s + (sign > 0 ? 1 : 0)); z = (float)(v + dv); uu = (float)(wx0 + u + du); vv = (float)(wz0 + v + dv); }
                        else                { x = (float)(u + du); y = (float)(v + dv); z = (float)(s + (sign > 0 ? 1 : 0)); uu = (float)(wx0 + u + du); vv = (float)(v + dv); }
                        pushV(x, y, z, N[0], N[1], N[2], uu, vv, tile, ao);
                    };
                    // id угла: 0:(0,0) 1:(w,0) 2:(w,h) 3:(0,h); flip по правилу 0fps
                    bool flip = (a00 + a11 > a01 + a10);
                    struct C { int du, dv; float ao; };
                    C c[4] = {{0,0,a00},{w,0,a10},{w,h,a11},{0,h,a01}};
                    int tri[6];
                    if (axis == 2) {
                        if (sign > 0) { if (!flip) { int t[6]={0,1,2, 0,2,3}; memcpy(tri,t,sizeof t); } else { int t[6]={1,2,3, 1,3,0}; memcpy(tri,t,sizeof t); } }
                        else          { if (!flip) { int t[6]={0,2,1, 0,3,2}; memcpy(tri,t,sizeof t); } else { int t[6]={1,3,2, 1,0,3}; memcpy(tri,t,sizeof t); } }
                    } else {
                        if (sign > 0) { if (!flip) { int t[6]={0,2,1, 0,3,2}; memcpy(tri,t,sizeof t); } else { int t[6]={1,3,2, 1,0,3}; memcpy(tri,t,sizeof t); } }
                        else          { if (!flip) { int t[6]={0,1,2, 0,2,3}; memcpy(tri,t,sizeof t); } else { int t[6]={1,2,3, 1,3,0}; memcpy(tri,t,sizeof t); } }
                    }
                    for (int k = 0; k < 6; k++) vert(c[tri[k]].du, c[tri[k]].dv, c[tri[k]].ao);
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
