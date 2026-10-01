// mesh_vk: поквадратный мешер для Vulkan (без greedy — как L, 1 грань = 1 квад).
// Угловая математика AO — 0fps-пробы. Флуда нет (решение). Выход: u32-записи
// (x4+z4+y6+face3+tile6+ao8+flip1), топологию разворачивает VS.
#pragma once
#include <vector>
#include <cstdint>
struct World;
std::vector<uint32_t> buildChunkVK(const World& w, int cx, int cz);
