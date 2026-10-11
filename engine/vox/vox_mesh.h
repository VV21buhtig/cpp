#ifndef VOX_MESH_H
#define VOX_MESH_H
// Greedy-мешинг чанка (их алгоритм, C11): 6 направлений, маска id+AO,
// мерж по ширине/высоте, flip по диагонали AO. Вершина 12 float:
// pos3 norm3 uv2 tile ao day night (страйд их lighting.vs 1:1).
// Соседи [6] для AO/граней на границе (±x,±y,±z), NULL = воздух.
#include "vox_chunk.h"
#include <stddef.h>

typedef struct {
    float *v;
    size_t n, cap;
} VoxMeshOut;

void vox_mesh_build(const VoxChunk *c, const VoxChunk *nb[6], VoxMeshOut *out);
void vox_mesh_free(VoxMeshOut *out);
#endif
