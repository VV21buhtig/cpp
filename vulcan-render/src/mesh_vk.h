// mesh_vk: поквадратный мешер для Vulkan (без greedy — как L, 1 грань = 1 квад).
// Угловая математика AO — 0fps-пробы. Флуда нет (решение). Выход: u32-записи
// (x4+z4+y6+face3+tile6+ao8+flip1), топологию разворачивает VS.
// Вода — отдельно (buildWaterVK): x4+z4+y6+face3+flow4+ao8, уровень из flow.
#pragma once
#include <vector>
#include <cstdint>
struct World;
std::vector<uint32_t> buildChunkVK(const World& w, int cx, int cz);
std::vector<uint32_t> buildWaterVK(const World& w, int cx, int cz);
