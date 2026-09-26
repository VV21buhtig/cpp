#ifndef TEXTURE_H
#define TEXTURE_H

// Engine: загрузка 2D-текстуры. Было loadTexture() в main.cpp, без изменений.
unsigned int loadTexture(const char* path);
// Атлас-массив 4x16x16 (tiles/grass_top,grass_side,dirt,stone), NEAREST. Нет файла = маджента.

unsigned int loadTileArray(const char* dir);

#endif
