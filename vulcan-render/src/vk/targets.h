// targets: все картинки/буферы кадра (depth, HDR, lum/exp, тени, тайлы,
// gigabuffer+meta, вода, ридбэк, копия глубины). Порядок создания = порядок
// del (был в main), поведение не меняем.
#pragma once

#include "vk/vk_ctx.h"
#include <glm/glm.hpp>

struct World;
struct Targets;
constexpr int SHADOW_S = 2048; // теневая карта D16 (рецепт Ch10/11)

struct Targets {
    VkImage hdrImg = nullptr;
    VmaAllocation hdrAlloc = nullptr;
    VkImageView hdrView = nullptr;
    VkImageLayout hdrLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage hdrMsImg = nullptr; // demo-9: MSAA4 цвет (резолв в hdrView)
    VmaAllocation hdrMsAlloc = nullptr;
    VkImageView hdrMsView = nullptr;
    VkImage fsrImg = nullptr; // FSR2: выход апскейла (пост читает его вместо HDR)
    VmaAllocation fsrAlloc = nullptr;
    VkImageView fsrView = nullptr;
    VkImage depthMsImg = nullptr; // demo-9: MSAA4 глубина (резолв в depthCopyView)
    VmaAllocation depthMsAlloc = nullptr;
    VkImageView depthMsView = nullptr;
    VkImage lumImg = nullptr, expImg[2] = {nullptr, nullptr};
    VmaAllocation lumAlloc = nullptr, expAlloc[2] = {nullptr, nullptr};
    VkImageView lumView = nullptr, expView[2] = {nullptr, nullptr};
    VkSampler hdrSmp = nullptr, expSmp = nullptr;
    VkImage shadowImg = nullptr;
    VmaAllocation shadowAlloc = nullptr;
    VkImageView shadowView = nullptr;
    VkSampler shadowSmp = nullptr;
    VkSampler shadowRawSmp = nullptr; // рентген без compare (F1)
    VkImageLayout shadowLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage tileImg = nullptr;
    VmaAllocation tileAlloc = nullptr;
    VkImageView tileView = nullptr;
    VkSampler tileSmp = nullptr;
    VkBuffer gigaBuf = nullptr;
    VmaAllocation gigaAlloc = nullptr;
    VkBuffer metaBuf = nullptr;
    VmaAllocation metaAlloc = nullptr;
    VkBuffer visBuf = nullptr;
    VmaAllocation visAlloc = nullptr;
    VkBuffer indBuf = nullptr; // uint count + 64 x VkDrawIndirectCommand
    VmaAllocation indAlloc = nullptr;
    size_t totalQuads = 0;
    VkBuffer shotBuf = nullptr; // стейджинг под скриншот (--shot K -> shot.tga)
    VmaAllocation shotAlloc = nullptr;
    VkDeviceSize shotSize = 0;
    bool waterDbg = false; // отладка indirect (VK_WATERDBG=1)
    VkBuffer dbgReadBuf = nullptr;
    VmaAllocation dbgReadAlloc = nullptr;
    VkImage depthCopyImg = nullptr;
    VmaAllocation depthCopyAlloc = nullptr;
    VkImageView depthCopyView = nullptr; // demo-9: ещё и цель резолва глубины
    VkBuffer waterGigaBuf = nullptr, waterMetaBuf = nullptr, waterIndBuf = nullptr;
    VmaAllocation waterGigaAlloc = nullptr, waterMetaAlloc = nullptr, waterIndAlloc = nullptr;
    VkImage occImg = nullptr; // demo-8: воксели плотности под RT AO (R8 3D, статика до EditStore)
    VmaAllocation occAlloc = nullptr;
    VkImageView occView = nullptr;
    VkSampler occSmp = nullptr;
};

void makeTargets(VkCore& core, const World& world, const glm::vec3& worldOffset, Targets& t);
