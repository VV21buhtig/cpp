// vulcan-render: воксели. Мир/мешер/логика — из GL-прототипа как есть,
// рендер — Vulkan 1.3. main — только оркестрация: vk/*.cpp делают всё.
// Управление: WASD+стрелки, Space/C вверх/вниз.
#include "vk/vk_ctx.h"
#include "vk/targets.h"
#include "vk/descriptors.h"
#include "vk/pipelines.h"
#include "vk/frame.h"

#include "engine/world.h"
#include "engine/blocks.h"
#include "mesh_vk.h"
#include "sky_atmo.h"

#include <cmath>
#include <cstring>

int main(int argc, char** argv) {
    int maxFrames = -1;
    int shotFrame = -1; // --shot K: сохранить кадр K в shot.tga (свой рентген)
    bool camOverride = false;
    glm::vec3 camPosOvr(0.0f);
    float yawOvr = 0.0f, pitchOvr = 0.0f;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shotFrame = atoi(argv[++i]);
        if (!strcmp(argv[i], "--cam") && i + 5 < argc) {
            camOverride = true;
            camPosOvr = glm::vec3((float)atof(argv[i+1]), (float)atof(argv[i+2]),
                                  (float)atof(argv[i+3]));
            yawOvr = (float)atof(argv[i+4]);
            pitchOvr = (float)atof(argv[i+5]);
            i += 5;
        }
    }

    // ---- мир (та же генерация что в игре, сид 1337) ----
    World world;
    world.init(8, 8, 1337);
    const int W = world.sizeX();
    const glm::vec3 worldOffset(-W / 2.0f, 0.0f, -W / 2.0f);

    VkCore core;
    if (!vkInitCore(core)) return 1;

    Targets t;
    makeTargets(core, world, worldOffset, t);

    Sets st;
    makeSets(core, t, st);

    Pipes pp;
    makePipes(core, st, pp);

    FrameArgs fa;
    fa.maxFrames = maxFrames;
    fa.shotFrame = shotFrame;
    fa.camOverride = camOverride;
    fa.camPosOvr = camPosOvr;
    fa.yawOvr = yawOvr;
    fa.pitchOvr = pitchOvr;
    FrameSync fsy;
    makeSync(core, fsy);
    int drawn = runFrameLoop(core, world, worldOffset, t, st, pp, fsy, fa);
    printf("demo-2 OK: %d frames\n", drawn);
    vkDeviceWaitIdle(core.device);
    core.del.flush();
    glfwDestroyWindow(core.window);
    glfwTerminate();
    return 0;
}
