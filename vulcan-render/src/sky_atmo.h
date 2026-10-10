// demo-5c: CPU-bake атмосферных LUT по математике Kaigen (wc_atmo.glsl).
// Один в один: константы, плотности, фазы, 40 шагов T, 8x8x20 шагов MS.
#pragma once
#include <vector>

namespace sky {

// Размеры LUT (меньше Kaigen 256x64/32x32 не надо — те же).
inline constexpr int TRANS_W = 256;
inline constexpr int TRANS_H = 64;
inline constexpr int MS_W = 32;
inline constexpr int MS_H = 32;

// RGBA32F построчно: bakeTransmittance()[ (y*W+x)*4 + c ].
std::vector<float> bakeTransmittance();
std::vector<float> bakeMultiscatter();

} // namespace sky
