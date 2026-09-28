#ifndef SAVE_H
#define SAVE_H

struct World;

// Engine: сейв мира в файл. Формат: "VXW2" + CX,CZ,SX,SY,SZ + сырые байты.
bool saveWorld(const World& w, const char* path);
bool loadWorld(World& w, const char* path);
// Только заголовок (для списка миров): размеры и сид без загрузки блоков.
// VXW2 сида не хранит — seed = -1.
bool readWorldInfo(const char* path, int& ncx, int& ncz, int& seed);

#endif
