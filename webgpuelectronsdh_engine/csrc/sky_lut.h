#ifndef SKY_LUT_H
#define SKY_LUT_H
// Печка неба из K: trans 256x64 + ms 32x32 один раз, skyview 128x72 x2
// при смене высоты тела >1e-3 / высоты камеры >1.0, ambient 6x1 следом.
// Форматы HDR (Rgba16Float), семплер clamp/linear общий.
#include <webgpu/webgpu.h>
#include "sdf_math.h"

#define SKY_TW 256
#define SKY_TH 64
#define SKY_MW 32
#define SKY_MH 32
#define SKY_VW 96
#define SKY_VH 54

typedef struct {
    WGPUDevice dev;
    WGPUQueue queue;
    WGPUSampler smp;
    WGPUTexture transTex, msTex, sunTex, moonTex, ambTex;
    WGPUTextureView transView, msView, sunView, moonView, ambView;
    WGPUShaderModule transMod, msMod, viewMod, ambMod;
    WGPURenderPipeline transPipe, msPipe, viewPipe, ambPipe;
    WGPUBindGroup msBind, ambBind;
    WGPUBuffer viewUbo, ambUbo; // 32Б / 64Б
    int lutsBuilt;
    float altSun, altMoon, camH;
    int skyValid;
} SkyLuts;

void sky_luts_init(SkyLuts *s, WGPUDevice dev, WGPUQueue q);
int sky_luts_update(SkyLuts *s, Vec3 sunDir, Vec3 moonDir, float camY); // 1 если пекла
// Цвета тел для ambient/LUT (палитра сцены, как sunCol в 30_sky.wgsl).
void sky_body_cols(Vec3 sunDir, Vec3 *sunCol, Vec3 *moonCol);
#endif
