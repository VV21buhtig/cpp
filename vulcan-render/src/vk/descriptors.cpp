// descriptors: см. vk/descriptors.h. Порядок создания = порядок del (как был в main).
#include "vk/descriptors.h"
#include "vk/targets.h"
#include "engine/world.h"
#include "sky_atmo.h"
#include <cstdio>
#include <cstring>

void makeSets(VkCore& core, Targets& tg, Sets& s) {
    // ---- UBO кадра x2 + дескрипторы (1 набор на кадр: UBO свой, остальное общее) ----
    {
        VkDescriptorSetLayoutBinding b0{};
        b0.binding = 0;
        b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b0.descriptorCount = 1;
        b0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b1{};
        b1.binding = 1;
        b1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b1.descriptorCount = 1;
        b1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b2{}; // 2=gigabuffer квадов
        b2.binding = 2;
        b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b2.descriptorCount = 1;
        b2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b3{}; // 3=meta чанков (origin/offset)
        b3.binding = 3;
        b3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b3.descriptorCount = 1;
        b3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b4{}; // 4=vis-список (compute пишет, VS читает)
        b4.binding = 4;
        b4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b4.descriptorCount = 1;
        b4.stageFlags = (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                              VK_SHADER_STAGE_COMPUTE_BIT);
        VkDescriptorSetLayoutBinding b5{}; // 5=теневая карта (compare-сэмплер)
        b5.binding = 5;
        b5.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b5.descriptorCount = 1;
        b5.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b6{}; // 6=тень сырьём для рентгена (F1)
        b6.binding = 6;
        b6.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b6.descriptorCount = 1;
        b6.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b7{}; // 7=вода gigabuffer (demo-5w)
        b7.binding = 7;
        b7.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b7.descriptorCount = 1;
        b7.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b8{}; // 8=вода meta (demo-5w)
        b8.binding = 8;
        b8.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b8.descriptorCount = 1;
        b8.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutBinding b9{}; // 9=глубина сцены для воды (поглощение)
        b9.binding = 9;
        b9.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b9.descriptorCount = 1;
        b9.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding b10{}; // 10=объём плотности для RT AO
        b10.binding = 10;
        b10.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b10.descriptorCount = 1;
        b10.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding bs[11] = {b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 11; li.pBindings = bs;
        VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.setLayout));
        VkDescriptorPoolSize ps[3]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[0].descriptorCount = 2;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 2 * 5;
        ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps[2].descriptorCount = 2 * 5;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 2;
        pi.poolSizeCount = 3; pi.pPoolSizes = ps;
        VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.descPool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = s.descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &s.setLayout;
        for (int i = 0; i < 2; i++) {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = sizeof(FrameUBO);
            bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            VmaAllocationCreateInfo aci{};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &aci, &s.uboBuf[i], &s.uboAlloc[i], nullptr));
            VK_CHECK(vkAllocateDescriptorSets(core.device, &ai, &s.descSets[i]));
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = s.uboBuf[i]; dbi.range = sizeof(FrameUBO);
            VkDescriptorImageInfo dii{};
            dii.sampler = tg.tileSmp; dii.imageView = tg.tileView;
            dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorBufferInfo sbi[3]{};
            sbi[0].buffer = tg.gigaBuf; sbi[0].range = VK_WHOLE_SIZE;
            sbi[1].buffer = tg.metaBuf; sbi[1].range = VK_WHOLE_SIZE;
            sbi[2].buffer = tg.visBuf; sbi[2].range = VK_WHOLE_SIZE;
            VkDescriptorBufferInfo wbi[2]{};
            wbi[0].buffer = tg.waterGigaBuf; wbi[0].range = VK_WHOLE_SIZE;
            wbi[1].buffer = tg.waterMetaBuf; wbi[1].range = VK_WHOLE_SIZE;
            VkDescriptorImageInfo shdi{};
            shdi.sampler = tg.shadowSmp; shdi.imageView = tg.shadowView;
            shdi.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet w[7]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = s.descSets[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[0].pBufferInfo = &dbi;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = s.descSets[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].pImageInfo = &dii;
            for (int b = 2; b < 5; b++) {
                w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[b].dstSet = s.descSets[i]; w[b].dstBinding = (uint32_t)b;
                w[b].descriptorCount = 1;
                w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[b].pBufferInfo = &sbi[b - 2];
            }
            w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[5].dstSet = s.descSets[i]; w[5].dstBinding = 5;
            w[5].descriptorCount = 1;
            w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[5].pImageInfo = &shdi;
            VkDescriptorImageInfo shraw{};
            shraw.sampler = tg.shadowRawSmp; shraw.imageView = tg.shadowView;
            shraw.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            w[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[6].dstSet = s.descSets[i]; w[6].dstBinding = 6;
            w[6].descriptorCount = 1;
            w[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[6].pImageInfo = &shraw;
            // дописываем воду 7,8 + глубину 9 отдельным апдейтом:
            VkWriteDescriptorSet wx[3]{};
            wx[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wx[0].dstSet = s.descSets[i]; wx[0].dstBinding = 7;
            wx[0].descriptorCount = 1;
            wx[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wx[0].pBufferInfo = &wbi[0];
            wx[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wx[1].dstSet = s.descSets[i]; wx[1].dstBinding = 8;
            wx[1].descriptorCount = 1;
            wx[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wx[1].pBufferInfo = &wbi[1];
            VkDescriptorImageInfo ddi{};
            ddi.sampler = tg.shadowRawSmp; ddi.imageView = tg.depthCopyView; // копия!
            ddi.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            wx[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wx[2].dstSet = s.descSets[i]; wx[2].dstBinding = 9;
            wx[2].descriptorCount = 1;
            wx[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wx[2].pImageInfo = &ddi;
            VkDescriptorImageInfo oci{};
            oci.sampler = tg.occSmp; oci.imageView = tg.occView;
            oci.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet w10{};
            w10.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w10.dstSet = s.descSets[i]; w10.dstBinding = 10;
            w10.descriptorCount = 1;
            w10.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w10.pImageInfo = &oci;
            vkUpdateDescriptorSets(core.device, 7, w, 0, nullptr);
            vkUpdateDescriptorSets(core.device, 1, &w10, 0, nullptr);
            vkUpdateDescriptorSets(core.device, 3, wx, 0, nullptr);
        }
    }
    core.del.push([corep = &core, sp = &s]() {
        for (int i = 0; i < 2; i++) vmaDestroyBuffer(corep->alloc, sp->uboBuf[i], sp->uboAlloc[i]);
        vkDestroyDescriptorPool(corep->device, sp->descPool, nullptr);
        vkDestroyDescriptorSetLayout(corep->device, sp->setLayout, nullptr);
    });
    // ---- compute-cull: свой layout (meta/vis/indirect + вода meta/indirect) ----
    {
        VkDescriptorSetLayoutBinding cb[5]{};
        for (int b = 0; b < 5; b++) {
            cb[b].binding = (uint32_t)b;
            cb[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            cb[b].descriptorCount = 1;
            cb[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 5; li.pBindings = cb;
        VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.cullLayout));
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps.descriptorCount = 5;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.cullPool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = s.cullPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &s.cullLayout;
        VK_CHECK(vkAllocateDescriptorSets(core.device, &ai, &s.cullSet));
        VkDescriptorBufferInfo bi[5]{};
        bi[0].buffer = tg.metaBuf; bi[0].range = VK_WHOLE_SIZE;
        bi[1].buffer = tg.visBuf; bi[1].range = VK_WHOLE_SIZE;
        bi[2].buffer = tg.indBuf; bi[2].range = VK_WHOLE_SIZE;
        bi[3].buffer = tg.waterMetaBuf; bi[3].range = VK_WHOLE_SIZE;
        bi[4].buffer = tg.waterIndBuf; bi[4].range = VK_WHOLE_SIZE;
        VkWriteDescriptorSet w[5]{};
        for (int b = 0; b < 5; b++) {
            w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[b].dstSet = s.cullSet; w[b].dstBinding = (uint32_t)b;
            w[b].descriptorCount = 1;
            w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[b].pBufferInfo = &bi[b];
        }
        vkUpdateDescriptorSets(core.device, 5, w, 0, nullptr);
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.size = 6 * sizeof(glm::vec4); pc.offset = 0;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &s.cullLayout;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(core.device, &pli, nullptr, &s.cullPipeLayout));
        VkShaderModule cs = makeShader(core.device, SHADER_DIR "cull.comp.spv");
        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = cs; cpi.stage.pName = "main";
        cpi.layout = s.cullPipeLayout;
        VK_CHECK(vkCreateComputePipelines(core.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &s.cullPipe));
        vkDestroyShaderModule(core.device, cs, nullptr);
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyPipeline(corep->device, sp->cullPipe, nullptr);
            vkDestroyPipelineLayout(corep->device, sp->cullPipeLayout, nullptr);
            vkDestroyDescriptorSetLayout(corep->device, sp->cullLayout, nullptr);
            vkDestroyDescriptorPool(corep->device, sp->cullPool, nullptr);
        });
    }

    // ---- demo-5b bloom-цели (GENERAL навсегда): A половина, B четверть, A2 половина-финал ----
    s.bloomSmp = nullptr;
    {
        uint32_t bw[3] = {(core.swapExtent.width + 1) / 2, (core.swapExtent.width + 3) / 4,
                          (core.swapExtent.width + 1) / 2};
        uint32_t bh[3] = {(core.swapExtent.height + 1) / 2, (core.swapExtent.height + 3) / 4,
                          (core.swapExtent.height + 1) / 2};
        for (int i = 0; i < 3; i++) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            ci.extent = {bw[i], bh[i], 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &s.bloomImg[i], &s.bloomAlloc[i], nullptr));
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = s.bloomImg[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &s.bloomView[i]));
            core.immRun([&](VkCommandBuffer cb) {
                imgBarrier(cb, s.bloomImg[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            });
        }
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &s.bloomSmp));
    }
    core.del.push([corep = &core, sp = &s]() {
        vkDestroySampler(corep->device, sp->bloomSmp, nullptr);
        for (int i = 0; i < 3; i++) {
            vkDestroyImageView(corep->device, sp->bloomView[i], nullptr);
            vmaDestroyImage(corep->alloc, sp->bloomImg[i], sp->bloomAlloc[i]);
        }
    });

    // ---- demo-5a пост: 2 набора (на кадр: exp ping-pong; апдейт до бинда = безопасно) ----
    {
        VkDescriptorSetLayoutBinding pb[7]{};
        pb[0].binding = 0;
        pb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[0].descriptorCount = 1;
        pb[0].stageFlags = (VkShaderStageFlags)(VK_SHADER_STAGE_COMPUTE_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT);
        pb[1].binding = 1;
        pb[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[1].descriptorCount = 1;
        pb[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        for (int b = 2; b < 5; b++) {
            pb[b].binding = (uint32_t)b;
            pb[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            pb[b].descriptorCount = 1;
            pb[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        pb[5].binding = 5; // demo-5b: bloom для тонемэппа
        pb[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[5].descriptorCount = 1;
        pb[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pb[6].binding = 6; // demo-6: SSAO для тонемэппа (дописывается позже)
        pb[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pb[6].descriptorCount = 1;
        pb[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutBinding pball[7] = {pb[0], pb[1], pb[2], pb[3], pb[4], pb[5], pb[6]};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 7; li.pBindings = pball;
        VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.postLayout));
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 8;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = 6;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 2;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.postPool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = s.postPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &s.postLayout;
        for (int i = 0; i < 2; i++) {
            VK_CHECK(vkAllocateDescriptorSets(core.device, &ai, &s.postSet[i]));
            VkDescriptorImageInfo ii[2]{};
            ii[0].sampler = tg.hdrSmp; ii[0].imageView = tg.hdrView;
            ii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ii[1].sampler = tg.expSmp; ii[1].imageView = tg.expView[i];
            ii[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo si[3]{};
            VkImageView siv[3] = {tg.lumView, tg.expView[0], tg.expView[1]};
            for (int b = 0; b < 3; b++) {
                si[b].sampler = VK_NULL_HANDLE; si[b].imageView = siv[b];
                si[b].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            VkDescriptorImageInfo bi5{};
            bi5.sampler = s.bloomSmp; bi5.imageView = s.bloomView[2];
            bi5.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet w[6]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = s.postSet[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].pImageInfo = &ii[0];
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = s.postSet[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].pImageInfo = &ii[1];
            for (int b = 2; b < 5; b++) {
                w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[b].dstSet = s.postSet[i]; w[b].dstBinding = (uint32_t)b;
                w[b].descriptorCount = 1;
                w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w[b].pImageInfo = &si[b - 2];
            }
            w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[5].dstSet = s.postSet[i]; w[5].dstBinding = 5;
            w[5].descriptorCount = 1;
            w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[5].pImageInfo = &bi5;
            vkUpdateDescriptorSets(core.device, 6, w, 0, nullptr);
        }
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.size = 16; pc.offset = 0; // dt + parity + pad
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &s.postLayout;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(core.device, &pli, nullptr, &s.postComputeLayout));
        auto mkCompute = [&](const char* spv, VkPipeline& out) {
            VkShaderModule cs = makeShader(core.device, spv);
            VkComputePipelineCreateInfo cpi{};
            cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpi.stage.module = cs; cpi.stage.pName = "main";
            cpi.layout = s.postComputeLayout;
            VK_CHECK(vkCreateComputePipelines(core.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &out));
            vkDestroyShaderModule(core.device, cs, nullptr);
        };
        char lumPath[1024], adaptPath[1024];
        snprintf(lumPath, sizeof(lumPath), "%slum.comp.spv", SHADER_DIR);
        snprintf(adaptPath, sizeof(adaptPath), "%sadapt.comp.spv", SHADER_DIR);
        mkCompute(lumPath, s.lumPipe);
        mkCompute(adaptPath, s.adaptPipe);
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyPipeline(corep->device, sp->lumPipe, nullptr);
            vkDestroyPipeline(corep->device, sp->adaptPipe, nullptr);
            vkDestroyPipelineLayout(corep->device, sp->postComputeLayout, nullptr);
            vkDestroyDescriptorSetLayout(corep->device, sp->postLayout, nullptr);
            vkDestroyDescriptorPool(corep->device, sp->postPool, nullptr);
        });
    }

    // ---- demo-5b bloom-наборы: 3 прохода (bright/down/up), картинки статичны ----
    {
        VkDescriptorSetLayoutBinding bb[3]{};
        bb[0].binding = 0;
        bb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bb[0].descriptorCount = 1;
        bb[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bb[1].binding = 1;
        bb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bb[1].descriptorCount = 1;
        bb[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bb[2].binding = 2; // только up (база); down игнорирует
        bb[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bb[2].descriptorCount = 1;
        bb[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bb;
        VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.bloomLayout));
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 3 * 2;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = 3;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 3;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.bloomPool));
        // проходы: 0 bright HDR->A, 1 down A->B, 2 up B->A2 (+base A)
        VkImage srcs[3] = {tg.hdrImg, s.bloomImg[0], s.bloomImg[1]};
        VkImageView srcv[3] = {tg.hdrView, s.bloomView[0], s.bloomView[1]};
        VkSampler srcsmp[3] = {tg.hdrSmp, s.bloomSmp, s.bloomSmp};
        VkImage dsts[3] = {s.bloomImg[0], s.bloomImg[1], s.bloomImg[2]};
        VkImageView dstv[3] = {s.bloomView[0], s.bloomView[1], s.bloomView[2]};
        for (int i = 0; i < 3; i++) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = s.bloomPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &s.bloomLayout;
            VK_CHECK(vkAllocateDescriptorSets(core.device, &ai, &s.bloomSet[i]));
            VkDescriptorImageInfo ii[3]{};
            ii[0].sampler = srcsmp[i]; ii[0].imageView = srcv[i];
            ii[0].imageLayout = (i == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         : VK_IMAGE_LAYOUT_GENERAL;
            ii[1].sampler = VK_NULL_HANDLE; ii[1].imageView = dstv[i];
            ii[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            ii[2].sampler = s.bloomSmp; ii[2].imageView = s.bloomView[0];
            ii[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet w[3]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = s.bloomSet[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].pImageInfo = &ii[0];
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = s.bloomSet[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w[1].pImageInfo = &ii[1];
            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = s.bloomSet[i]; w[2].dstBinding = 2;
            w[2].descriptorCount = 1;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[2].pImageInfo = &ii[2];
            vkUpdateDescriptorSets(core.device, 3, w, 0, nullptr);
        }
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &s.bloomLayout;
        pli.pushConstantRangeCount = 0; pli.pPushConstantRanges = nullptr;
        VK_CHECK(vkCreatePipelineLayout(core.device, &pli, nullptr, &s.bloomPipeLayout));
        auto mkBCompute = [&](const char* name, VkPipeline& out) {
            char p[1024];
            snprintf(p, sizeof(p), "%s%s.spv", SHADER_DIR, name);
            VkShaderModule cs = makeShader(core.device, p);
            VkComputePipelineCreateInfo cpi{};
            cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpi.stage.module = cs; cpi.stage.pName = "main";
            cpi.layout = s.bloomPipeLayout;
            VK_CHECK(vkCreateComputePipelines(core.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &out));
            vkDestroyShaderModule(core.device, cs, nullptr);
        };
        mkBCompute("bright.comp", s.brightPipe);
        mkBCompute("kdown.comp", s.kdownPipe);
        mkBCompute("kup.comp", s.kupPipe);
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyPipeline(corep->device, sp->brightPipe, nullptr);
            vkDestroyPipeline(corep->device, sp->kdownPipe, nullptr);
            vkDestroyPipeline(corep->device, sp->kupPipe, nullptr);
            vkDestroyPipelineLayout(corep->device, sp->bloomPipeLayout, nullptr);
            vkDestroyDescriptorSetLayout(corep->device, sp->bloomLayout, nullptr);
            vkDestroyDescriptorPool(corep->device, sp->bloomPool, nullptr);
        });
    }

    // ---- demo-5c небо LUT: T 256x64 + MS 32x32, bake на CPU, upload staging'ом ----
    // Свой сет для неба (UBO кадра + 2 LUT): layout террейна не трогаем.
    s.skyLayout = nullptr;
    {
        printf("sky LUT bake...\n");
        std::vector<float> trans = sky::bakeTransmittance();
        std::vector<float> multi = sky::bakeMultiscatter();
        printf("sky LUT done\n");
        s.skyTImg = nullptr, s.skyMImg = nullptr;
        s.skyTAlloc = nullptr, s.skyMAlloc = nullptr;
        s.skyTView = nullptr, s.skyMView = nullptr;
        auto mkLut = [&](int w, int h, const std::vector<float>& px,
                         VkImage& img, VmaAllocation& alc, VkImageView& view) {
            VkBuffer stg;
            VmaAllocation stgAlc;
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = px.size() * sizeof(float);
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo aci{};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VK_CHECK(vmaCreateBuffer(core.alloc, &bi, &aci, &stg, &stgAlc, nullptr));
            void* dst = nullptr;
            VK_CHECK(vmaMapMemory(core.alloc, stgAlc, &dst));
            memcpy(dst, px.data(), px.size() * sizeof(float));
            vmaUnmapMemory(core.alloc, stgAlc);
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            ci.extent = {(uint32_t)w, (uint32_t)h, 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &img, &alc, nullptr));
            core.immRun([&](VkCommandBuffer cb) {
                imgBarrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                VkBufferImageCopy cp{};
                cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                cp.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
                vkCmdCopyBufferToImage(cb, stg, img,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
                imgBarrier(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT);
            });
            vmaDestroyBuffer(core.alloc, stg, stgAlc);
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &view));
        };
        mkLut(sky::TRANS_W, sky::TRANS_H, trans, s.skyTImg, s.skyTAlloc, s.skyTView);
        mkLut(sky::MS_W, sky::MS_H, multi, s.skyMImg, s.skyMAlloc, s.skyMView);
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyImageView(corep->device, sp->skyTView, nullptr);
            vmaDestroyImage(corep->alloc, sp->skyTImg, sp->skyTAlloc);
            vkDestroyImageView(corep->device, sp->skyMView, nullptr);
            vmaDestroyImage(corep->alloc, sp->skyMImg, sp->skyMAlloc);
        });
        s.skySmp = nullptr;
        {
            VkSamplerCreateInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.magFilter = VK_FILTER_LINEAR;
            si.minFilter = VK_FILTER_LINEAR;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            si.addressModeU = si.addressModeV = si.addressModeW =
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VK_CHECK(vkCreateSampler(core.device, &si, nullptr, &s.skySmp));
        }
        core.del.push([corep = &core, sp = &s]() { vkDestroySampler(corep->device, sp->skySmp, nullptr); });
        // Сет неба: UBO кадра (уже создан выше) + 2 LUT.
        {
            VkDescriptorSetLayoutBinding b0{};
            b0.binding = 0;
            b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            b0.descriptorCount = 1;
            b0.stageFlags = (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                                 VK_SHADER_STAGE_FRAGMENT_BIT);
            VkDescriptorSetLayoutBinding b1{};
            b1.binding = 1;
            b1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b1.descriptorCount = 1;
            b1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutBinding b2{};
            b2.binding = 2;
            b2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b2.descriptorCount = 1;
            b2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutBinding bs[3] = {b0, b1, b2};
            VkDescriptorSetLayoutCreateInfo li{};
            li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            li.bindingCount = 3; li.pBindings = bs;
            VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.skyLayout));
        }
        s.skyPool = nullptr;
        {
            VkDescriptorPoolSize ps[2]{};
            ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[0].descriptorCount = 2;
            ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 4;
            VkDescriptorPoolCreateInfo pi{};
            pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pi.maxSets = 2;
            pi.poolSizeCount = 2; pi.pPoolSizes = ps;
            VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.skyPool));
        }
        for (int i = 0; i < 2; i++) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = s.skyPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &s.skyLayout;
            VK_CHECK(vkAllocateDescriptorSets(core.device, &ai, &s.skySets[i]));
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = s.uboBuf[i]; dbi.range = sizeof(FrameUBO);
            VkDescriptorImageInfo tii{};
            tii.sampler = s.skySmp; tii.imageView = s.skyTView;
            tii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorImageInfo mii{};
            mii.sampler = s.skySmp; mii.imageView = s.skyMView;
            mii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet w[3]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = s.skySets[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[0].pBufferInfo = &dbi;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = s.skySets[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].pImageInfo = &tii;
            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = s.skySets[i]; w[2].dstBinding = 2;
            w[2].descriptorCount = 1;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[2].pImageInfo = &mii;
            vkUpdateDescriptorSets(core.device, 3, w, 0, nullptr);
        }
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyDescriptorPool(corep->device, sp->skyPool, nullptr);
            vkDestroyDescriptorSetLayout(corep->device, sp->skyLayout, nullptr);
        });
    }

    // ---- demo-6 SSAO: R16F-цель (GENERAL) + свой compute-сет/пайп ----
    s.ssaoImg = nullptr;
    s.ssaoAlloc = nullptr;
    s.ssaoView = nullptr;
    s.ssaoSet = nullptr;
    s.ssaoPipeLayout = nullptr;
    s.ssaoPipe = nullptr;
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R16_SFLOAT;
        ci.extent = {core.swapExtent.width, core.swapExtent.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &s.ssaoImg, &s.ssaoAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = s.ssaoImg;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &s.ssaoView));
        core.immRun([&](VkCommandBuffer cb) {
            imgBarrier(cb, s.ssaoImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        });
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyImageView(corep->device, sp->ssaoView, nullptr);
            vmaDestroyImage(corep->alloc, sp->ssaoImg, sp->ssaoAlloc);
        });
        VkDescriptorSetLayoutBinding b0{};
        b0.binding = 0;
        b0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b0.descriptorCount = 1;
        b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutBinding b1{};
        b1.binding = 1;
        b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b1.descriptorCount = 1;
        b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutBinding bs[2] = {b0, b1};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bs;
        VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.ssaoLayout));
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 1;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.ssaoPool));
        VkDescriptorSetAllocateInfo sai{};
        sai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        sai.descriptorPool = s.ssaoPool;
        sai.descriptorSetCount = 1;
        sai.pSetLayouts = &s.ssaoLayout;
        VK_CHECK(vkAllocateDescriptorSets(core.device, &sai, &s.ssaoSet));
        VkDescriptorImageInfo dii{};
        dii.sampler = tg.shadowRawSmp; dii.imageView = tg.depthCopyView; // D32 nearest
        dii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo sii{};
        sii.sampler = VK_NULL_HANDLE; sii.imageView = s.ssaoView;
        sii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet w[2]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = s.ssaoSet; w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[0].pImageInfo = &dii;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = s.ssaoSet; w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[1].pImageInfo = &sii;
        vkUpdateDescriptorSets(core.device, 2, w, 0, nullptr);
        // Тонемэпп читает SSAO линейно (неявный блюр, как combine в книге).
        for (int i = 0; i < 2; i++) {
            VkDescriptorImageInfo aoi{};
            aoi.sampler = s.bloomSmp; aoi.imageView = s.ssaoView;
            aoi.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet aw{};
            aw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            aw.dstSet = s.postSet[i]; aw.dstBinding = 6;
            aw.descriptorCount = 1;
            aw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            aw.pImageInfo = &aoi;
            vkUpdateDescriptorSets(core.device, 1, &aw, 0, nullptr);
        }
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.size = 32; pc.offset = 0; // res + params
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &s.ssaoLayout;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(core.device, &pli, nullptr, &s.ssaoPipeLayout));
        VkShaderModule cs = makeShader(core.device, SHADER_DIR "ssao.comp.spv");
        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = cs; cpi.stage.pName = "main";
        cpi.layout = s.ssaoPipeLayout;
        VK_CHECK(vkCreateComputePipelines(core.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &s.ssaoPipe));
        vkDestroyShaderModule(core.device, cs, nullptr);
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyPipeline(corep->device, sp->ssaoPipe, nullptr);
            vkDestroyPipelineLayout(corep->device, sp->ssaoPipeLayout, nullptr);
            vkDestroyDescriptorSetLayout(corep->device, sp->ssaoLayout, nullptr);
            vkDestroyDescriptorPool(corep->device, sp->ssaoPool, nullptr);
        });
    }

    // ---- demo-7 TAA: 2 history R16F (GENERAL) + 2 сета крест-накрест ----
    // set[fi]: читает H[fi^1], пишет H[fi] — перкадровых апдейтов не надо.
    {
        for (int i = 0; i < 2; i++) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            ci.extent = {core.swapExtent.width, core.swapExtent.height, 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VK_CHECK(vmaCreateImage(core.alloc, &ci, &ai, &s.histImg[i], &s.histAlloc[i], nullptr));
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = s.histImg[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(core.device, &vi, nullptr, &s.histView[i]));
            core.immRun([&](VkCommandBuffer cb) {
                imgBarrier(cb, s.histImg[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            });
        }
        VkDescriptorSetLayoutBinding b[5]{};
        b[0].binding = 0; // HDR кадра
        b[1].binding = 1; // история (linear)
        b[2].binding = 2; // история-выход
        b[3].binding = 3; // глубина
        b[4].binding = 4; // UBO (prevViewProj + invViewProj)
        b[0].descriptorType = b[1].descriptorType = b[3].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        for (int i = 0; i < 5; i++) {
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 5; li.pBindings = b;
        VK_CHECK(vkCreateDescriptorSetLayout(core.device, &li, nullptr, &s.taaLayout));
        VkDescriptorPoolSize ps[3]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 6;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = 2;
        ps[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[2].descriptorCount = 2;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 2;
        pi.poolSizeCount = 3; pi.pPoolSizes = ps;
        VK_CHECK(vkCreateDescriptorPool(core.device, &pi, nullptr, &s.taaPool));
        for (int i = 0; i < 2; i++) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = s.taaPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &s.taaLayout;
            VK_CHECK(vkAllocateDescriptorSets(core.device, &ai, &s.taaSet[i]));
            VkDescriptorImageInfo ci0{};
            ci0.sampler = tg.shadowRawSmp; ci0.imageView = tg.hdrView;
            ci0.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorImageInfo ci1{};
            ci1.sampler = s.bloomSmp; ci1.imageView = s.histView[i ^ 1];
            ci1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo ci2{};
            ci2.sampler = VK_NULL_HANDLE; ci2.imageView = s.histView[i];
            ci2.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo ci3{};
            ci3.sampler = tg.shadowRawSmp; ci3.imageView = tg.depthCopyView;
            ci3.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = s.uboBuf[i]; dbi.range = sizeof(FrameUBO);
            VkWriteDescriptorSet w[5]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = s.taaSet[i]; w[0].dstBinding = 0;
            w[0].descriptorCount = 1;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].pImageInfo = &ci0;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = s.taaSet[i]; w[1].dstBinding = 1;
            w[1].descriptorCount = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].pImageInfo = &ci1;
            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = s.taaSet[i]; w[2].dstBinding = 2;
            w[2].descriptorCount = 1;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w[2].pImageInfo = &ci2;
            w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[3].dstSet = s.taaSet[i]; w[3].dstBinding = 3;
            w[3].descriptorCount = 1;
            w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[3].pImageInfo = &ci3;
            w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[4].dstSet = s.taaSet[i]; w[4].dstBinding = 4;
            w[4].descriptorCount = 1;
            w[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[4].pBufferInfo = &dbi;
            vkUpdateDescriptorSets(core.device, 5, w, 0, nullptr);
        }
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.size = 32; pc.offset = 0; // res + params
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &s.taaLayout;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(core.device, &pli, nullptr, &s.taaPipeLayout));
        VkShaderModule cs = makeShader(core.device, SHADER_DIR "taa.comp.spv");
        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = cs; cpi.stage.pName = "main";
        cpi.layout = s.taaPipeLayout;
        VK_CHECK(vkCreateComputePipelines(core.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &s.taaPipe));
        vkDestroyShaderModule(core.device, cs, nullptr);
        core.del.push([corep = &core, sp = &s]() {
            vkDestroyPipeline(corep->device, sp->taaPipe, nullptr);
            vkDestroyPipelineLayout(corep->device, sp->taaPipeLayout, nullptr);
            vkDestroyDescriptorSetLayout(corep->device, sp->taaLayout, nullptr);
            vkDestroyDescriptorPool(corep->device, sp->taaPool, nullptr);
            for (int i = 0; i < 2; i++) {
                vkDestroyImageView(corep->device, sp->histView[i], nullptr);
                vmaDestroyImage(corep->alloc, sp->histImg[i], sp->histAlloc[i]);
            }
        });
    }

}
