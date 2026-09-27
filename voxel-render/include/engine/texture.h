#ifndef TEXTURE_H
#define TEXTURE_H

// Engine: загрузка 2D-текстуры. Было loadTexture() в main.cpp, без изменений.
unsigned int loadTexture(const char* path);
// Атлас-массив 4x16x16 (tiles/grass_top,grass_side,dirt,stone), NEAREST. Нет файла = маджента.

unsigned int loadTileArray(const char* dir);
void setTileArrayFilter(unsigned int tex, int mode);
// Режим сэмплирования tile array: 0 Nearest 1 Bilinear 2 Trilinear 3 Aniso 8x.
// Можно дёргать в рантайме (паки, консоль, меню).

#endif
