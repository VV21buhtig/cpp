#ifndef PLAYER_H
#define PLAYER_H

#include <glm/glm.hpp>

struct World;

// Game: игрок. Fly (как было) + Walk с гравитацией и AABB-коллизией по осям.
struct Player {
    glm::vec3 pos;   // ноги (центр XZ, низ)
    glm::vec3 vel{0.0f};
    bool onGround = false;
    bool fly = true; // стартуем с fly чтобы не упасть в текстуры, F — переключить
    float halfW = 0.3f, height = 1.8f, eye = 1.62f;
    // плавный автошаг: подъём stepFromY -> stepToY за stepDur
    float stepT = 0.0f, stepDur = 0.12f, stepFromY = 0.0f, stepToY = 0.0f;

    void spawn(const World& w, int wx, int wz);
    // dt уже клампнут. keys: fwd/strafe в плоскости XZ, yaw-радианы, jump, down
    void update(float dt, const World& w, glm::vec2 move, float yaw,
                bool jump, bool down);
};

#endif
