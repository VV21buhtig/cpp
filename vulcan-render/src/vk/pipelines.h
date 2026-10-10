// pipelines: графические пайпы (террейн, тень, вода, рентген, небо, тонемэпп).
// Compute-пайпы живут в descriptors (рядом со своими сетами).
#pragma once

#include "vk/vk_ctx.h"

struct Sets;
struct Pipes {
    VkPipelineLayout pipeLayout = nullptr;
    VkPipeline pipeline = nullptr;
    VkPipeline shadowPipe = nullptr;
    VkPipeline waterPipe = nullptr;
    VkPipeline dbgPipe = nullptr;
    VkPipelineLayout skyPipeLayout = nullptr, tonemapPipeLayout = nullptr;
    VkPipeline skyPipe = nullptr, tonemapPipe = nullptr;
};

void makePipes(VkCore& core, Sets& st, Pipes& p);
