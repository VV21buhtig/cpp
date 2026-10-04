// targets: см. vk/targets.h. Порядок создания = порядок del (как был в main).
#include "vk/targets.h"
#include "engine/world.h"
#include "mesh_vk.h"
#include <stb/stb_image.h>
#include <cstdio>
#include <cstring>

void makeTargets(VkCore& core, const World& world, const glm::vec3& worldOffset, Targets& t) {
    // ---- demo-5a HDR-цель (R16F, сцена+небо пишут, читают lum/tonemap) ----
    t.hdrImg = nullptr;
    t.hdrAlloc = nullptr;
    t.hdrView = nullptr;
    t.hdrLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.extent = {core.swapExtent.width, core.swapExtent.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT; // demo-7: TAA пишет историю назад в HDR
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.hdrImg, &t.hdrAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.hdrImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.hdrView));
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroyImageView(corep->device, tp->hdrView, nullptr);
        vmaDestroyImage(corep->alloc, tp->hdrImg, tp->hdrAlloc);
    });

    // ---- demo-9 MSAA4 цели (DONT_CARE: всё уходит резолвом) ----
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.extent = {core.swapExtent.width, core.swapExtent.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_4_BIT;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
        if (vmaCreateImage(core.alloc, &ci, &ai, &t.hdrMsImg, &t.hdrMsAlloc, nullptr) != VK_SUCCESS) {
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // нет lazy — обычная
            VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.hdrMsImg, &t.hdrMsAlloc, nullptr));
        }
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.hdrMsImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.hdrMsView));
        ci.format = VK_FORMAT_D32_SFLOAT;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        ai.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
        if (vmaCreateImage(core.alloc, &ci, &ai, &t.depthMsImg, &t.depthMsAlloc, nullptr) != VK_SUCCESS) {
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.depthMsImg, &t.depthMsAlloc, nullptr));
        }
        vi.image = t.depthMsImg;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.depthMsView));
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroyImageView(corep->device, tp->depthMsView, nullptr);
        vmaDestroyImage(corep->alloc, tp->depthMsImg, tp->depthMsAlloc);
        vkDestroyImageView(corep->device, tp->hdrMsView, nullptr);
        vmaDestroyImage(corep->alloc, tp->hdrMsImg, tp->hdrMsAlloc);
    });

    // ---- demo-5a lum 64x36 + exposure ping-pong 1x1 (GENERAL навсегда) ----
    t.lumImg = nullptr;
    t.lumAlloc = nullptr;
    t.lumView = nullptr;
    t.hdrSmp = nullptr, t.expSmp = nullptr;
    {
        auto mkTarget = [&](uint32_t w, uint32_t h, VkImage& img, VmaAllocation& al,
                            VkImageView& view, bool clear1) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R16_SFLOAT;
            ci.extent = {w, h, 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &img, &al, nullptr));
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16_SFLOAT;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &view));
            core.immRun([&](VkCommandBuffer cb) {
                imgBarrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                           0, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                if (clear1) {
                    VkClearColorValue cv{};
                    cv.float32[0] = 1.0f; // exposure стартует с 1.0 (рецепт книги)
                    VkImageSubresourceRange rg{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &rg);
                }
                imgBarrier(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            });
        };
        mkTarget(64, 36, t.lumImg, t.lumAlloc, t.lumView, false);
        mkTarget(1, 1, t.expImg[0], t.expAlloc[0], t.expView[0], true);
        mkTarget(1, 1, t.expImg[1], t.expAlloc[1], t.expView[1], true);
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &t.hdrSmp));
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &t.expSmp));
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroySampler(corep->device, tp->hdrSmp, nullptr);
        vkDestroySampler(corep->device, tp->expSmp, nullptr);
        vkDestroyImageView(corep->device, tp->lumView, nullptr);
        vmaDestroyImage(corep->alloc, tp->lumImg, tp->lumAlloc);
        for (int i = 0; i < 2; i++) {
            vkDestroyImageView(corep->device, tp->expView[i], nullptr);
            vmaDestroyImage(corep->alloc, tp->expImg[i], tp->expAlloc[i]);
        }
    });

    // ---- demo-4 теневая карта: D16 2048 (рецепт Ch10/11) + compare-сэмплер ----
    t.shadowImg = nullptr;
    t.shadowAlloc = nullptr;
    t.shadowView = nullptr;
    t.shadowSmp = nullptr;
    t.shadowRawSmp = nullptr; // рентген без compare (F1)
    t.shadowLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_D16_UNORM;
        ci.extent = {(uint32_t)SHADOW_S, (uint32_t)SHADOW_S, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.shadowImg, &t.shadowAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.shadowImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D16_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.shadowView));
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR; // железный 2x2 PCF на тап (рецепт книги)
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.compareEnable = VK_TRUE; // sampler2DShadow: сравнение LESS_OR_EQUAL
        si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        si.minLod = 0.0f; si.maxLod = 0.0f;
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &t.shadowSmp));
        si.compareEnable = VK_FALSE; // сырая глубина для рентгена
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &t.shadowRawSmp));
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroySampler(corep->device, tp->shadowRawSmp, nullptr);
        vkDestroySampler(corep->device, tp->shadowSmp, nullptr);
        vkDestroyImageView(corep->device, tp->shadowView, nullptr);
        vmaDestroyImage(corep->alloc, tp->shadowImg, tp->shadowAlloc);
    });

    // ---- texture array 13x16x16 + 5 мипов блитами ----
    {
        const char* names[14] = {"grass_top.png", "grass_side.png", "dirt.png", "stone.png",
            "water.png", "lava.png", "leaves.png", "log_side.png", "log_top.png",
            "ore_coal.png", "ore_iron.png", "ore_gold.png", "ore_diamond.png", "sand.png"};
        const int T = 16, NL = 14, MIPS = 5;
        std::vector<unsigned char> all(T * T * 4 * NL);
        stbi_set_flip_vertically_on_load(true);
        for (int i = 0; i < NL; i++) {
            char path[1024];
            snprintf(path, sizeof(path), "assets/tiles/%s", names[i]);
            int w, h, ch;
            unsigned char* d = stbi_load(path, &w, &h, &ch, 4);
            if (!d || w != T || h != T) { printf("tile bad %s\n", path); exit(1); }
            memcpy(&all[i * T * T * 4], d, T * T * 4);
            stbi_image_free(d);
        }
        // Ванильные grass_top/листва/вода — ч/б + биомный тинт (как в MC).
        // Печём plains-тинты сразу: трава #91BD59, листва #77AB2F, вода #3F76E4.
        for (int p = 0; p < T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 145 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 189 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 89 / 255);
        }
        for (int p = 6 * T * T; p < 7 * T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 119 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 171 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 47 / 255);
        }
        // Вода глубокая (замер 0.54R: белила): тинт #2050A0 вместо #3F76E4.
        for (int p = 4 * T * T; p < 5 * T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 32 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 80 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 160 / 255);
        }
        // Песок ванильно бледный (mean 0.83) — на нашем солнце выгорает в белое.
        // Тёплый тинт (0.87, 0.78, 0.56): и пляж, и пустыня читаются песком.
        for (int p = 13 * T * T; p < 14 * T * T; p++) {
            all[p * 4 + 0] = (unsigned char)(all[p * 4 + 0] * 222 / 255);
            all[p * 4 + 1] = (unsigned char)(all[p * 4 + 1] * 200 / 255);
            all[p * 4 + 2] = (unsigned char)(all[p * 4 + 2] * 144 / 255);
        }
        VkDeviceSize upSize = all.size();
        VkBuffer staging;
        VmaAllocation stagingAlloc;
        {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = upSize;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
            void* dst = nullptr;
            VK_CHECK(vmaMapMemory(core.alloc, stagingAlloc, &dst));
            memcpy(dst, all.data(), all.size());
            vmaUnmapMemory(core.alloc, stagingAlloc);
        }
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {(uint32_t)T, (uint32_t)T, 1};
        ci.mipLevels = MIPS; ci.arrayLayers = NL;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.tileImg, &t.tileAlloc, nullptr));
        core.immRun([&](VkCommandBuffer cb) {
            imgBarrier(cb, t.tileImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, MIPS,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy cp{};
            cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, NL};
            cp.imageExtent = {(uint32_t)T, (uint32_t)T, 1};
            vkCmdCopyBufferToImage(cb, staging, t.tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
            // мипы блитами 16->8->4->2->1 (каждый уровень: DST->SRC, blit, SRC->SHADER)
            for (int m = 1; m < MIPS; m++) {
                imgBarrier(cb, t.tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                VkImageBlit bl{};
                bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(m - 1), 0, NL};
                bl.srcOffsets[1] = {T >> (m - 1), T >> (m - 1), 1};
                bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)m, 0, NL};
                bl.dstOffsets[1] = {T >> m, T >> m, 1};
                vkCmdBlitImage(cb, t.tileImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               t.tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
                imgBarrier(cb, t.tileImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            }
            imgBarrier(cb, t.tileImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, MIPS - 1, 1,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        });
        vmaDestroyBuffer(core.alloc, staging, stagingAlloc);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.tileImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, MIPS, 0, NL};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.tileView));
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = core.maxAniso < 8.0f ? core.maxAniso : 8.0f;
        si.maxLod = (float)(MIPS - 1);
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &t.tileSmp));
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroySampler(corep->device, tp->tileSmp, nullptr);
        vkDestroyImageView(corep->device, tp->tileView, nullptr);
        vmaDestroyImage(corep->alloc, tp->tileImg, tp->tileAlloc);
    });

    // ---- gigabuffer квадов (всё в одном SSBO) + meta чанков ----
    // demo-3c: compute-cull читает meta, пишет vis + indirect; VS тянет квады
    // по firstInstance+instance, чанк — по gl_DrawID из vis[].
    struct ChunkMeta { uint32_t quadOff, quadCount; float ox, oz; };
    t.gigaBuf = nullptr;
    t.gigaAlloc = nullptr;
    t.metaBuf = nullptr;
    t.metaAlloc = nullptr;
    t.visBuf = nullptr;
    t.visAlloc = nullptr;
    t.indBuf = nullptr; // uint count + 64 x VkDrawIndirectCommand
    t.indAlloc = nullptr;
    t.totalQuads = 0;
    {
        std::vector<uint32_t> all;
        std::vector<ChunkMeta> metas;
        for (int cz = 0; cz < 8; cz++)
            for (int cx = 0; cx < 8; cx++) {
                std::vector<uint32_t> data = buildChunkVK(world, cx, cz);
                if (data.size() >= (1u << 20)) { printf("chunk too big for QUADBIAS\n"); exit(1); }
                ChunkMeta m{(uint32_t)all.size(), (uint32_t)data.size(),
                            worldOffset.x + cx * 16.0f, worldOffset.z + cz * 16.0f};
                metas.push_back(m);
                all.insert(all.end(), data.begin(), data.end());
            }
        t.totalQuads = all.size();
        printf("gigaquads total %zu\n", t.totalQuads);
        auto upload = [&](const void* src, VkDeviceSize sz, VkBufferUsageFlags use,
                          VkBuffer& out, VmaAllocation& oa) {
            VkBuffer staging;
            VmaAllocation stagingAlloc;
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = sz ? sz : 16;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
            if (sz) {
                void* dst = nullptr;
                VK_CHECK(vmaMapMemory(core.alloc, stagingAlloc, &dst));
                memcpy(dst, src, sz);
                vmaUnmapMemory(core.alloc, stagingAlloc);
            }
            bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | use;
            ai.flags = 0;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &out, &oa, nullptr));
            core.immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = sz ? sz : 16;
                vkCmdCopyBuffer(cb, staging, out, 1, &cp);
            });
            vmaDestroyBuffer(core.alloc, staging, stagingAlloc);
        };
        upload(all.data(), all.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, t.gigaBuf, t.gigaAlloc);
        upload(metas.data(), metas.size() * sizeof(ChunkMeta),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, t.metaBuf, t.metaAlloc);
        std::vector<uint32_t> zero(64, 0);
        upload(zero.data(), zero.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               t.visBuf, t.visAlloc); // +SRC для DEBUG-ридбэка
        std::vector<uint8_t> izero(16 + 64 * sizeof(VkDrawIndirectCommand), 0);
        upload(izero.data(), izero.size(),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               t.indBuf, t.indAlloc);
    }
    core.del.push([corep = &core, tp = &t]() {
        vmaDestroyBuffer(corep->alloc, tp->gigaBuf, tp->gigaAlloc);
        vmaDestroyBuffer(corep->alloc, tp->metaBuf, tp->metaAlloc);
        vmaDestroyBuffer(corep->alloc, tp->visBuf, tp->visAlloc);
        vmaDestroyBuffer(corep->alloc, tp->indBuf, tp->indAlloc);
    });

    // ---- demo-5w2 ридбэк: стейджинг под скриншот (--shot K -> shot.tga) ----
    t.shotBuf = nullptr;
    t.shotAlloc = nullptr;
    t.shotSize = (VkDeviceSize)core.swapExtent.width * core.swapExtent.height * 4;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = t.shotSize;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &t.shotBuf, &t.shotAlloc, nullptr));
    }
    core.del.push([corep = &core, tp = &t]() { vmaDestroyBuffer(corep->alloc, tp->shotBuf, tp->shotAlloc); });

    // ---- отладка indirect: маленький host-буфер для чтения счётчиков (VK_WATERDBG=1) ----
    t.waterDbg = getenv("VK_WATERDBG") != nullptr;
    t.dbgReadBuf = nullptr;
    t.dbgReadAlloc = nullptr;
    if (t.waterDbg) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = 2048;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &t.dbgReadBuf, &t.dbgReadAlloc, nullptr));
        core.del.push([corep = &core, tp = &t]() { vmaDestroyBuffer(corep->alloc, tp->dbgReadBuf, tp->dbgReadAlloc); });
    }

    // ---- demo-5x копия глубины для воды (фидбэк-луп запрещён: читать ту же
    // картинку что пишешь нельзя — копируем после террейна, вода читает копию).
    t.depthCopyImg = nullptr;
    t.depthCopyAlloc = nullptr;
    t.depthCopyView = nullptr;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_D32_SFLOAT;
        ci.extent = {core.swapExtent.width, core.swapExtent.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; // demo-9: цель резолва
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.depthCopyImg, &t.depthCopyAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.depthCopyImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.depthCopyView));
        core.immRun([&](VkCommandBuffer cb) {
            imgBarrier(cb, t.depthCopyImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        });
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroyImageView(corep->device, tp->depthCopyView, nullptr);
        vmaDestroyImage(corep->alloc, tp->depthCopyImg, tp->depthCopyAlloc);
    });
    struct WaterMeta { uint32_t quadOff, quadCount; float ox, oz; };
    t.waterGigaBuf = nullptr, t.waterMetaBuf = nullptr, t.waterIndBuf = nullptr;
    t.waterGigaAlloc = nullptr, t.waterMetaAlloc = nullptr, t.waterIndAlloc = nullptr;
    {
        std::vector<uint32_t> all;
        std::vector<WaterMeta> metas;
        for (int cz = 0; cz < 8; cz++)
            for (int cx = 0; cx < 8; cx++) {
                std::vector<uint32_t> data = buildWaterVK(world, cx, cz);
                if (data.size() >= (1u << 20)) { printf("water chunk too big\n"); exit(1); }
                WaterMeta m{(uint32_t)all.size(), (uint32_t)data.size(),
                            worldOffset.x + cx * 16.0f, worldOffset.z + cz * 16.0f};
                metas.push_back(m);
                all.insert(all.end(), data.begin(), data.end());
            }
        printf("water quads total %zu\n", all.size());
        auto upload = [&](const void* src, VkDeviceSize sz, VkBufferUsageFlags use,
                          VkBuffer& out, VmaAllocation& oa) {
            VkBuffer staging;
            VmaAllocation stagingAlloc;
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = sz ? sz : 16;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &staging, &stagingAlloc, nullptr));
            if (sz) {
                void* dst = nullptr;
                VK_CHECK(vmaMapMemory(core.alloc, stagingAlloc, &dst));
                memcpy(dst, src, sz);
                vmaUnmapMemory(core.alloc, stagingAlloc);
            }
            bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | use;
            ai.flags = 0;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &ai, &out, &oa, nullptr));
            core.immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = sz ? sz : 16;
                vkCmdCopyBuffer(cb, staging, out, 1, &cp);
            });
            vmaDestroyBuffer(core.alloc, staging, stagingAlloc);
        };
        upload(all.data(), all.size() * sizeof(uint32_t),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, t.waterGigaBuf, t.waterGigaAlloc);
        upload(metas.data(), metas.size() * sizeof(WaterMeta),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               t.waterMetaBuf, t.waterMetaAlloc);
        std::vector<uint8_t> izero(16 + 64 * sizeof(VkDrawIndirectCommand), 0);
        upload(izero.data(), izero.size(),
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               t.waterIndBuf, t.waterIndAlloc);
    }
    core.del.push([corep = &core, tp = &t]() {
        vmaDestroyBuffer(corep->alloc, tp->waterGigaBuf, tp->waterGigaAlloc);
        vmaDestroyBuffer(corep->alloc, tp->waterMetaBuf, tp->waterMetaAlloc);
        vmaDestroyBuffer(corep->alloc, tp->waterIndBuf, tp->waterIndAlloc);
    });

    // ---- demo-8 объём плотности: solid=255, листва=128, иначе 0 (R8 3D) ----
    // Статика на весь запуск (перестройка — с EditStore в игровой фазе).
    {
        int W = world.sizeX(), D = world.sizeZ(), H = Chunk::SY;
        std::vector<unsigned char> vox((size_t)W * H * D, 0);
        for (int z = 0; z < D; z++)
            for (int x = 0; x < W; x++)
                for (int y = 0; y < H; y++) {
                    unsigned char b = world.getBlock(x, y, z);
                    if (!World::isSolid(b)) continue; // воздух/вода/лава не затеняют
                    vox[((size_t)z * H + y) * W + x] =
                        (b == B_LEAVES) ? 128 : 255;
                }
        VkBuffer stg;
        VmaAllocation stgAlloc;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = vox.size();
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &aci, &stg, &stgAlloc, nullptr));
        void* dst = nullptr;
        VK_CHECK(vmaMapMemory(core.alloc, stgAlloc, &dst));
        memcpy(dst, vox.data(), vox.size());
        vmaUnmapMemory(core.alloc, stgAlloc);
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_3D;
        ci.format = VK_FORMAT_R8_UNORM;
        ci.extent = {(uint32_t)W, (uint32_t)H, (uint32_t)D};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &t.occImg, &t.occAlloc, nullptr));
        core.immRun([&](VkCommandBuffer cb) {
            imgBarrier(cb, t.occImg, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy cp{};
            cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            cp.imageExtent = {(uint32_t)W, (uint32_t)H, (uint32_t)D};
            vkCmdCopyBufferToImage(cb, stg, t.occImg,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
            imgBarrier(cb, t.occImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        });
        vmaDestroyBuffer(core.alloc, stg, stgAlloc);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.occImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
        vi.format = VK_FORMAT_R8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &t.occView));
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK; // вне мира пусто
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &t.occSmp));
    }
    core.del.push([corep = &core, tp = &t]() {
        vkDestroySampler(corep->device, tp->occSmp, nullptr);
        vkDestroyImageView(corep->device, tp->occView, nullptr);
        vmaDestroyImage(corep->alloc, tp->occImg, tp->occAlloc);
    });
}
