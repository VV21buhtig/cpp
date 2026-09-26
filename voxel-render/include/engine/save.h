#ifndef SAVE_H
#define SAVE_H

struct World;

// Engine: сейв мира в файл. Формат: "VXW2" + CX,CZ,SX,SY,SZ + сырые байты.
bool saveWorld(const World& w, const char* path);
bool loadWorld(World& w, const char* path);

#endif
