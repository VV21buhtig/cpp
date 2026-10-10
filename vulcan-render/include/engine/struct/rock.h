#ifndef STRUCT_ROCK_H
#define STRUCT_ROCK_H

#include "engine/struct/config.h"

struct World;

// Валун: 2 камня рядом + 1 сверху, полузакопан. На любом solid-верхе выше моря
// (и в пустыне тоже). В воду/на деревья не ставится.
void stampRocks(World& w, const PlacementConfig& cfg);

#endif
