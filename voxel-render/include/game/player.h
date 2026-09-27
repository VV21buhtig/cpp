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
    float bodyH() const { return sneak ? 1.5f : height; } // MC: присед 1.5
    float eyeH() const { return sneak ? eye - 0.35f : eye; }
    float walkSpeed = 4.3f, flySpeed = 8.0f, jumpVel = 7.5f;
    bool stepOn = true;
    float stepH = 1.0f;
    bool autoJump = false; // bhop на зажатый пробел
    bool sneak = false;    // шифт: медленно + не падать с края
    // плавный автошаг: подъём stepFromY -> stepToY за stepDur
    float stepT = 0.0f, stepDur = 0.12f, stepFromY = 0.0f, stepToY = 0.0f;
    // прыжок только по нажатию (зажатый пробел не бхопит): edge + буфер 0.15с
    bool prevJumpHeld = false;
    float jumpBuf = 0.0f;

    void spawn(const World& w, int wx, int wz);
    // dt уже клампнут. keys: fwd/strafe в плоскости XZ, yaw-радианы, jump, down
    void update(float dt, const World& w, glm::vec2 move, float yaw,
                bool jump, bool down);
};

#endif
