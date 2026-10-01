// mesh_vk: поквадратный мешер для Vulkan (без greedy — как L, 1 грань = 1 квад).
// Угловая математика AO — 0fps-пробы. Флуда нет (решение). Выход: 10 floats/вершина
// (pos3+nrm3+uv2+tile+ao), 6 вертексов на грань (2 триса, без индекса).
#pragma once
#include <vector>
struct World;
std::vector<float> buildChunkVK(const World& w, int cx, int cz);
