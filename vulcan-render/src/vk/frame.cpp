// frame: см. vk/frame.h.
#include "vk/frame.h"
#include "vk/targets.h"
#include "vk/descriptors.h"
#include "vk/pipelines.h"
#include "engine/world.h"
#include "engine/blocks.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <cstdio>
#include <cstring>

void makeSync(VkCore& core, FrameSync& sy) {
    // Синхра: acquire-семафор по кадру, render-семафор + layout по картинке,
    // fence кадра ждётся ПЕРЕД acquire (сабмит позапрошлого кадра выполнен —
    // cmdbuf свободен, acquire-семафор потреблён). acquire ПЕРЕД fence картинки
    // не нужен: acquire сам ждёт present. Так велят слои (3 бага найдено ими).
    const int NIMGS = (int)core.swapImages.size();
    if (NIMGS > 8) { printf("too many swap images %d\n", NIMGS); exit(1); }
    for (int i = 0; i < NIMGS; i++) {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(core.device, &si, nullptr, &sy.acquireSem[i]));
        VK_CHECK(vkCreateSemaphore(core.device, &si, nullptr, &sy.renderSem[i]));
        sy.imgLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
        int j = i;
        core.del.push([corep = &core, syp = &sy, j]() {
            vkDestroySemaphore(corep->device, syp->acquireSem[j], nullptr);
            vkDestroySemaphore(corep->device, syp->renderSem[j], nullptr);
        });
    }
    {
        // Кадровые заборы отдельно: их ровно FRAMES, не путать с картинками.
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < FrameSync::FRAMES; i++)
            VK_CHECK(vkCreateFence(core.device, &fi, nullptr, &sy.frameFence[i]));
        core.del.push([corep = &core, syp = &sy]() {
            for (int i = 0; i < FrameSync::FRAMES; i++)
                vkDestroyFence(corep->device, syp->frameFence[i], nullptr);
        });
    }
    // ---- кадровые комманд-буферы (2 в полёте, синхра — по картинкам выше) ----
    VK_CHECK([&]() {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = core.gfxFamily;
        return vkCreateCommandPool(core.device, &ci, nullptr, &sy.cmdPool);
    }());
    core.del.push([corep = &core, syp = &sy]() {
        vkDestroyCommandPool(corep->device, syp->cmdPool, nullptr);
    });
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = sy.cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = FrameSync::FRAMES;
        VK_CHECK(vkAllocateCommandBuffers(core.device, &ai, sy.cmdBufs));
    }
    sy.nimgs = NIMGS;
}

// Halton (рецепт Kaigen wc_render: последовательность джиттера TAA).
static float haltonRoot(unsigned i, unsigned b) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) {
        f /= (float)b;
        r += f * (float)(i % b);
        i /= b;
    }
    return r;
}

int runFrameLoop(VkCore& core, World& world, const glm::vec3& worldOffset,
                 Targets& tg, Sets& st, Pipes& pp, FrameSync& sy, const FrameArgs& a) {
    // ---- камера: старт у холма, WASD+мышь+стрелки, Space/C, ESC выход ----
    glm::vec3 camPos, camFront;
    {
        int hx = 64, hz = 64, top = 20;
        for (int z = 20; z < 108; z++)
            for (int x = 20; x < 108; x++) {
                int t = -1;
                for (int y = 63; y >= 0; y--)
                    if (World::isSolid(world.getBlock(x, y, z))) { t = y; break; }
                if (t > top) { top = t; hx = x; hz = z; }
            }
        glm::vec3 target = worldOffset + glm::vec3(hx + 0.5f, top, hz + 0.5f);
        camPos = target + glm::vec3(20.0f, 12.0f, 28.0f);
        camFront = glm::normalize(target - camPos);
        core.ctl.yaw = glm::degrees(atan2(camFront.z, camFront.x));
        core.ctl.pitch = glm::degrees(asin(camFront.y));
        printf("hill (%d,%d,%d)\n", hx, top, hz);
        // Вода для прицела: самая большая гладь (для --cam).
        {
            int bx = 64, bz = 64, bn = 0;
            for (int z = 4; z < 124; z += 4)
                for (int x = 4; x < 124; x += 4) {
                    int n = 0;
                    for (int dz = 0; dz < 4; dz++)
                        for (int dx = 0; dx < 4; dx++)
                            if (world.getBlock(x + dx, 20, z + dz) == B_WATER) n++;
                    if (n > bn) { bn = n; bx = x; bz = z; }
                }
            printf("water (%d,%d) n=%d/16\n", bx, bz, bn);
        }
        if (a.camOverride) {
            camPos = a.camPosOvr;
            core.ctl.yaw = a.yawOvr;
            core.ctl.pitch = a.pitchOvr;
            camFront.x = cos(glm::radians(core.ctl.yaw)) * cos(glm::radians(core.ctl.pitch));
            camFront.y = sin(glm::radians(core.ctl.pitch));
            camFront.z = sin(glm::radians(core.ctl.yaw)) * cos(glm::radians(core.ctl.pitch));
            camFront = glm::normalize(camFront);
            printf("cam override (%g,%g,%g) yaw %g pitch %g\n",
                   camPos.x, camPos.y, camPos.z, core.ctl.yaw, core.ctl.pitch);
        }
    }
    auto updFront = [&]() {
        camFront.x = cos(glm::radians(core.ctl.yaw)) * cos(glm::radians(core.ctl.pitch));
        camFront.y = sin(glm::radians(core.ctl.pitch));
        camFront.z = sin(glm::radians(core.ctl.yaw)) * cos(glm::radians(core.ctl.pitch));
        camFront = glm::normalize(camFront);
    };

    glm::mat4 proj = glm::perspective(glm::radians(70.0f),
        (float)core.swapExtent.width / (float)core.swapExtent.height, 0.1f, 600.0f);
    proj[1][1] *= -1.0f; // Y-flip под Vulkan (идиома vkguide)
    float tod = 1.5707f; // полдень (1/2/3 утро/день/вечер, F1 рентген карты)
    if (getenv("VK_TOD")) tod = (float)atof(getenv("VK_TOD")); // рентген: фикс солнца
    bool dbgShadow = false, prevF1 = false;
    bool useSsao = true, prevF2 = false; // F2: SSAO вкл/выкл
    bool useTaa = true, prevF3 = false;  // F3: TAA вкл/выкл (+сброс истории)
    double prevT = glfwGetTime();
    int frame = 0, drawn = 0;
    double fpsT = prevT;
    int fpsN = 0;
    glm::mat4 prevVP(1.0f); // demo-7: VP прошлого кадра (с джиттером)
    float prevTod = tod;
    bool prevUseTaa = true;
    while (!glfwWindowShouldClose(core.window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)(now - prevT);
        prevT = now;
        if (dt > 0.05f) dt = 0.05f;
        // ввод
        {
            float sp = (glfwGetKey(core.window, GLFW_KEY_LEFT_SHIFT) ? 30.0f : 12.0f) * dt;
            glm::vec3 right = glm::normalize(glm::cross(camFront, glm::vec3(0, 1, 0)));
            if (glfwGetKey(core.window, GLFW_KEY_W)) camPos += camFront * sp;
            if (glfwGetKey(core.window, GLFW_KEY_S)) camPos -= camFront * sp;
            if (glfwGetKey(core.window, GLFW_KEY_A)) camPos -= right * sp;
            if (glfwGetKey(core.window, GLFW_KEY_D)) camPos += right * sp;
            if (glfwGetKey(core.window, GLFW_KEY_SPACE)) camPos.y += sp;
            if (glfwGetKey(core.window, GLFW_KEY_C)) camPos.y -= sp;
            float rs = 60.0f * dt;
            if (glfwGetKey(core.window, GLFW_KEY_LEFT)) core.ctl.yaw -= rs;
            if (glfwGetKey(core.window, GLFW_KEY_RIGHT)) core.ctl.yaw += rs;
            if (glfwGetKey(core.window, GLFW_KEY_UP)) core.ctl.pitch += rs;
            if (glfwGetKey(core.window, GLFW_KEY_DOWN)) core.ctl.pitch -= rs;
            if (core.ctl.pitch > 89.0f) core.ctl.pitch = 89.0f;
            if (core.ctl.pitch < -89.0f) core.ctl.pitch = -89.0f;
            updFront();
            if (glfwGetKey(core.window, GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(core.window, 1);
            if (glfwGetKey(core.window, GLFW_KEY_1)) tod = 0.5f;   // утро: длинные тени
            if (glfwGetKey(core.window, GLFW_KEY_2)) tod = 1.5707f; // полдень
            if (glfwGetKey(core.window, GLFW_KEY_3)) tod = 2.6f;    // вечер: длинные тени
            bool f1 = glfwGetKey(core.window, GLFW_KEY_F1) != 0;
            if (f1 && !prevF1) { dbgShadow = !dbgShadow; printf("shadow xray %d\n", dbgShadow); }
            prevF1 = f1;
            bool f2 = glfwGetKey(core.window, GLFW_KEY_F2) != 0;
            if (f2 && !prevF2) { useSsao = !useSsao; printf("ssao %d\n", useSsao); }
            prevF2 = f2;
            bool f3 = glfwGetKey(core.window, GLFW_KEY_F3) != 0;
            if (f3 && !prevF3) { useTaa = !useTaa; printf("taa %d\n", useTaa); }
            prevF3 = f3;
        }
        int fi = frame % FrameSync::FRAMES;
        uint32_t imgIdx = 0;
        // Кадровый fence ПЕРЕД acquire: сабмит двухкадровой давности точно
        // выполнен → cmdbuf свободен, acquire-семафор потреблён. Без этого
        // слои орут про pending semaphore/commandbuffer (проверено).
        VK_CHECK(vkWaitForFences(core.device, 1, &sy.frameFence[fi], VK_TRUE, 1000000000ull));
        VK_CHECK(vkResetFences(core.device, 1, &sy.frameFence[fi]));
        // acquire ПЕРЕД записью: картинка вернётся только после своего present,
        // layout трекаем сами. Кадровый fence выше уже гарантирует свободный cmdbuf.
        VK_CHECK(vkAcquireNextImageKHR(core.device, core.swapchain, 1000000000ull,
                                       sy.acquireSem[fi], VK_NULL_HANDLE, &imgIdx));
        // demo-7 джиттер: Halton 2/3, hi = drawn%8+1 (рецепт Kaigen).
        // Смещение в NDC (+= в proj[2][0..1]) — вся математика репроекции
        // идёт через матрицы, знак с Y-флипом сходится сам.
        unsigned hi = (unsigned)(drawn % 8) + 1;
        float jx = (haltonRoot(hi, 2) - 0.5f) * 2.0f / (float)core.swapExtent.width;
        float jy = (haltonRoot(hi, 3) - 0.5f) * 2.0f / (float)core.swapExtent.height;
        glm::mat4 jproj = proj;
        if (useTaa) {
            jproj[2][0] += jx;
            jproj[2][1] += jy;
        }
        bool taaReset = (drawn == 0) || (fabsf(tod - prevTod) > 1e-6f) || (useTaa && !prevUseTaa);
        prevTod = tod;
        prevUseTaa = useTaa;
        // UBO кадра
        {
            FrameUBO u{};
            u.viewProj = jproj * glm::lookAt(camPos, camPos + camFront, glm::vec3(0, 1, 0));
            u.invViewProj = glm::inverse(u.viewProj);
            u.prevViewProj = prevVP;
            glm::vec3 sun = glm::normalize(glm::vec3(cos(tod), sin(tod), 0.35f));
            u.sunDir = glm::vec4(sun, 0.0f);
            u.sunCol = glm::vec4(1.25f, 1.21f, 1.12f, 0.0f);
            u.ambSky = glm::vec4(0.54f, 0.60f, 0.69f, 0.0f);
            u.ambGnd = glm::vec4(0.27f, 0.24f, 0.21f, 0.0f);
            u.fog = glm::vec4(0.55f, 0.65f, 0.80f, 260.0f);
            u.misc = glm::vec4(40.0f, 1.1f, 1.2f, (float)now);
            u.viewPos = glm::vec4(camPos, 0.0f);
            void* dst = nullptr;
            VK_CHECK(vmaMapMemory(core.alloc, st.uboAlloc[fi], &dst));
            memcpy(dst, &u, sizeof(u));
            vmaUnmapMemory(core.alloc, st.uboAlloc[fi]);
            prevVP = u.viewProj; // demo-7: следующему кадру
        }
        VK_CHECK(vkResetCommandBuffer(sy.cmdBufs[fi], 0));
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(sy.cmdBufs[fi], &bi));
        VkImageMemoryBarrier toDraw{};
        toDraw.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDraw.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toDraw.oldLayout = sy.imgLayout[imgIdx]; // трекаем: первый раз UNDEFINED
        toDraw.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toDraw.image = core.swapImages[imgIdx];
        toDraw.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDraw);
        // demo-3c + вода: обнулить оба счётчика (fill + барьер transfer->compute).
        vkCmdFillBuffer(sy.cmdBufs[fi], tg.indBuf, 0, 4, 0);
        vkCmdFillBuffer(sy.cmdBufs[fi], tg.waterIndBuf, 0, 4, 0);
        {
            VkBufferMemoryBarrier b[2]{};
            VkBuffer bbs[2] = {tg.indBuf, tg.waterIndBuf};
            for (int i = 0; i < 2; i++) {
                b[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                b[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                b[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                b[i].buffer = bbs[i]; b[i].offset = 0; b[i].size = VK_WHOLE_SIZE;
            }
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 2, b, 0, nullptr);
        }
        // 2) плоскости фрустума (строки viewProj, нормированные).
        glm::mat4 vp = jproj * glm::lookAt(camPos, camPos + camFront, glm::vec3(0, 1, 0));
        glm::vec4 planes[6];
        {
            glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
            glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
            glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
            glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
            planes[0] = r3 + r0; planes[1] = r3 - r0;
            planes[2] = r3 + r1; planes[3] = r3 - r1;
            planes[4] = r3 + r2; planes[5] = r3 - r2;
            for (int i = 0; i < 6; i++) planes[i] /= glm::length(glm::vec3(planes[i]));
        }
        vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, st.cullPipe);
        vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                st.cullPipeLayout, 0, 1, &st.cullSet, 0, nullptr);
        vkCmdPushConstants(sy.cmdBufs[fi], st.cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(planes), planes);
        vkCmdDispatch(sy.cmdBufs[fi], 1, 1, 1); // 64 потока = 64 чанка
        // 3) барьер: compute-write -> indirect-read + vertex-read (оба indirect!).
        {
            VkBufferMemoryBarrier b[3]{};
            VkBuffer bbs[3] = {tg.indBuf, tg.visBuf, tg.waterIndBuf};
            for (int i = 0; i < 3; i++) {
                b[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                b[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                b[i].buffer = bbs[i];
                b[i].offset = 0; b[i].size = VK_WHOLE_SIZE;
            }
            b[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            b[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b[2].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                 0, 0, nullptr, 3, b, 0, nullptr);
        }
        // demo-4: матрица солнца (снап в light-space + scale/bias fold, GL-рецепт).
        // Солнце фикс-полдень; квант не нужен (нет цикла дня), снап нужен (камера едет).
        glm::mat4 lightSpace;
        {
            glm::vec3 sun = glm::normalize(glm::vec3(cos(tod), sin(tod), 0.35f));
            const float SE = 70.0f;
            float texel = 2.0f * SE / (float)SHADOW_S;
            glm::vec3 L = sun;
            glm::vec3 up0 = fabs(L.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            glm::vec3 xx = glm::normalize(glm::cross(up0, L));
            glm::vec3 yx = glm::cross(L, xx);
            glm::vec3 center = camPos;
            float lx = glm::dot(center, xx), ly = glm::dot(center, yx), lz = glm::dot(center, L);
            lx = floor(lx / texel + 0.5f) * texel;
            ly = floor(ly / texel + 0.5f) * texel;
            center = xx * lx + yx * ly + L * lz;
            glm::mat4 sb(1.0f); // NDC->0..1 по всем осям (GLM даёт глубину [-1,1])
            sb = glm::translate(sb, glm::vec3(0.5f, 0.5f, 0.5f));
            sb = glm::scale(sb, glm::vec3(0.5f, 0.5f, 0.5f));
            lightSpace = sb * glm::ortho(-SE, SE, -SE, SE, 1.0f, 400.0f) *
                         glm::lookAt(center, center + L, yx);
        }
        // demo-4 shadow pass: та же видимость (indirect), только глубина.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = (tg.shadowLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                  ? 0 : VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.oldLayout = tg.shadowLayout;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            b.image = tg.shadowImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi],
                                 (tg.shadowLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                     ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                     : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            tg.shadowLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
        VkRenderingAttachmentInfo sdepth{};
        sdepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        sdepth.imageView = tg.shadowView;
        sdepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        sdepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sdepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        sdepth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo sri{};
        sri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        sri.renderArea = {{0, 0}, {(uint32_t)SHADOW_S, (uint32_t)SHADOW_S}};
        sri.layerCount = 1;
        sri.colorAttachmentCount = 0;
        sri.pDepthAttachment = &sdepth;
        vkCmdBeginRendering(sy.cmdBufs[fi], &sri);
        vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pp.shadowPipe);
        {
            VkViewport svp{0, 0, (float)SHADOW_S, (float)SHADOW_S, 0.0f, 1.0f};
            VkRect2D ssc{{0, 0}, {(uint32_t)SHADOW_S, (uint32_t)SHADOW_S}};
            vkCmdSetViewport(sy.cmdBufs[fi], 0, 1, &svp);
            vkCmdSetScissor(sy.cmdBufs[fi], 0, 1, &ssc);
        }
        vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pp.pipeLayout, 0, 1, &st.descSets[fi], 0, nullptr);
        vkCmdPushConstants(sy.cmdBufs[fi], pp.pipeLayout,
                           (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT),
                           0, sizeof(lightSpace), &lightSpace);
        vkCmdSetDepthBias(sy.cmdBufs[fi], 1.1f, 0.0f, 2.0f); // const/slope из книги
        vkCmdDrawIndirectCount(sy.cmdBufs[fi], tg.indBuf, sizeof(uint32_t) * 4, tg.indBuf, 0,
                               64, sizeof(VkDrawIndirectCommand));
        vkCmdEndRendering(sy.cmdBufs[fi]);
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = tg.shadowImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            tg.shadowLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        // demo-5a: HDR-цепочка. Небо и террейн пишут HDR, дальше compute + тонемэппинг.
        // HDR-переход (трекаем как своп).
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = (tg.hdrLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                  ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.oldLayout = tg.hdrLayout;
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = tg.hdrImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi],
                                 (tg.hdrLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                     ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                     : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            tg.hdrLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkViewport svwp{0, 0, (float)core.swapExtent.width, (float)core.swapExtent.height, 0.0f, 1.0f};
        VkRect2D ssc{{0, 0}, core.swapExtent};
        // Небо первым (без глубины, CLEAR поверх всего).
        {
            VkRenderingAttachmentInfo sky{};
            sky.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            sky.imageView = tg.hdrView;
            sky.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            sky.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            sky.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            sky.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            VkRenderingInfo sri{};
            sri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            sri.renderArea = {{0, 0}, core.swapExtent};
            sri.layerCount = 1;
            sri.colorAttachmentCount = 1;
            sri.pColorAttachments = &sky;
            vkCmdBeginRendering(sy.cmdBufs[fi], &sri);
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pp.skyPipe);
            vkCmdSetViewport(sy.cmdBufs[fi], 0, 1, &svwp);
            vkCmdSetScissor(sy.cmdBufs[fi], 0, 1, &ssc);
            vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pp.skyPipeLayout, 0, 1, &st.skySets[fi], 0, nullptr);
            glm::vec4 viewSize((float)core.swapExtent.width, (float)core.swapExtent.height, 0, 0);
            vkCmdPushConstants(sy.cmdBufs[fi], pp.skyPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(viewSize), &viewSize);
            vkCmdDraw(sy.cmdBufs[fi], 3, 1, 0, 0);
            vkCmdEndRendering(sy.cmdBufs[fi]);
        }
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = tg.hdrView; // террейн — в HDR поверх неба (LOAD!)
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = tg.depthView;
        depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // + сэмпл воды там же
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // читает вода следующим пассом
        depth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea = {{0, 0}, core.swapExtent};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        ri.pDepthAttachment = &depth;
        vkCmdBeginRendering(sy.cmdBufs[fi], &ri);
        vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pp.pipeline);
        VkViewport vwp{0, 0, (float)core.swapExtent.width, (float)core.swapExtent.height, 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, core.swapExtent};
        vkCmdSetViewport(sy.cmdBufs[fi], 0, 1, &vwp);
        vkCmdSetScissor(sy.cmdBufs[fi], 0, 1, &sc);
        vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pp.pipeLayout, 0, 1, &st.descSets[fi], 0, nullptr);
        // 4) один indirect-count draw на всё видимое (команды пишет compute).
        vkCmdPushConstants(sy.cmdBufs[fi], pp.pipeLayout,
                           (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT),
                           0, sizeof(lightSpace), &lightSpace);
        vkCmdDrawIndirectCount(sy.cmdBufs[fi], tg.indBuf, sizeof(uint32_t) * 4, tg.indBuf, 0,
                               64, sizeof(VkDrawIndirectCommand));
        vkCmdEndRendering(sy.cmdBufs[fi]);
        // demo-5x копия глубины для воды + барьеры (всё в GENERAL, только доступ).
        {
            VkImageMemoryBarrier b[2]{};
            b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b[0].image = tg.depthImg;
            b[0].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            b[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b[1].image = tg.depthCopyImg;
            b[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, b);
            VkImageCopy cp{};
            cp.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            cp.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            cp.extent = {core.swapExtent.width, core.swapExtent.height, 1};
            vkCmdCopyImage(sy.cmdBufs[fi], tg.depthImg, VK_IMAGE_LAYOUT_GENERAL,
                           tg.depthCopyImg, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
            VkImageMemoryBarrier b2[2]{};
            b2[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b2[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b2[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b2[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[0].image = tg.depthCopyImg;
            b2[0].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            b2[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b2[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b2[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b2[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[1].image = tg.depthImg;
            b2[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, b2);
        }
        // demo-6 SSAO по копии глубины террейна (вода depth ещё не писала — ей AO
        // ложится от рельефа за ней, малозаметно). Барьер transfer->compute,
        // после — compute->fragment для тонемэппа.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.image = tg.depthCopyImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, st.ssaoPipe);
            vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                    st.ssaoPipeLayout, 0, 1, &st.ssaoSet, 0, nullptr);
            struct SsaoPush { float rw, rh, rw2, rh2, zn, zf, rad, dist; };
            SsaoPush push{(float)core.swapExtent.width, (float)core.swapExtent.height,
                          1.0f / (float)core.swapExtent.width, 1.0f / (float)core.swapExtent.height,
                          0.1f, 600.0f, 0.6f, 1.5f};
            vkCmdPushConstants(sy.cmdBufs[fi], st.ssaoPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(push), &push);
            vkCmdDispatch(sy.cmdBufs[fi], (core.swapExtent.width + 15) / 16,
                          (core.swapExtent.height + 15) / 16, 1);
            VkImageMemoryBarrier b2s{};
            b2s.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b2s.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b2s.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b2s.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2s.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2s.image = st.ssaoImg;
            b2s.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b2s);
        }
        {
            VkRenderingAttachmentInfo wcol{};
            wcol.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            wcol.imageView = tg.hdrView;
            wcol.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            wcol.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            wcol.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingAttachmentInfo wdep{};
            wdep.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            wdep.imageView = tg.depthView;
            wdep.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            wdep.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            wdep.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkRenderingInfo wri{};
            wri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            wri.renderArea = {{0, 0}, core.swapExtent};
            wri.layerCount = 1;
            wri.colorAttachmentCount = 1;
            wri.pColorAttachments = &wcol;
            wri.pDepthAttachment = &wdep;
            vkCmdBeginRendering(sy.cmdBufs[fi], &wri);
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pp.waterPipe);
            vkCmdSetViewport(sy.cmdBufs[fi], 0, 1, &vwp);
            vkCmdSetScissor(sy.cmdBufs[fi], 0, 1, &sc);
            vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pp.pipeLayout, 0, 1, &st.descSets[fi], 0, nullptr);
            glm::vec2 wres((float)core.swapExtent.width, (float)core.swapExtent.height);
            vkCmdPushConstants(sy.cmdBufs[fi], pp.pipeLayout,
                               (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                                                    VK_SHADER_STAGE_FRAGMENT_BIT),
                               0, sizeof(wres), &wres);
            vkCmdDrawIndirectCount(sy.cmdBufs[fi], tg.waterIndBuf, sizeof(uint32_t) * 4, tg.waterIndBuf, 0,
                                   64, sizeof(VkDrawIndirectCommand));
            vkCmdEndRendering(sy.cmdBufs[fi]);
        }
        // demo-5a пост: HDR -> lum -> adapt -> тонемэппинг в своп.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = tg.hdrImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            tg.hdrLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        // demo-7 TAA: resolve HDR+история -> H[fi], копия назад в HDR.
        // Дальше lum/bloom/tonemap читают уже сглаженный HDR. F3 выключает.
        if (useTaa) {
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, st.taaPipe);
            vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                    st.taaPipeLayout, 0, 1, &st.taaSet[fi], 0, nullptr);
            struct TaaPush {
                float rw, rh, rw2, rh2, reset, p0, p1, p2;
            } push{(float)core.swapExtent.width, (float)core.swapExtent.height,
                   1.0f / (float)core.swapExtent.width, 1.0f / (float)core.swapExtent.height,
                   taaReset ? 1.0f : 0.0f, 0, 0, 0};
            vkCmdPushConstants(sy.cmdBufs[fi], st.taaPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(push), &push);
            vkCmdDispatch(sy.cmdBufs[fi], (core.swapExtent.width + 15) / 16,
                          (core.swapExtent.height + 15) / 16, 1);
            VkImageMemoryBarrier b2[2]{};
            b2[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b2[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b2[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b2[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[0].image = st.histImg[fi];
            b2[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b2[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b2[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b2[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b2[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b2[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b2[1].image = tg.hdrImg;
            b2[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, b2);
            VkImageCopy cp{};
            cp.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            cp.extent = {core.swapExtent.width, core.swapExtent.height, 1};
            vkCmdCopyImage(sy.cmdBufs[fi], st.histImg[fi], VK_IMAGE_LAYOUT_GENERAL,
                           tg.hdrImg, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
            VkImageMemoryBarrier b3{};
            b3.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b3.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b3.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b3.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b3.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b3.image = tg.hdrImg;
            b3.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b3);
        }
        // exp для тонемэппа: binding 1 -> только что записанный.
        // ДО всех биндов сета в кадре (апдейт после бинда инвалидирует запись)!
        // (parity объявлен ниже у adapt; здесь inline по frame.)
        {
            VkDescriptorImageInfo ei{};
            ei.sampler = tg.expSmp;
            ei.imageView = (frame % 2 == 0) ? tg.expView[1] : tg.expView[0];
            ei.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = st.postSet[fi]; w.dstBinding = 1;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &ei;
            vkUpdateDescriptorSets(core.device, 1, &w, 0, nullptr);
        }
        // demo-5b bloom-цепочка: bright HDR->A, down A->B, up B->A2(+base A).
        {
            struct BloomPass { VkPipeline pipe; VkDescriptorSet set; uint32_t w, h; };
            uint32_t hw = (core.swapExtent.width + 1) / 2, hh = (core.swapExtent.height + 1) / 2;
            uint32_t qw = (core.swapExtent.width + 3) / 4, qh = (core.swapExtent.height + 3) / 4;
            BloomPass ps[3] = {{st.brightPipe, st.bloomSet[0], hw, hh},
                               {st.kdownPipe, st.bloomSet[1], qw, qh},
                               {st.kupPipe, st.bloomSet[2], hw, hh}};
            for (int i = 0; i < 3; i++) {
                vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, ps[i].pipe);
                vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                        st.bloomPipeLayout, 0, 1, &ps[i].set, 0, nullptr);
                vkCmdDispatch(sy.cmdBufs[fi], (ps[i].w + 7) / 8, (ps[i].h + 7) / 8, 1);
                VkImageMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                b.image = (i == 0) ? st.bloomImg[0] : ((i == 1) ? st.bloomImg[1] : st.bloomImg[2]);
                b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     (i == 2) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                              : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
            }
        }
        vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, st.lumPipe);
        vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                st.postComputeLayout, 0, 1, &st.postSet[fi], 0, nullptr);
        vkCmdDispatch(sy.cmdBufs[fi], 8, 5, 1); // 64x36 тайлов
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.image = tg.lumImg;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        float parity = (frame % 2 == 0) ? 0.0f : 1.0f; // чёт: читаем A пишем B
        {
            struct AdaptPush { float dt, parity, p0, p1; } ap{dt, parity, 0, 0};
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE, st.adaptPipe);
            vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_COMPUTE,
                                    st.postComputeLayout, 0, 1, &st.postSet[fi], 0, nullptr);
            vkCmdPushConstants(sy.cmdBufs[fi], st.postComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(ap), &ap);
            vkCmdDispatch(sy.cmdBufs[fi], 1, 1, 1);
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.image = (parity < 0.5f) ? tg.expImg[1] : tg.expImg[0];
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        // (exp binding 1 обновлён до биндов выше — см. перед lum.)
        {
            VkRenderingAttachmentInfo tm{};
            tm.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            tm.imageView = core.swapViews[imgIdx];
            tm.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            tm.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            tm.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            tm.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            VkRenderingInfo tri{};
            tri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            tri.renderArea = {{0, 0}, core.swapExtent};
            tri.layerCount = 1;
            tri.colorAttachmentCount = 1;
            tri.pColorAttachments = &tm;
            vkCmdBeginRendering(sy.cmdBufs[fi], &tri);
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pp.tonemapPipe);
            vkCmdSetViewport(sy.cmdBufs[fi], 0, 1, &vwp);
            vkCmdSetScissor(sy.cmdBufs[fi], 0, 1, &sc);
            vkCmdBindDescriptorSets(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pp.tonemapPipeLayout, 0, 1, &st.postSet[fi], 0, nullptr);
            glm::vec4 res((float)core.swapExtent.width, (float)core.swapExtent.height,
                            useSsao ? 0.65f : 0.0f, 0); // F2 гасит AO
            vkCmdPushConstants(sy.cmdBufs[fi], pp.tonemapPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(res), &res);
            vkCmdDraw(sy.cmdBufs[fi], 3, 1, 0, 0);
            vkCmdEndRendering(sy.cmdBufs[fi]);
        }
        // 5) рентген карты в угол (F1): второй проход по свопу (LOAD).
        if (dbgShadow) {
            VkRenderingAttachmentInfo dg{};
            dg.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            dg.imageView = core.swapViews[imgIdx];
            dg.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            dg.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            dg.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo dri{};
            dri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            dri.renderArea = {{0, 0}, core.swapExtent};
            dri.layerCount = 1;
            dri.colorAttachmentCount = 1;
            dri.pColorAttachments = &dg;
            vkCmdBeginRendering(sy.cmdBufs[fi], &dri);
            vkCmdBindPipeline(sy.cmdBufs[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, pp.dbgPipe);
            VkViewport dvp{0, 0, 256, 256, 0.0f, 1.0f};
            VkRect2D dsc{{0, 0}, {256, 256}};
            vkCmdSetViewport(sy.cmdBufs[fi], 0, 1, &dvp);
            vkCmdSetScissor(sy.cmdBufs[fi], 0, 1, &dsc);
            vkCmdDraw(sy.cmdBufs[fi], 3, 1, 0, 0);
            vkCmdEndRendering(sy.cmdBufs[fi]);
        }
        bool wantShot = (a.shotFrame >= 0 && frame == a.shotFrame);
        if (wantShot) {
            // Ридбэк вместо present-перехода: ATTACHMENT -> TRANSFER_SRC, копия, -> PRESENT.
            VkImageMemoryBarrier b[2]{};
            b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b[0].image = core.swapImages[imgIdx];
            b[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b[0]);
            VkBufferImageCopy cp{};
            cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            cp.imageExtent = {core.swapExtent.width, core.swapExtent.height, 1};
            vkCmdCopyImageToBuffer(sy.cmdBufs[fi], core.swapImages[imgIdx],
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tg.shotBuf, 1, &cp);
            b[1] = b[0];
            b[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b[1].dstAccessMask = 0;
            b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b[1]);
        } else {
            VkImageMemoryBarrier toPresent = toDraw;
            toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toPresent.dstAccessMask = 0;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(sy.cmdBufs[fi], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &toPresent);
        }
        VK_CHECK(vkEndCommandBuffer(sy.cmdBufs[fi]));
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo sub{};
        sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &sy.acquireSem[fi];
        sub.pWaitDstStageMask = &waitStage;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &sy.cmdBufs[fi];
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &sy.renderSem[imgIdx];
        VK_CHECK(vkQueueSubmit(core.gfxQueue, 1, &sub, sy.frameFence[fi]));
        sy.imgLayout[imgIdx] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkPresentInfoKHR pr{};
        pr.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pr.waitSemaphoreCount = 1;
        pr.pWaitSemaphores = &sy.renderSem[imgIdx];
        pr.swapchainCount = 1;
        pr.pSwapchains = &core.swapchain;
        pr.pImageIndices = &imgIdx;
        VK_CHECK(vkQueuePresentKHR(core.gfxQueue, &pr));
        if (wantShot) {
            VK_CHECK(vkWaitForFences(core.device, 1, &sy.frameFence[fi], VK_TRUE, 1000000000ull));
            void* px = nullptr;
            VK_CHECK(vmaMapMemory(core.alloc, tg.shotAlloc, &px));
            FILE* f = fopen("shot.tga", "wb");
            if (f) {
                int W = (int)core.swapExtent.width, H = (int)core.swapExtent.height;
                unsigned char hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    (unsigned char)(W & 255), (unsigned char)(W >> 8),
                    (unsigned char)(H & 255), (unsigned char)(H >> 8), 32, 0x20};
                fwrite(hdr, 1, 18, f);
                fwrite(px, 1, (size_t)W * H * 4, f); // BGRA сверху вниз (0x20)
                fclose(f);
                printf("shot saved frame %d\n", frame);
            }
            vmaUnmapMemory(core.alloc, tg.shotAlloc);
        }
        if (tg.waterDbg && frame == 5) {
            vkDeviceWaitIdle(core.device);
            core.immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = 16 + 64 * 16;
                vkCmdCopyBuffer(cb, tg.waterIndBuf, tg.dbgReadBuf, 1, &cp);
            });
            void* dpx = nullptr;
            VK_CHECK(vmaMapMemory(core.alloc, tg.dbgReadAlloc, &dpx));
            uint32_t* u = (uint32_t*)dpx;
            printf("WATERDBG waterInd count=%u cmd0=(%u,%u,%u,%u)\n", u[0], u[4], u[5], u[6], u[7]);
            {
                uint32_t total = 0;
                printf("WATERDBG slots:");
                for (int s = 0; s < 64; s++) {
                    uint32_t inst = u[4 + s * 4 + 1], fi_ = u[4 + s * 4 + 3];
                    total += inst;
                    if (inst) printf(" [%d]i=%u,fi=%u", s, inst, fi_);
                }
                printf(" totalInst=%u\n", total);
            }
            vmaUnmapMemory(core.alloc, tg.dbgReadAlloc);
            core.immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = 64;
                vkCmdCopyBuffer(cb, tg.visBuf, tg.dbgReadBuf, 1, &cp);
            });
            VK_CHECK(vmaMapMemory(core.alloc, tg.dbgReadAlloc, &dpx));
            u = (uint32_t*)dpx;
            printf("WATERDBG vis0-7: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                   u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]);
            vmaUnmapMemory(core.alloc, tg.dbgReadAlloc);
            core.immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = 64;
                vkCmdCopyBuffer(cb, tg.waterMetaBuf, tg.dbgReadBuf, 1, &cp);
            });
            VK_CHECK(vmaMapMemory(core.alloc, tg.dbgReadAlloc, &dpx));
            u = (uint32_t*)dpx;
            printf("WATERDBG wmeta0-3: off=%u cnt=%u ox=%f oz=%f | off=%u cnt=%u\n",
                   u[0], u[1], *(float*)&u[2], *(float*)&u[3], u[4], u[5]);
            vmaUnmapMemory(core.alloc, tg.dbgReadAlloc);
            core.immRun([&](VkCommandBuffer cb) {
                VkBufferCopy cp{};
                cp.size = 64;
                vkCmdCopyBuffer(cb, tg.indBuf, tg.dbgReadBuf, 1, &cp);
            });
            VK_CHECK(vmaMapMemory(core.alloc, tg.dbgReadAlloc, &dpx));
            u = (uint32_t*)dpx;
            printf("WATERDBG terrainInd count=%u cmd0=(%u,%u,%u,%u)\n", u[0], u[4], u[5], u[6], u[7]);
            vmaUnmapMemory(core.alloc, tg.dbgReadAlloc);
        }
        frame++; drawn++; fpsN++;
        if (now - fpsT >= 2.0) {
            printf("fps %.0f (%.2f ms)\n", fpsN / (now - fpsT), (now - fpsT) * 1000.0 / fpsN);
            fpsT = now; fpsN = 0;
        }
        if (a.maxFrames > 0 && drawn >= a.maxFrames) break;
    }
    return drawn;
}
