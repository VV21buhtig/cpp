#ifndef ATLAS_H
#define ATLAS_H

// Engine: атлас 2x1 из двух картинок (ресайз до 256). tile0=первая, tile1=вторая.
// UV.x в меше уже поделены: (tile + uv)/2. S/T = CLAMP_TO_EDGE чтобы не текло.
unsigned int makeAtlas2(const char* pathA, const char* pathB);

#endif
