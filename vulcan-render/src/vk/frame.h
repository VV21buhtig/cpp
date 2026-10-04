// frame: камера + синхра + цикл кадра. makeSync создаёт пул/буферы/семафоры
// (живут в FrameSync у main — del-лямбды их переживают), runFrameLoop крутит.
#pragma once

#include "vk/vk_ctx.h"
#include <glm/glm.hpp>

struct World;
struct Targets;
struct Sets;
struct Pipes;

struct FrameArgs {
    int maxFrames = -1;
    int shotFrame = -1; // --shot K: сохранить кадр K в shot.tga (свой рентген)
    bool camOverride = false;
    glm::vec3 camPosOvr{0.0f};
    float yawOvr = 0.0f, pitchOvr = 0.0f;
};

struct FrameSync {
    static const int FRAMES = 2;
    VkCommandPool cmdPool = nullptr;
    VkCommandBuffer cmdBufs[FRAMES] = {};
    VkSemaphore acquireSem[8] = {}, renderSem[8] = {};
    VkFence frameFence[2] = {};
    VkImageLayout imgLayout[8] = {};
    int nimgs = 0;
};

void makeSync(VkCore& core, FrameSync& sy);
int runFrameLoop(VkCore& core, World& world, const glm::vec3& worldOffset,
                 Targets& tg, Sets& st, Pipes& pp, FrameSync& sy, const FrameArgs& a);
