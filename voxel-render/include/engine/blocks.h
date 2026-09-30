#ifndef BLOCKS_H
#define BLOCKS_H

// Engine: реестр блоков из blocks.json (data-driven вместо хардкода ids).
// ids ЗАМОРОЖЕНЫ (0..12 с дыркой 8) — их хранят сейвы VXW. JSON меняет только
// свойства: тайлы, solid/fluid/cutout, звуки, шаги. Нет файла — built-in defaults.
enum BlockId : unsigned char {
    B_AIR = 0, B_GRASS = 1, B_DIRT = 2, B_STONE = 3, B_LEAVES = 4, B_LOG = 5,
    B_WATER = 6, B_LAVA = 7, B_COAL = 9, B_IRON = 10, B_GOLD = 11, B_DIAMOND = 12
};

enum class BlockSnd : unsigned char {
    Dig = 0,    // земля/трава (dig.wav)
    Wood = 1,   // бревно (dig_wood.wav)
    Leaves = 2, // листва (dig_leaves.wav)
    Stone = 3,  // камень/руды (dig_stone.wav)
    Generic = 4 // только для place: нейтральный place.wav
};

struct BlockDef {
    unsigned char id = 0;
    char name[32] = {0};
    int tileTop = 0, tileSide = 0, tileBottom = 0; // слои texture array
    bool solid = false;   // коллизии, пик, мешинг
    bool fluid = false;   // вода/лава (особый тик и рендер)
    bool cutout = false;  // листва: не закрывает чужие грани
    BlockSnd brk = BlockSnd::Dig;
    BlockSnd plc = BlockSnd::Generic;
    int stepSurf = 0; // 0 трава 1 камень (см. playStep)
};

// Слот инвентаря (survival): id блока + штуки. Пусто = id 0. Сейвится? Пока нет (сессия).
struct InvSlot {
    unsigned char id = 0;
    int n = 0;
};

struct BlockRegistry {
    BlockDef defs[256];
    bool loaded = false; // true если blocks.json разобран
    int count = 0;
    BlockRegistry(); // всегда заполняет defaults, даже без файла
    bool load(const char* path); // false — остались defaults
    const BlockDef& get(unsigned char id) const { return defs[id]; }
};

extern BlockRegistry gBlocks;

#endif
