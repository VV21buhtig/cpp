#ifndef STRUCT_PLACE_H
#define STRUCT_PLACE_H

// Общая математика плейсмента: детерминированный хэш (сид+соль+клетка),
// кандидат на spacing-сетке как в MC random_spread.
inline unsigned int cellHash(int seed, int salt, int x, int z) {
    unsigned int h = (unsigned int)(x * 31 + seed * 131 + salt);
    h ^= (unsigned int)(z * 57 + salt * 17 + 0x9e3779b9u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return h;
}
inline float cellRand(int seed, int salt, int x, int z) {
    return (float)(cellHash(seed, salt, x, z) & 0xffffff) / (float)0xffffff;
}

// Кандидат-чанк в клетке (region): true + локальный офсет ox,oz в чанках.
inline bool gridCandidate(int seed, int salt, int regionX, int regionZ,
                          int spacing, int separation, int* ox, int* oz) {
    int range = spacing - separation;
    if (range < 1) range = 1;
    unsigned int h = cellHash(seed, salt, regionX, regionZ);
    *ox = (int)((h >> 8) % (unsigned)range);
    *oz = (int)((h >> 20) % (unsigned)range);
    return true;
}

#endif
