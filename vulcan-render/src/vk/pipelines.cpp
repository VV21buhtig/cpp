// pipelines: см. vk/pipelines.h.
#include "vk/pipelines.h"
#include "vk/descriptors.h"
#include <cstdio>

void makePipes(VkCore& core, Sets& st, Pipes& p) {
    // ---- пайплайн террейна (vertex-input 12 floats, depth, cull NONE на demo-2) ----
    {
        VkShaderModule vs = makeShader(core.device, SHADER_DIR "vk_terrain.vert.spv");
        VkShaderModule fs = makeShader(core.device, SHADER_DIR "vk_terrain.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
        // demo-3a pulling (Ch05): vertex-input ПУСТОЙ, вершины тянет шейдер
        // из SSBO (binding 2) по gl_VertexIndex. Индекс-буфер — следующим шагом.
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE; // demo-2: winding проверим глазами, каллинг позже
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
        VkPushConstantRange pc{};
        pc.stageFlags = (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                             VK_SHADER_STAGE_FRAGMENT_BIT); // lightSpace VS + res FS
        pc.size = sizeof(glm::mat4); pc.offset = 0;
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = 1; li.pSetLayouts = &st.setLayout;
        li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(core.device, &li, nullptr, &p.pipeLayout));
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        VkFormat hdrPipeFmt = VK_FORMAT_R16G16B16A16_SFLOAT; // террейн всегда в HDR!
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &hdrPipeFmt;
        ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.pNext = &ri;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDepthStencilState = &ds;
        pi.pDynamicState = &dyn;
        pi.layout = p.pipeLayout;
        VK_CHECK(vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p.pipeline));
        vkDestroyShaderModule(core.device, vs, nullptr);
        vkDestroyShaderModule(core.device, fs, nullptr);
    }
    core.del.push([corep = &core, pp = &p]() {
        vkDestroyPipeline(corep->device, pp->pipeline, nullptr);
        vkDestroyPipelineLayout(corep->device, pp->pipeLayout, nullptr);
    });

    // ---- demo-4 shadow p.pipeline: те же st.setLayout+push (совместим!), только глубина.
    // Depth bias — драйверный (рецепт книги), включается динамикой.
    {
        VkShaderModule vs = makeShader(core.device, SHADER_DIR "shadow.vert.spv");
        VkShaderModule fs = makeShader(core.device, SHADER_DIR "shadow.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        rs.depthBiasEnable = VK_TRUE; // bias задаём командой (const/slope из книги)
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 0; cb.pAttachments = nullptr; // цвета нет
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_DEPTH_BIAS};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ri.colorAttachmentCount = 0; ri.pColorAttachmentFormats = nullptr;
        ri.depthAttachmentFormat = VK_FORMAT_D16_UNORM;
        pi.pNext = &ri;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDepthStencilState = &ds;
        pi.pDynamicState = &dyn;
        pi.layout = p.pipeLayout; // тот же layout: set с giga/meta + push lightSpace
        VK_CHECK(vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p.shadowPipe));
        vkDestroyShaderModule(core.device, vs, nullptr);
        vkDestroyShaderModule(core.device, fs, nullptr);
    }
    core.del.push([corep = &core, pp = &p]() { vkDestroyPipeline(corep->device, pp->shadowPipe, nullptr); });

    // ---- demo-5w вода: тот же layout (superset), бленд ON, глубину только читаем ----
    {
        VkShaderModule vs = makeShader(core.device, SHADER_DIR "water.vert.spv");
        VkShaderModule fs = makeShader(core.device, SHADER_DIR "water.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.blendEnable = VK_TRUE; // прозрачная гладь поверх террейна
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.alphaBlendOp = VK_BLEND_OP_ADD;
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE; // вода пишет глубину: перекрытия одной
        ds.depthCompareOp = VK_COMPARE_OP_LESS; // среды решает depth, не порядок бленда
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        VkFormat whdrFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &whdrFmt;
        ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.pNext = &ri;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDepthStencilState = &ds;
        pi.pDynamicState = &dyn;
        pi.layout = p.pipeLayout; // superset: вода берёт биндинги 0,1,7,8
        VK_CHECK(vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p.waterPipe));
        vkDestroyShaderModule(core.device, vs, nullptr);
        vkDestroyShaderModule(core.device, fs, nullptr);
    }
    core.del.push([corep = &core, pp = &p]() { vkDestroyPipeline(corep->device, pp->waterPipe, nullptr); });

    // ---- demo-4b рентген: фулскрин-три в угол 256x256 (F1), тот же st.setLayout ----
    {
        VkShaderModule vs = makeShader(core.device, SHADER_DIR "tri.vert.spv");
        VkShaderModule fs = makeShader(core.device, SHADER_DIR "dbg.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        VkPipelineRenderingCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &core.swapFormat;
        ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        pi.pNext = &ri;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDepthStencilState = &ds;
        pi.pDynamicState = &dyn;
        pi.layout = p.pipeLayout; // тот же set (binding 6 с сырой глубиной)
        VK_CHECK(vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p.dbgPipe));
        vkDestroyShaderModule(core.device, vs, nullptr);
        vkDestroyShaderModule(core.device, fs, nullptr);
    }
    core.del.push([corep = &core, pp = &p]() { vkDestroyPipeline(corep->device, pp->dbgPipe, nullptr); });

    // ---- demo-5a небо + тонемэппинг: фулскрин-пайпы (layout = набор + свой пуш) ----
    p.skyPipeLayout, p.tonemapPipeLayout;
    p.skyPipe, p.tonemapPipe;
    {
        auto mkLayout = [&](VkDescriptorSetLayout set, uint32_t pushSize,
                            VkShaderStageFlags pushStage, VkPipelineLayout& out) {
            VkPushConstantRange pc{};
            pc.stageFlags = pushStage;
            pc.size = pushSize; pc.offset = 0;
            VkPipelineLayoutCreateInfo li{};
            li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            li.setLayoutCount = 1; li.pSetLayouts = &set;
            li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pc;
            VK_CHECK(vkCreatePipelineLayout(core.device, &li, nullptr, &out));
        };
        mkLayout(st.skyLayout, 16, VK_SHADER_STAGE_FRAGMENT_BIT, p.skyPipeLayout);
        mkLayout(st.postLayout, 16, VK_SHADER_STAGE_FRAGMENT_BIT, p.tonemapPipeLayout);
        auto mkFullPipe = [&](const char* fsName, VkFormat colorFmt,
                              VkPipelineLayout layout, VkPipeline& out) {
            VkShaderModule vs = makeShader(core.device, SHADER_DIR "tri.vert.spv");
            char fsPath[1024];
            snprintf(fsPath, sizeof(fsPath), "%s%s.spv", SHADER_DIR, fsName);
            VkShaderModule fs = makeShader(core.device, fsPath);
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs; stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs; stages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo ia{};
            ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo vp{};
            vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vp.viewportCount = 1; vp.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo rs{};
            rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState ba{};
            ba.colorWriteMask = 0xF;
            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1; cb.pAttachments = &ba;
            VkPipelineDepthStencilStateCreateInfo ds{};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            ds.depthTestEnable = VK_FALSE;
            ds.depthWriteEnable = VK_FALSE;
            VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dyn{};
            dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;
            VkGraphicsPipelineCreateInfo pi{};
            pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            VkPipelineRenderingCreateInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &colorFmt;
            ri.depthAttachmentFormat = VK_FORMAT_UNDEFINED; // без глубины
            pi.pNext = &ri;
            pi.stageCount = 2; pi.pStages = stages;
            pi.pVertexInputState = &vi;
            pi.pInputAssemblyState = &ia;
            pi.pViewportState = &vp;
            pi.pRasterizationState = &rs;
            pi.pMultisampleState = &ms;
            pi.pColorBlendState = &cb;
            pi.pDepthStencilState = &ds;
            pi.pDynamicState = &dyn;
            pi.layout = layout;
            VK_CHECK(vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pi, nullptr, &out));
            vkDestroyShaderModule(core.device, vs, nullptr);
            vkDestroyShaderModule(core.device, fs, nullptr);
        };
        mkFullPipe("sky.frag", VK_FORMAT_R16G16B16A16_SFLOAT, p.skyPipeLayout, p.skyPipe);
        mkFullPipe("tonemap.frag", core.swapFormat, p.tonemapPipeLayout, p.tonemapPipe);
        core.del.push([corep = &core, pp = &p]() {
            vkDestroyPipeline(corep->device, pp->skyPipe, nullptr);
            vkDestroyPipeline(corep->device, pp->tonemapPipe, nullptr);
            vkDestroyPipelineLayout(corep->device, pp->skyPipeLayout, nullptr);
            vkDestroyPipelineLayout(corep->device, pp->tonemapPipeLayout, nullptr);
        });
    }
}
