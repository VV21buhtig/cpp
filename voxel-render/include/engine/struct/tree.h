#ifndef STRUCT_TREE_H
#define STRUCT_TREE_H

#include "engine/struct/config.h"

struct World;

// Дуб MC-стиль: прямой ствол 4-6 (id 5) + blob-крона (id 4), только на траве.
void stampTrees(World& w, const PlacementConfig& cfg);

#endif
