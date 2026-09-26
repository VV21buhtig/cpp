#ifndef PICK_H
#define PICK_H

#include <glm/glm.hpp>

class Chunk;

// Game: матрица куба + Ray-AABB выбор (было в main.cpp, глава 23). Без изменений.
glm::mat4 cubeModelMatrix(glm::vec3 pos, unsigned int index);
float rayAABBLocal(glm::vec3 origin, glm::vec3 dir);
int pickCube(glm::vec3 rayOrigin, glm::vec3 rayDir,
             const glm::vec3* positions, int count, float maxDist);

// Game: пик по вокселям чанка. Возвращает t до блока или -1; bx,by,bz — координаты блока.
float pickChunkBlock(glm::vec3 rayOrigin, glm::vec3 rayDir,
                     const Chunk& chunk, glm::vec3 chunkOffset,
                     float maxDist, int& bx, int& by, int& bz);

#endif
