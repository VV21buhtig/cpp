#include "engine/world.h"
#include "engine/struct/config.h"
#include "engine/struct/tree.h"
#include "engine/struct/rock.h"
#include "engine/struct/mine.h"

void loadStructConfigs(StructConfigs& c) { c.load("structures.cfg"); }
#include <algorithm>
#include <cmath>
#include <cstring>

void World::init(int ncx, int ncz, int s) {
    cx_ = ncx; cz_ = ncz; seed_ = s;
    chunks.assign(ncx * ncz, Chunk());
    flowDirty_.assign(ncx * ncz, 0);
    // Рельеф как в новом MC: 2D-октавы (континенты/холмы/горы-маска) + 3D-карв пещер.
    // SEA=20. Горы до ~44. ids: 1 grass 2 dirt 3 stone 6 water 7 lava.
    const int SEA = 20;
    auto hash2 = [s](int x, int z) -> float {
        int h = (x + s * 131) * 374761393 + (z + s * 57) * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        h = h ^ (h >> 16);
        return (float)(h & 0xffff) / 65535.0f;
    };
    auto hash3 = [s](int x, int y, int z) -> float {
        unsigned int h = (unsigned int)(x + s * 131) * 374761393u
                       + (unsigned int)(y + s * 733) * 2246822519u
                       + (unsigned int)(z + s * 57) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h = h ^ (h >> 16);
        return (float)(h & 0xffff) / 65535.0f;
    };
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    auto noise2 = [&](float fx, float fz) {
        int x0 = (int)floor(fx), z0 = (int)floor(fz);
        float tx = smooth(fx - x0), tz = smooth(fz - z0);
        float a = hash2(x0, z0), b = hash2(x0 + 1, z0);
        float c = hash2(x0, z0 + 1), d = hash2(x0 + 1, z0 + 1);
        return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
    };
    auto noise3 = [&](float fx, float fy, float fz) {
        int x0 = (int)floor(fx), y0 = (int)floor(fy), z0 = (int)floor(fz);
        float tx = smooth(fx - x0), ty = smooth(fy - y0), tz = smooth(fz - z0);
        float c000 = hash3(x0,y0,z0), c100 = hash3(x0+1,y0,z0);
        float c010 = hash3(x0,y0+1,z0), c110 = hash3(x0+1,y0+1,z0);
        float c001 = hash3(x0,y0,z0+1), c101 = hash3(x0+1,y0,z0+1);
        float c011 = hash3(x0,y0+1,z0+1), c111 = hash3(x0+1,y0+1,z0+1);
        float x00 = c000+(c100-c000)*tx, x10 = c010+(c110-c010)*tx;
        float x01 = c001+(c101-c001)*tx, x11 = c011+(c111-c011)*tx;
        float y0v = x00+(x10-x00)*ty, y1v = x01+(x11-x01)*ty;
        return y0v+(y1v-y0v)*tz;
    };
    // fbm на тех же hash2/hash3+noise (детерминизм тот же): lacunarity 2,
    // persist — параметр. Возврат нормирован 0..1 (сумма/сумма амплитуд).
    auto fbm2D = [&](float fx, float fz, int oct, float persist) {
        float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
        for (int i = 0; i < oct; i++) {
            sum += amp * noise2(fx * freq, fz * freq);
            norm += amp;
            amp *= persist;
            freq *= 2.0f;
        }
        return sum / norm;
    };
    auto fbm3D = [&](float fx, float fy, float fz, int oct, float persist) {
        float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
        for (int i = 0; i < oct; i++) {
            sum += amp * noise3(fx * freq, fy * freq, fz * freq);
            norm += amp;
            amp *= persist;
            freq *= 2.0f;
        }
        return sum / norm;
    };
    int W = ncx * Chunk::SX, D = ncz * Chunk::SZ;
    // 1. высота по L-схеме (mapgen v7): persist-карта P -> base/alt 5 окт ->
    // hselect 6 окт -> surface=max(base, mix). Те же единицы, что раньше
    // (cont*10+hills*6): 8..24 до гор. Горы — 3D-полем выше hi, реки —
    // вычитанием ниже hi. SEA=20.
    for (int wz = 0; wz < D; wz++)
        for (int wx = 0; wx < W; wx++) {
            float P = fbm2D(wx / 96.0f, wz / 96.0f, 3, 0.5f); // persist-карта, низкая частота
            float pers = 0.3f + 0.6f * P;
            float base = fbm2D(wx / 48.0f, wz / 48.0f, 5, pers);
            float alt = fbm2D(wx / 48.0f + 37.2f, wz / 48.0f + 11.9f, 5, pers); // другой сид-офсет
            float hsel = fbm2D(wx / 64.0f + 91.4f, wz / 64.0f + 51.7f, 6, 0.5f);
            if (hsel < 0.0f) hsel = 0.0f; if (hsel > 1.0f) hsel = 1.0f;
            float surf = alt * hsel + base * (1.0f - hsel);
            if (base > surf) surf = base;
            // Континенты (L): низкая частота тянет регионы в океан/сушу.
            // Амплитуда с запасом: впадины под глубокий океан, суша выше пляжа.
            // Иначе всё жмётся к SEA и выходят лужи-плёнки вместо океана.
            float cont = fbm2D(wx / 190.0f + 3.1f, wz / 190.0f + 8.7f, 2, 0.5f);
            float h = 2.0f + cont * 30.0f + (surf - 0.5f) * 12.0f;
            int hi = (int)h;
            if (hi >= Chunk::SY - 1) hi = Chunk::SY - 2;
            for (int y = 0; y <= hi; y++) setBlock(wx, y, wz, B_STONE); // пока камень
            // 1b. горы 3D-полем: mh=max(поле mountHeight,1);
            // solid = fbm3D(np_mountain) - (y-8)/mh >= 0, только выше hi.
            // Склоны крутые без террас (поле 3D, а не 2D-высота). Лимит SY-2.
            float mraw = (fbm2D(wx / 90.0f + 71.0f, wz / 90.0f + 3.0f, 4, 0.5f) - 0.60f) / 0.20f;
            if (mraw < 0.0f) mraw = 0.0f; if (mraw > 1.0f) mraw = 1.0f;
            float mount = smooth(mraw); // маска гор 0..1
            if (mount > 0.0f) {
                float mh = 1.0f + mount * 63.0f; // 1..64
                int cap = 8 + (int)(mh * 0.8f); // выше fbm3D почти не дотягивает — не считаем
                if (cap > Chunk::SY - 2) cap = Chunk::SY - 2;
                for (int y = hi + 1; y <= cap; y++) {
                    float m = fbm3D(wx / 28.0f + 5.0f, y / 20.0f + 9.0f, wz / 28.0f + 1.0f, 4, 0.5f)
                            - (float)(y - 8) / mh;
                    if (m >= 0.0f) setBlock(wx, y, wz, B_STONE);
                }
            }
            // 1c. реки-каньоны: uw=|ridge2D-0.5|*2 (0 на русле); где uw<=0.2
            // вычитаем камень формулой river=nr+(0.2-uw)*((y-SEA+17)/2.5)>=0.6,
            // nr=fbm3D(ridge3D)*max(y-SEA,0)/7. Только ниже hi (небо не дырявим).
            float uw = fabsf(fbm2D(wx / 110.0f + 17.3f, wz / 110.0f + 29.1f, 4, 0.5f) - 0.5f) * 2.0f;
            if (uw <= 0.2f) {
                for (int y = 3; y <= hi; y++) {
                    float over = (y > SEA) ? (float)(y - SEA) : 0.0f;
                    float nr = fbm3D(wx / 16.0f + 51.0f, y / 12.0f + 7.0f, wz / 16.0f + 23.0f,
                                     3, 0.5f) * over / 7.0f;
                    float river = nr + (0.2f - uw) * ((float)(y - SEA + 17) / 2.5f);
                    if (river >= 0.6f) setBlock(wx, y, wz, B_AIR);
                }
            }
            // 2. спагетти-пещеры: тонкая зона |n-0.5| (края шума), только ниже поверхности-1.
            // Taper: ширина душится pinch-полем (тоннели сходят на нет) + глубинным
            // сужением (внизу уже). Без этого везде одинаковые дудки.
            float pinch = fbm2D(wx / 40.0f + 3.7f, wz / 40.0f + 9.2f, 2, 0.5f);
            float pw = (pinch - 0.3f) / 0.4f;
            if (pw < 0.0f) pw = 0.0f; if (pw > 1.0f) pw = 1.0f;
            pw = smooth(pw);
            if (pw <= 0.0f) continue;
            for (int y = 1; y < hi - 1 && y < Chunk::SY; y++) {
                float depthT = 0.5f + 0.5f * ((float)(y - 2) / 12.0f);
                if (depthT > 1.0f) depthT = 1.0f;
                float thr = 0.012f * pw * depthT;
                if (thr < 0.002f) continue;
                float n = 0.55f * noise3(wx / 9.0f, y / 7.0f, wz / 9.0f)
                        + 0.45f * noise3(wx / 23.0f + 5.0f, y / 17.0f, wz / 23.0f + 9.0f);
                if (fabs(n - 0.5f) < thr) setBlock(wx, y, wz, B_AIR); // спагетти-тоннели
            }
        }
    // 3. флюиды: сначала лава на дне (иначе вода займёт низ), потом море
    for (int wz = 0; wz < D; wz++)
        for (int wx = 0; wx < W; wx++)
            for (int y = 0; y <= 2; y++)
                if (getBlock(wx, y, wz) == B_AIR) { setBlock(wx, y, wz, B_LAVA); setFlow(wx, y, wz, 8); } // лава на дне
    for (int wz = 0; wz < D; wz++)
        for (int wx = 0; wx < W; wx++)
            for (int y = 0; y <= SEA && y < Chunk::SY; y++)
                if (getBlock(wx, y, wz) == B_AIR) { setBlock(wx, y, wz, B_WATER); setFlow(wx, y, wz, 8); } // вода
    // 3b. Scatter-руды (L-идея): не поштучно, а блобы-кластеры random walk'ом.
    // Только в камень, детерминированно от сида (свой LCG, мир тот же при том же сиде).
    {
        struct Vein { unsigned char id; int count, size, ymin, ymax; unsigned salt; };
        Vein veins[4] = {{B_COAL, 600, 5, 2, 48, 101}, {B_IRON, 400, 4, 2, 32, 202},
                         {B_GOLD, 120, 4, 2, 16, 303}, {B_DIAMOND, 80, 4, 2, 10, 404}};
        for (auto& vn : veins) {
            unsigned rng = (unsigned)(s * 7919 + vn.salt * 104729 + 1);
            auto next = [&]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
            for (int i = 0; i < vn.count; i++) {
                int x = (int)(next() % (unsigned)W);
                int y = vn.ymin + (int)(next() % (unsigned)(vn.ymax - vn.ymin + 1));
                int z = (int)(next() % (unsigned)D);
                for (int b = 0; b < vn.size; b++) {
                    if (getBlock(x, y, z) == B_STONE) setBlock(x, y, z, vn.id);
                    x += (int)(next() % 3) - 1;
                    y += (int)(next() % 3) - 1;
                    z += (int)(next() % 3) - 1;
                    if (x < 0) x = 0; if (x >= W) x = W - 1;
                    if (z < 0) z = 0; if (z >= D) z = D - 1;
                    if (y < vn.ymin) y = vn.ymin; if (y > vn.ymax) y = vn.ymax;
                }
            }
        }
    }
    // 4. поверхность: верх трава (под водой земля), -3 земля, глубже камень.
    // Биомы по heat/humidity-картам: жарко+сухо = пустыня (песок вместо травы/земли).
    // Пляж у воды остаётся песком в любом биоме. Деревья пустыню пропускают сами
    // (только на траве), валуны — везде.
    for (int wz = 0; wz < D; wz++)
        for (int wx = 0; wx < W; wx++) {
            int top = -1;
            for (int y = Chunk::SY - 1; y >= 0; y--)
                if (isSolid(getBlock(wx, y, wz))) { top = y; break; }
            if (top < 0) continue;
            float heat = fbm2D(wx / 140.0f + 51.7f, wz / 140.0f + 13.3f, 2, 0.5f);
            float humid = fbm2D(wx / 140.0f + 7.9f, wz / 140.0f + 71.1f, 2, 0.5f);
            bool desert = heat > 0.52f && humid < 0.48f;
            for (int y = top; y >= 0 && y >= top - 3; y--) {
                unsigned char cur = getBlock(wx, y, wz);
                if (cur != B_STONE) continue;
                // Берег и дно — песок (блока 8 раньше не было!), суша выше — трава.
                if (y == top) setBlock(wx, y, wz, (top <= SEA + 1 || desert) ? B_SAND : B_GRASS);
                else setBlock(wx, y, wz, desert ? B_SAND : B_DIRT);
            }
        }
    for (auto& d : flowDirty_) d = 0; // сгенерированное стабильно
    // 5. структуры: конфиг + штампы (мир целиком в RAM — границ нет)
    {
        StructConfigs sc;
        loadStructConfigs(sc);
        stampPines(*this, sc.pine); // сосны ПЕРЕД дубами: редкие, иначе дубы занимают клетки
        stampTrees(*this, sc.tree);
        stampRocks(*this, sc.rock);
        stampMines(*this, sc.mine);
    }
    rebuildLight(); // P1: солнце столбами + эмиссия + BFS (сырой setBlock свет не трогает)
}

unsigned char World::getBlock(int wx, int y, int wz) const {
    if (y < 0 || y >= Chunk::SY) return 0; // под миром пустота (дно острова мешится и видно снизу)
    if (!inXZ(wx, wz)) return 0;
    return at(wx / Chunk::SX, wz / Chunk::SZ).get(wx % Chunk::SX, y, wz % Chunk::SZ);
}

void World::setBlock(int wx, int y, int wz, unsigned char v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    at(wx / Chunk::SX, wz / Chunk::SZ).set(wx % Chunk::SX, y, wz % Chunk::SZ, v);
    markFluidDirty(wx / Chunk::SX, wz / Chunk::SZ);
}

unsigned char World::getFlow(int wx, int y, int wz) const {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return 0;
    const Chunk& c = at(wx / Chunk::SX, wz / Chunk::SZ);
    return c.flow[c.idx(wx % Chunk::SX, y, wz % Chunk::SZ)];
}

void World::setFlow(int wx, int y, int wz, unsigned char v) {
    if (y < 0 || y >= Chunk::SY || !inXZ(wx, wz)) return;
    Chunk& c = at(wx / Chunk::SX, wz / Chunk::SZ);
    c.flow[c.idx(wx % Chunk::SX, y, wz % Chunk::SZ)] = v;
}

void World::markFluidDirty(int cx, int cz) {
    if (flowDirty_.empty()) return;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            int x = cx + dx, z = cz + dz;
            if (x >= 0 && z >= 0 && x < cx_ && z < cz_) flowDirty_[z * cx_ + x] = 1;
        }
}

bool World::fluidsDirty() const {
    for (char d : flowDirty_) if (d) return true;
    return false;
}

void World::takeFluidDirty(std::vector<int>& out) {
    out.clear();
    for (size_t i = 0; i < flowDirty_.size(); i++)
        if (flowDirty_[i]) { out.push_back((int)i); flowDirty_[i] = 0; }
}

// Тик: вода каждый, лава каждый 4-й. Вниз = 8, вбок = level-1 (мин 1, макс 8 от источника).
int World::tickFluids(bool lavaTick) {
    int changed = 0;
    const int W = sizeX(), D = sizeZ();
    for (int cz = 0; cz < cz_; cz++)
        for (int cx = 0; cx < cx_; cx++) {
            if (!flowDirty_[cz * cx_ + cx]) continue;
            flowDirty_[cz * cx_ + cx] = 0; // съели флаг; новые пометки переживут
            for (int z = cz * 16; z < (cz + 1) * 16 && z < D; z++)
                for (int x = cx * 16; x < (cx + 1) * 16 && x < W; x++)
                    for (int y = 0; y < Chunk::SY; y++) {
                        unsigned char id = getBlock(x, y, z);
                        if (!isFluid(id)) continue;
                        if (id == B_LAVA && !lavaTick) continue;
                        unsigned char L = getFlow(x, y, z);
                        if (L == 0) L = 8;
                        // вниз
                        if (y > 0 && getBlock(x, y - 1, z) == 0) {
                            setBlock(x, y - 1, z, id);
                            setFlow(x, y - 1, z, 8);
                            markFluidDirty(x / 16, z / 16);
                            changed++;
                            continue;
                        }
                        if (L <= 1) continue;
                        // вбок
                        const int o[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                        for (auto& d : o) {
                            int nx = x + d[0], nz = z + d[1];
                            if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                            if (getBlock(nx, y, nz) != 0) continue;
                            unsigned char nl = (unsigned char)(L - 1);
                            if (nl == 0) continue;
                            unsigned char have = getBlock(nx, y, nz) == id ? getFlow(nx, y, nz) : 0;
                            if (nl > have) {
                                setBlock(nx, y, nz, id);
                                setFlow(nx, y, nz, nl);
                                markFluidDirty(nx / 16, nz / 16);
                                if (++changed > 6000) { cz = cz_; break; } // остаток следующим тиком
                            }
                        }
                    }
            // флаг НЕ гасим в конце: пометки за тик переживают для rebuild
        }
    return changed;
}

std::vector<float> World::buildChunk(int cx, int cz) const {
    // Greedy + вершинное AO (0fps): маска хранит id, слияние равных,
    // углы семплят соседей, триангуляция с flip по AO+свету (K-идея).
    // Свет угла = среднее day/night по не-opaque из 4 клеток вокруг угла
    // (L getSmoothLightCombined-идея). UV мировые (REPEAT).
    std::vector<float> out;
    out.reserve(4096 * 12);
    auto pushV = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v, float tile, float ao, float day, float night) {
        out.push_back(x); out.push_back(y); out.push_back(z);
        out.push_back(nx); out.push_back(ny); out.push_back(nz);
        out.push_back(u); out.push_back(v); out.push_back(tile); out.push_back(ao);
        out.push_back(day); out.push_back(night);
    };
    auto tileFor = [](unsigned char id, int axis, int sign) -> float {
        const BlockDef& d = gBlocks.get(id); // тайлы из blocks.json
        if (axis == 1) return (sign > 0) ? (float)d.tileTop : (float)d.tileBottom;
        return (float)d.tileSide;
    };
    const int S = Chunk::SX;
    int wx0 = cx * S, wz0 = cz * S;
    // Ключ greedy: id + AO/свет 4 углов клетки. Слияние только равных —
    // иначе градиент тянется через весь слитый квад длинными полосами
    // (гайд-правило greedy+AO: мержить можно лишь одинаковую освещённость).
    struct MQ { unsigned char id, ao[4], d[4], n[4]; };
    auto sameQ = [](const MQ& a, const MQ& b) { return memcmp(&a, &b, sizeof(MQ)) == 0; };
    // corner AO: 3 клетки снаружи грани (статья 0fps). A/B — касательные, o — наружу.
    auto occ = [&](int x, int y, int z) -> int { return getBlock(x, y, z) ? 1 : 0; };
    for (int d = 0; d < 6; d++) {
        int axis = d / 2;       // 0=x 1=y 2=z
        int sign = (d % 2 == 0) ? 1 : -1;
        int ns = (axis == 1) ? Chunk::SY : S;
        for (int s = 0; s < ns; s++) {
            int NU = S, NV = (axis == 1) ? S : Chunk::SY;
            // Угловые пробы AO (флуда нет — cornerLT удалён).
            auto cornerCells = [&](int cu, int cv, int du, int dv, int& ox, int& oy, int& oz,
                                   int& ax, int& ay, int& az,
                                   int& bx, int& by, int& bz,
                                   int& nx, int& ny, int& nz,
                                   int& gx, int& gy, int& gz) {
                int a0 = (du == 0) ? -1 : 0, b0 = (dv == 0) ? -1 : 0;
                int o = (sign > 0) ? 0 : -1;
                if (axis == 0)      { gx = wx0 + s + (sign > 0 ? 1 : 0); gy = cv + dv; gz = wz0 + cu + du;
                                      ax = 0; ay = 0; az = 1; bx = 0; by = 1; bz = 0; nx = 1; ny = 0; nz = 0; }
                else if (axis == 1) { gx = wx0 + cu + du; gy = s + (sign > 0 ? 1 : 0); gz = wz0 + cv + dv;
                                      ax = 1; ay = 0; az = 0; bx = 0; by = 0; bz = 1; nx = 0; ny = 1; nz = 0; }
                else                { gx = wx0 + cu + du; gy = cv + dv; gz = wz0 + s + (sign > 0 ? 1 : 0);
                                      ax = 1; ay = 0; az = 0; bx = 0; by = 1; bz = 0; nx = 0; ny = 0; nz = 1; }
                ox = nx*o + ax*a0; oy = ny*o + ay*a0; oz = nz*o + az*a0;
            };
            auto cornerAO = [&](int cu, int cv, int du, int dv) -> float {
                if (axis == 1) return 3.0f; // топы плоские (иначе полосы через 16-блочный квад)
                int ox, oy, oz, ax, ay, az, bx, by, bz, nx, ny, nz, gx, gy, gz;
                cornerCells(cu, cv, du, dv, ox, oy, oz, ax, ay, az, bx, by, bz, nx, ny, nz, gx, gy, gz);
                int b0 = (dv == 0) ? -1 : 0;
                int o = (sign > 0) ? 0 : -1;
                int s1 = occ(gx + ox, gy + oy, gz + oz);
                int s2 = occ(gx + nx*o + bx*b0, gy + ny*o + by*b0, gz + nz*o + bz*b0);
                int cc = occ(gx + ox + bx*b0, gy + oy + by*b0, gz + oz + bz*b0);
                return (s1 && s2) ? 0.0f : (float)(3 - (s1 + s2 + cc));
            };
            MQ mask[64][16] = {};
            for (int v = 0; v < NV; v++)
                for (int u = 0; u < NU; u++) {
                    int bx, by, bz, ox = 0, oy = 0, oz = 0;
                    if (axis == 0)      { bx = wx0 + s; by = v; bz = wz0 + u; ox = sign; }
                    else if (axis == 1) { bx = wx0 + u; by = s; bz = wz0 + v; oy = sign; }
                    else                { bx = wx0 + u; by = v; bz = wz0 + s; oz = sign; }
                    unsigned char id = getBlock(bx, by, bz);
                    unsigned char ob = getBlock(bx + ox, by + oy, bz + oz);
                    // флюиды не мешутся; грань нужна если сосед не opaque (вода видна насквозь).
                    // листва с дырками (cutout) соседей НЕ закрывает: иначе сквозь дыры
                    // видно полые внутренности (ствол без граней). Лист-лист давим как раньше.
                    const BlockDef& dd = gBlocks.get(id);
                    const BlockDef& od = gBlocks.get(ob);
                    bool oOpaque = od.solid && !(od.cutout && ob != id); // листва не закрывает чужие грани
                    if (!(dd.solid && !oOpaque)) continue;
                    MQ q; q.id = id;
                    const int DUs[4] = {0, 1, 1, 0}, DVs[4] = {0, 0, 1, 1};
                    // Флуда нет (решение): в ключе только id+AO. d/n поля нули.
                    for (int k = 0; k < 4; k++)
                        q.ao[k] = (unsigned char)cornerAO(u, v, DUs[k], DVs[k]);
                    mask[v][u] = q;
                }
            bool done[64][16] = {};
            for (int v = 0; v < NV; v++)
                for (int u = 0; u < NU; u++) {
                    MQ q0 = mask[v][u];
                    if (!q0.id || done[v][u]) continue;
                    int w = 1;
                    while (u + w < NU && sameQ(mask[v][u + w], q0) && !done[v][u + w]) w++;
                    int h = 1;
                    bool grow = true;
                    while (v + h < NV && grow) {
                        for (int k = 0; k < w; k++)
                            if (!sameQ(mask[v + h][u + k], q0) || done[v + h][u + k]) { grow = false; break; }
                        if (grow) h++;
                    }
                    for (int dv = 0; dv < h; dv++)
                        for (int du = 0; du < w; du++) done[v + dv][u + du] = true;
                    float N[3] = {0, 0, 0};
                    N[axis] = (float)sign;
                    float tile = tileFor(q0.id, axis, sign);
                    // Углы слитого квада — те же пробы. day/night константы (флуда нет).
                    float a00 = cornerAO(u, v, 0, 0), a10 = cornerAO(u, v, w, 0);
                    float a11 = cornerAO(u, v, w, h), a01 = cornerAO(u, v, 0, h);
                    auto vert = [&](int du, int dv, float ao) {
                        float x, y, z, uu, vv;
                        if (axis == 0)      { x = (float)(s + (sign > 0 ? 1 : 0)); y = (float)(v + dv); z = (float)(u + du); uu = (float)(wz0 + u + du); vv = (float)(v + dv); }
                        else if (axis == 1) { x = (float)(u + du); y = (float)(s + (sign > 0 ? 1 : 0)); z = (float)(v + dv); uu = (float)(wx0 + u + du); vv = (float)(wz0 + v + dv); }
                        else                { x = (float)(u + du); y = (float)(v + dv); z = (float)(s + (sign > 0 ? 1 : 0)); uu = (float)(wx0 + u + du); vv = (float)(v + dv); }
                        pushV(x, y, z, N[0], N[1], N[2], uu, vv, tile, ao, 15.0f, 0.0f);
                    };
                    // id угла: 0:(0,0) 1:(w,0) 2:(w,h) 3:(0,h); flip по AO (флуда нет).
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
    if (isSolid(getBlock(x, y, z))) { wx = x; wy = y; wz = z; normal = glm::vec3(0.0f); return 0.0f; }
    float t = 0.0f;
    glm::vec3 n(0.0f);
    while (t <= maxDist) {
        if (tmx < tmy && tmx < tmz)      { x += sx; t = tmx; tmx += tdx; n = glm::vec3((float)-sx, 0, 0); }
        else if (tmy < tmz)              { y += sy; t = tmy; tmy += tdy; n = glm::vec3(0, (float)-sy, 0); }
        else                             { z += sz; t = tmz; tmz += tdz; n = glm::vec3(0, 0, (float)-sz); }
        if (t > maxDist) break;
        if (isSolid(getBlock(x, y, z))) { wx = x; wy = y; wz = z; normal = n; return t; }
    }
    return -1.0f;
}

void World::buildFluids(int cx, int cz, std::vector<float>& water, std::vector<float>& lava) const {
    // Поблочные грани флюидов к воздуху (без greedy — воды мало). Верх воды -0.125.
    static const float F[6][6][8] = {
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
        {{0,1,1, 0,1,0, 0,0},{1,1,1, 0,1,0, 1,0},{1,1,0, 0,1,0, 1,1},
         {1,1,0, 0,1,0, 1,1},{0,1,0, 0,1,0, 0,1},{0,1,1, 0,1,0, 0,0}},
    };
    static const int NB[6][3] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    water.clear(); lava.clear();
    for (int z = 0; z < 16; z++)
    for (int y = 0; y < Chunk::SY; y++)
    for (int x = 0; x < 16; x++) {
        int wx = cx * 16 + x, wz = cz * 16 + z;
        unsigned char id = getBlock(wx, y, wz);
        if (id != B_WATER && id != B_LAVA) continue;
        float tile = (id == B_WATER) ? 4.0f : 5.0f;
        float lvl = (float)getFlow(wx, y, wz) / 8.0f; // поверхность по уровню
        if (lvl <= 0.0f) lvl = 1.0f;
        std::vector<float>& out = (id == B_WATER) ? water : lava;
        for (int f = 0; f < 6; f++) {
            if (getBlock(wx + NB[f][0], y + NB[f][1], wz + NB[f][2]) != 0) continue;
            // Флуда нет: константы (страйд 12 тот же, ChunkMesh не трогаем).
            float fday = 15.0f, fnight = 0.0f;
            for (int v = 0; v < 6; v++) {
                float px = F[f][v][0] + x, pz = F[f][v][2] + z;
                float py = (F[f][v][1] > 0.5f) ? (float)y + lvl : (float)y + F[f][v][1];
                out.push_back(px); out.push_back(py); out.push_back(pz);
                out.push_back(F[f][v][3]); out.push_back(F[f][v][4]); out.push_back(F[f][v][5]);
                out.push_back(F[f][v][6]); out.push_back(F[f][v][7]);
                out.push_back(tile); out.push_back(3.0f);
                out.push_back(fday); out.push_back(fnight);
            }
        }
    }
}
