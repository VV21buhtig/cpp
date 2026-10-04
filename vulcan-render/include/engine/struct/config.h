#ifndef STRUCT_CONFIG_H
#define STRUCT_CONFIG_H

// structures/: плейсмент структур в стиле MC structure_set random_spread.
// spacing/separation в чанках, salt декоррелирует типы, chance 0..1 на клетку.
struct PlacementConfig {
    int spacing = 2;    // размер клетки сетки
    int separation = 1; // поля: кандидат в [0, spacing-separation)
    int salt = 1337;
    float chance = 0.5f;
};

struct TreeConfig : PlacementConfig {
    int trunkBase = 4, trunkRand = 2; // высота = base + rand(0..trunkRand), как StraightTrunkPlacer
    float holeChance = 0.6f;          // дырки по углам кроны (0 густо — 1 лысо)
};

struct StructConfigs {
    TreeConfig tree{}; // chance правим ниже в structures.cfg
    TreeConfig pine{{3, 1, 4451, 0.5f}, 5, 1, 0.4f}; // сосна: реже дуба, ствол 5..6
    PlacementConfig mine{6, 2, 777, 0.9f};
    PlacementConfig rock{4, 2, 9182, 0.6f}; // валуны
    // structures.cfg рядом с бинарником (копируется при сборке, правится локально):
    //   tree 2 1 1337 0.5 4 2 0.6
    //   pine 3 1 4451 0.5 5 1 0.4
    //   rock 4 2 9182 0.6
    //   mine 6 2 777 0.9
    void load(const char* path);
};

#endif
