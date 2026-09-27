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

struct StructConfigs {
    PlacementConfig tree{2, 1, 1337, 0.5f};
    PlacementConfig mine{6, 2, 777, 0.9f};
    // structures.cfg рядом с бинарником (копируется при сборке, правится локально):
    //   tree 2 1 1337 0.5
    //   mine 6 2 777 0.9
    void load(const char* path);
};

#endif
