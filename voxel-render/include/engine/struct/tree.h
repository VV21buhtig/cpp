#ifndef STRUCT_TREE_H
#define STRUCT_TREE_H

#include "engine/struct/config.h"

struct World;

// Дуб MC-стиль: ствол base+rand (id 5) + blob-крона (id 4), только на траве.
struct TreeConfig;
void stampTrees(World& w, const TreeConfig& cfg);

#endif
