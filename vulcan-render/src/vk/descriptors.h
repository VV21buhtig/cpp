// descriptors: сеты кадра/куллинга/поста/блума/неба/SSAO + compute-пайпы
// (cull/lum/adapt/bloom/ssao) + bloom-цели + LUT-картинки. Порядок создания =
// порядок del (как был в main). Графические пайпы — в pipelines.
#pragma once

#include "vk/vk_ctx.h"

struct Targets;
struct Sets {
    VkDescriptorSetLayout setLayout = nullptr;
    VkDescriptorPool descPool = nullptr;
    VkDescriptorSet descSets[2] = {nullptr, nullptr};
    VkBuffer uboBuf[2] = {nullptr, nullptr};
    VmaAllocation uboAlloc[2] = {nullptr, nullptr};
    VkDescriptorSetLayout cullLayout = nullptr;
    VkDescriptorPool cullPool = nullptr;
    VkDescriptorSet cullSet = nullptr;
    VkPipelineLayout cullPipeLayout = nullptr;
    VkPipeline cullPipe = nullptr;
    VkImage bloomImg[3] = {nullptr, nullptr, nullptr};
    VmaAllocation bloomAlloc[3] = {nullptr, nullptr, nullptr};
    VkImageView bloomView[3] = {nullptr, nullptr, nullptr};
    VkSampler bloomSmp = nullptr;
    VkDescriptorSetLayout postLayout = nullptr;
    VkDescriptorPool postPool = nullptr;
    VkDescriptorSet postSet[2] = {nullptr, nullptr};
    VkPipelineLayout postComputeLayout = nullptr; // lum+adapt делят (push dt/parity)
    VkPipeline lumPipe = nullptr, adaptPipe = nullptr;
    VkDescriptorSetLayout bloomLayout = nullptr;
    VkDescriptorPool bloomPool = nullptr;
    VkDescriptorSet bloomSet[3];
    VkPipelineLayout bloomPipeLayout = nullptr;
    VkPipeline brightPipe = nullptr, kdownPipe = nullptr, kupPipe = nullptr;
    VkDescriptorSetLayout skyLayout = nullptr;
    VkDescriptorPool skyPool = nullptr;
    VkDescriptorSet skySets[2] = {nullptr, nullptr};
    VkImage skyTImg = nullptr, skyMImg = nullptr;
    VmaAllocation skyTAlloc = nullptr, skyMAlloc = nullptr;
    VkImageView skyTView = nullptr, skyMView = nullptr;
    VkSampler skySmp = nullptr;
    VkImage ssaoImg = nullptr;
    VmaAllocation ssaoAlloc = nullptr;
    VkImageView ssaoView = nullptr;
    VkDescriptorSetLayout ssaoLayout = nullptr;
    VkDescriptorPool ssaoPool = nullptr;
    VkDescriptorSet ssaoSet = nullptr;
    VkPipelineLayout ssaoPipeLayout = nullptr;
    VkPipeline ssaoPipe = nullptr;
};

void makeSets(VkCore& core, Targets& tg, Sets& s);
