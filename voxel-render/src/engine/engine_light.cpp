#include "engine/world.h"
#include <cstring>
#include <vector>

// P1 baked-свет. Идеи Luanti (voxelalgorithms.cpp): два независимых банка,
// затухание строго -1/воксель, солнце SUN=15 идёт столбом вниз пока не opaque.
// Упрощение: вместо unspread/relight-очередей правка пересчитывает бокс ±16
// (дальше свет 15..0 физически не достаёт) с подпиткой от границ бокса.

bool World::isOpaque(unsigned char id) {
    const BlockDef& d = gBlocks.get(id);
    return d.solid && !d.cutout; // листва с дырками свет пропускает
}

int World::getDay(int wx, int y, int wz) const {
    if (y >= Chunk::SY) return Chunk::LIGHT_SUN; // открытое небо
    if (y < 0 || !inXZ(wx, wz)) return 0;
    return at(wx / Chunk::SX, wz / Chunk::SZ).getDay(wx % Chunk::SX, y, wz % Chunk::SZ);
}

int World::getNight(int wx, int y, int wz) const {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return 0;
    return at(wx / Chunk::SX, wz / Chunk::SZ).getNight(wx % Chunk::SX, y, wz % Chunk::SZ);
}

void World::setDay(int wx, int y, int wz, int v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    at(wx / Chunk::SX, wz / Chunk::SZ).setDay(wx % Chunk::SX, y, wz % Chunk::SZ, v);
}

void World::setNight(int wx, int y, int wz, int v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    at(wx / Chunk::SX, wz / Chunk::SZ).setNight(wx % Chunk::SX, y, wz % Chunk::SZ, v);
}

namespace {
// BFS-разливка одного банка строго внутрь бокса [x0..x1]x[z0..z1], вся высота.
// Очередь — вектор с головой (FIFO без pop-стоимости). Код позиции: ((y*D)+z)*W+x.
void spreadBox(World& w, std::vector<int>& q, bool day, int x0, int x1, int z0, int z1) {
    const int W = w.sizeX(), D = w.sizeZ(), H = Chunk::SY;
    static const int OX[6] = {1, -1, 0, 0, 0, 0};
    static const int OY[6] = {0, 0, 1, -1, 0, 0};
    static const int OZ[6] = {0, 0, 0, 0, 1, -1};
    size_t head = 0;
    while (head < q.size()) {
        int code = q[head++];
        int x = code % W, z = (code / W) % D, y = code / (W * D);
        int cur = day ? w.getDay(x, y, z) : w.getNight(x, y, z);
        if (cur <= 1) continue;
        int nl = cur - 1;
        for (int d = 0; d < 6; d++) {
            int nx = x + OX[d], ny = y + OY[d], nz = z + OZ[d];
            if (ny < 0 || ny >= H) continue;
            if (nx < x0 || nx > x1 || nz < z0 || nz > z1) continue; // граница бокса — сток
            if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
            if (World::isOpaque(w.getBlock(nx, ny, nz))) continue;
            int have = day ? w.getDay(nx, ny, nz) : w.getNight(nx, ny, nz);
            if (have < nl) {
                if (day) w.setDay(nx, ny, nz, nl);
                else w.setNight(nx, ny, nz, nl);
                q.push_back((ny * D + nz) * W + nx);
            }
        }
    }
}
} // namespace

void World::rebuildLight() {
    const int W = sizeX(), D = sizeZ(), H = Chunk::SY;
    for (int cz = 0; cz < cz_; cz++)
        for (int cx = 0; cx < cx_; cx++)
            memset(at(cx, cz).light, 0, sizeof(at(cx, cz).light));
    std::vector<int> qd, qn;
    qd.reserve(1 << 20); qn.reserve(1 << 16);
    // Солнечные столбы: сверху вниз пока не opaque.
    for (int z = 0; z < D; z++)
        for (int x = 0; x < W; x++) {
            for (int y = H - 1; y >= 0; y--) {
                if (isOpaque(getBlock(x, y, z))) break;
                setDay(x, y, z, Chunk::LIGHT_SUN);
                qd.push_back((y * D + z) * W + x);
            }
        }
    // Эмиттеры (лава).
    for (int z = 0; z < D; z++)
        for (int x = 0; x < W; x++)
            for (int y = 0; y < H; y++) {
                int e = gBlocks.get(getBlock(x, y, z)).emit;
                if (e > 0 && !isOpaque(getBlock(x, y, z))) {
                    setNight(x, y, z, e);
                    qn.push_back((y * D + z) * W + x);
                }
            }
    spreadBox(*this, qd, true, 0, W - 1, 0, D - 1);
    spreadBox(*this, qn, false, 0, W - 1, 0, D - 1);
}

void World::updateLightAt(int wx, int y, int wz) {
    const int W = sizeX(), D = sizeZ(), H = Chunk::SY;
    (void)y; // бокс во всю высоту: солнечный столб зависит от верха
    int x0 = wx - 16, x1 = wx + 16, z0 = wz - 16, z1 = wz + 16;
    if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0;
    if (x1 >= W) x1 = W - 1; if (z1 >= D) z1 = D - 1;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            for (int yy = 0; yy < H; yy++) {
                setDay(x, yy, z, 0);
                setNight(x, yy, z, 0);
            }
    std::vector<int> qd, qn;
    qd.reserve(1 << 15); qn.reserve(1 << 12);
    // Солнце в боксе.
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            for (int yy = H - 1; yy >= 0; yy--) {
                if (isOpaque(getBlock(x, yy, z))) break;
                setDay(x, yy, z, Chunk::LIGHT_SUN);
                qd.push_back((yy * D + z) * W + x);
            }
        }
    // Эмиттеры в боксе.
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            for (int yy = 0; yy < H; yy++) {
                int e = gBlocks.get(getBlock(x, yy, z)).emit;
                if (e > 0 && !isOpaque(getBlock(x, yy, z))) {
                    if (getNight(x, yy, z) < e) {
                        setNight(x, yy, z, e);
                        qn.push_back((yy * D + z) * W + x);
                    }
                }
            }
    // Подпитка от света снаружи бокса (свет ходит на 15, бокс 16 — хватает).
    static const int OX[6] = {1, -1, 0, 0, 0, 0};
    static const int OY[6] = {0, 0, 1, -1, 0, 0};
    static const int OZ[6] = {0, 0, 0, 0, 1, -1};
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            if (x != x0 && x != x1 && z != z0 && z != z1) continue; // только граница
            for (int yy = 0; yy < H; yy++) {
                if (isOpaque(getBlock(x, yy, z))) continue;
                for (int d = 0; d < 6; d++) {
                    int nx = x + OX[d], ny = yy + OY[d], nz = z + OZ[d];
                    if (ny < 0 || ny >= H) continue;
                    if (nx >= x0 && nx <= x1 && nz >= z0 && nz <= z1) continue;
                    if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                    if (isOpaque(getBlock(nx, ny, nz))) continue;
                    int od = getDay(nx, ny, nz) - 1;
                    if (od > getDay(x, yy, z)) {
                        setDay(x, yy, z, od);
                        qd.push_back((yy * D + z) * W + x);
                    }
                    int on = getNight(nx, ny, nz) - 1;
                    if (on > getNight(x, yy, z)) {
                        setNight(x, yy, z, on);
                        qn.push_back((yy * D + z) * W + x);
                    }
                }
            }
        }
    spreadBox(*this, qd, true, x0, x1, z0, z1);
    spreadBox(*this, qn, false, x0, x1, z0, z1);
}

void World::editBlock(int wx, int y, int wz, unsigned char v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    at(wx / Chunk::SX, wz / Chunk::SZ).set(wx % Chunk::SX, y, wz % Chunk::SZ, v);
    markFluidDirty(wx / Chunk::SX, wz / Chunk::SZ);
    updateLightAt(wx, y, wz);
}
