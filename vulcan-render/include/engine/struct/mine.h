#ifndef STRUCT_MINE_H
#define STRUCT_MINE_H

#include "engine/struct/config.h"

struct World;

// Шахта v1: пьяный червь 2 высотой (без рельс/факелов — их блоков пока нет).
void stampMines(World& w, const PlacementConfig& cfg);

#endif
