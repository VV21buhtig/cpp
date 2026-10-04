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
    VkImage depthImg = nullptr;
    VmaAllocation depthAlloc = nullptr;
    VkImageView depthView = nullptr;
    VkImage hdrImg = nullptr;
    VmaAllocation hdrAlloc = nullptr;
    VkImageView hdrView = nullptr;
    VkImageLayout hdrLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
    VkImageView depthCopyView = nullptr;
    VkBuffer waterGigaBuf = nullptr, waterMetaBuf = nullptr, waterIndBuf = nullptr;
    VmaAllocation waterGigaAlloc = nullptr, waterMetaAlloc = nullptr, waterIndAlloc = nullptr;
};

void makeTargets(VkCore& core, const World& world, const glm::vec3& worldOffset, Targets& t);
