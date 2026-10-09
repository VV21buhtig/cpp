// Печка неба: см. sky_lut.h. Все проходы — фулскрин-треугольник, draw(3).
#include "sky_lut.h"
#include "sdf_wgsl.h"
#include <string.h>
#include <math.h>

void sky_body_cols(Vec3 sunDir, Vec3 *sunCol, Vec3 *moonCol) {
    float dayF = sunDir.y > 1.0f ? 1.0f : (sunDir.y < -1.0f ? -1.0f : sunDir.y);
    float k = dayF * 2.0f;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    sunCol->x = 1.0f + (1.25f - 1.0f) * k;
    sunCol->y = 0.45f + (1.21f - 0.45f) * k;
    sunCol->z = 0.20f + (1.12f - 0.20f) * k;
    moonCol->x = 0.95f; moonCol->y = 0.96f; moonCol->z = 1.0f;
}

static WGPUTexture layer_tex(WGPUDevice dev, int w, int h) {
    WGPUTextureDescriptor d;
    memset(&d, 0, sizeof d);
    d.size.width = (uint32_t)w;
    d.size.height = (uint32_t)h;
    d.size.depthOrArrayLayers = 1;
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    d.dimension = WGPUTextureDimension_2D;
    d.format = WGPUTextureFormat_RGBA16Float;
    d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    return wgpuDeviceCreateTexture(dev, &d);
}

static WGPUTextureView layer_view(WGPUTexture t) {
    WGPUTextureViewDescriptor d;
    memset(&d, 0, sizeof d);
    d.format = WGPUTextureFormat_RGBA16Float;
    d.dimension = WGPUTextureViewDimension_2D;
    d.mipLevelCount = 1;
    d.arrayLayerCount = 1;
    d.aspect = WGPUTextureAspect_All;
    return wgpuTextureCreateView(t, &d);
}

static WGPUShaderModule bake_mod(WGPUDevice dev, const char *src) {
    WGPUShaderSourceWGSL w;
    memset(&w, 0, sizeof w);
    w.chain.sType = WGPUSType_ShaderSourceWGSL;
    w.code = (WGPUStringView){src, strlen(src)};
    WGPUShaderModuleDescriptor d;
    memset(&d, 0, sizeof d);
    d.nextInChain = (const WGPUChainedStruct *)&w;
    return wgpuDeviceCreateShaderModule(dev, &d);
}

static WGPURenderPipeline bake_pipe(WGPUDevice dev, WGPUShaderModule mod) {
    WGPUColorTargetState color;
    memset(&color, 0, sizeof color);
    color.format = WGPUTextureFormat_RGBA16Float;
    color.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState frag;
    memset(&frag, 0, sizeof frag);
    frag.module = mod;
    frag.entryPoint = (WGPUStringView){"fs", 2};
    frag.targetCount = 1;
    frag.targets = &color;
    WGPUVertexState vert;
    memset(&vert, 0, sizeof vert);
    vert.module = mod;
    vert.entryPoint = (WGPUStringView){"vs", 2};
    WGPURenderPipelineDescriptor p;
    memset(&p, 0, sizeof p);
    p.vertex = vert;
    p.fragment = &frag;
    p.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    p.primitive.frontFace = WGPUFrontFace_CCW;
    p.primitive.cullMode = WGPUCullMode_None;
    p.multisample.count = 1;
    p.multisample.mask = 0xFFFFFFFFu;
    return wgpuDeviceCreateRenderPipeline(dev, &p);
}

static WGPUBuffer ubo_buf(WGPUDevice dev, size_t n) {
    WGPUBufferDescriptor d;
    memset(&d, 0, sizeof d);
    d.size = (uint64_t)n;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(dev, &d);
}

// Один fullscreen-проход в текстуру. Возвращает энкодер-кусок через общий submit caller'а.
static void bake_draw(WGPUDevice dev, WGPUQueue q, WGPURenderPipeline pipe,
                      WGPUBindGroup bind, WGPUTextureView target) {
    WGPUCommandEncoderDescriptor ed;
    memset(&ed, 0, sizeof ed);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(dev, &ed);
    WGPURenderPassColorAttachment ca;
    memset(&ca, 0, sizeof ca);
    ca.view = target;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0, 0, 0, 1};
    WGPURenderPassDescriptor rp;
    memset(&rp, 0, sizeof rp);
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, pipe);
    if (bind) wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor bd;
    memset(&bd, 0, sizeof bd);
    WGPUCommandBuffer buf = wgpuCommandEncoderFinish(enc, &bd);
    wgpuQueueSubmit(q, 1, &buf);
    wgpuCommandBufferRelease(buf);
    wgpuCommandEncoderRelease(enc);
}

static WGPUBindGroup tex2_bind(WGPUDevice dev, WGPURenderPipeline pipe,
                               WGPUTextureView t0, WGPUTextureView t1,
                               WGPUSampler smp, WGPUBuffer ubo, size_t uboSize) {
    WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(pipe, 0);
    WGPUBindGroupEntry e[4];
    memset(e, 0, sizeof e);
    e[0].binding = 0;
    e[0].textureView = t0;
    e[1].binding = 1;
    if (t1) e[1].textureView = t1;
    else e[1].sampler = smp;
    int n = 2;
    if (t1) {
        e[2].binding = 2;
        e[2].sampler = smp;
        n = 3;
    }
    if (ubo) {
        e[n].binding = (uint32_t)(t1 ? 3 : 1);
        if (!t1) e[n].binding = 1;
        e[n].buffer = ubo;
        e[n].size = (uint64_t)uboSize;
        n++;
    }
    WGPUBindGroupDescriptor d;
    memset(&d, 0, sizeof d);
    d.layout = l;
    d.entryCount = (size_t)n;
    d.entries = e;
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(dev, &d);
    wgpuBindGroupLayoutRelease(l);
    return g;
}

void sky_luts_init(SkyLuts *s, WGPUDevice dev, WGPUQueue q) {
    memset(s, 0, sizeof *s);
    s->dev = dev;
    s->queue = q;
    WGPUSamplerDescriptor sd;
    memset(&sd, 0, sizeof sd);
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.maxAnisotropy = 1;
    s->smp = wgpuDeviceCreateSampler(dev, &sd);

    s->transTex = layer_tex(dev, SKY_TW, SKY_TH);
    s->msTex = layer_tex(dev, SKY_MW, SKY_MH);
    s->sunTex = layer_tex(dev, SKY_VW, SKY_VH);
    s->moonTex = layer_tex(dev, SKY_VW, SKY_VH);
    s->ambTex = layer_tex(dev, 6, 1);
    s->transView = layer_view(s->transTex);
    s->msView = layer_view(s->msTex);
    s->sunView = layer_view(s->sunTex);
    s->moonView = layer_view(s->moonTex);
    s->ambView = layer_view(s->ambTex);

    s->transMod = bake_mod(dev, SKY_TRANS_WGSL);
    s->msMod = bake_mod(dev, SKY_MS_WGSL);
    s->viewMod = bake_mod(dev, SKY_VIEW_WGSL);
    s->ambMod = bake_mod(dev, SKY_AMB_WGSL);
    s->transPipe = bake_pipe(dev, s->transMod);
    s->msPipe = bake_pipe(dev, s->msMod);
    s->viewPipe = bake_pipe(dev, s->viewMod);
    s->ambPipe = bake_pipe(dev, s->ambMod);
    s->viewUbo = ubo_buf(dev, 32);
    s->ambUbo = ubo_buf(dev, 64);

    // ms: trans + sampler
    s->msBind = tex2_bind(dev, s->msPipe, s->transView, 0, s->smp, 0, 0);

    // trans+ms один раз (порядок очереди гарантирует видимость)
    bake_draw(dev, q, s->transPipe, 0, s->transView);
    bake_draw(dev, q, s->msPipe, s->msBind, s->msView);
    s->lutsBuilt = 1;
    s->skyValid = 0;
}

void sky_luts_update(SkyLuts *s, Vec3 sunDir, Vec3 moonDir, float camY) {
    float altSun = asinf(sunDir.y > 1.0f ? 1.0f : (sunDir.y < -1.0f ? -1.0f : sunDir.y));
    float altMoon = asinf(moonDir.y > 1.0f ? 1.0f : (moonDir.y < -1.0f ? -1.0f : moonDir.y));
    float dh = camY - s->camH;
    if (dh < 0) dh = -dh;
    int moved = !s->skyValid || dh > 1.0f;
    Vec3 bodies[2] = {sunDir, moonDir};
    float *cached[2] = {&s->altSun, &s->altMoon};
    WGPUTextureView targets[2] = {s->sunView, s->moonView};
    for (int b = 0; b < 2; b++) {
        float alt = b ? altMoon : altSun;
        float dd = alt - *cached[b];
        if (dd < 0) dd = -dd;
        if (!moved && dd <= 1e-3f) continue;
        *cached[b] = alt;
        float ubo[8] = {bodies[b].x, bodies[b].y, bodies[b].z, 0.0f, camY, 0, 0, 0};
        wgpuQueueWriteBuffer(s->queue, s->viewUbo, 0, ubo, sizeof ubo);
        WGPUBindGroup bg;
        // Привязки: trans, ms, sampler, viewUbo.
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(s->viewPipe, 0);
            WGPUBindGroupEntry e[4];
            memset(e, 0, sizeof e);
            e[0].binding = 0; e[0].textureView = s->transView;
            e[1].binding = 1; e[1].textureView = s->msView;
            e[2].binding = 2; e[2].sampler = s->smp;
            e[3].binding = 3; e[3].buffer = s->viewUbo; e[3].size = 32;
            WGPUBindGroupDescriptor d;
            memset(&d, 0, sizeof d);
            d.layout = l;
            d.entryCount = 4;
            d.entries = e;
            bg = wgpuDeviceCreateBindGroup(s->dev, &d);
            wgpuBindGroupLayoutRelease(l);
        bake_draw(s->dev, s->queue, s->viewPipe, bg, targets[b]);
        wgpuBindGroupRelease(bg);
    }
    if (moved) s->camH = camY;
    s->skyValid = 1;
    // ambient следом (6px, дёшево): sun/moon dirs+cols
    {
        Vec3 sunCol, moonCol;
        sky_body_cols(sunDir, &sunCol, &moonCol);
        float ubo[16] = {
            sunDir.x, sunDir.y, sunDir.z, 0,
            sunCol.x, sunCol.y, sunCol.z, 0,
            moonDir.x, moonDir.y, moonDir.z, 0,
            moonCol.x, moonCol.y, moonCol.z, 0,
        };
        wgpuQueueWriteBuffer(s->queue, s->ambUbo, 0, ubo, sizeof ubo);
        WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(s->ambPipe, 0);
        WGPUBindGroupEntry e[4];
        memset(e, 0, sizeof e);
        e[0].binding = 0; e[0].textureView = s->sunView;
        e[1].binding = 1; e[1].textureView = s->moonView;
        e[2].binding = 2; e[2].sampler = s->smp;
        e[3].binding = 3; e[3].buffer = s->ambUbo; e[3].size = 64;
        WGPUBindGroupDescriptor d;
        memset(&d, 0, sizeof d);
        d.layout = l;
        d.entryCount = 4;
        d.entries = e;
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(s->dev, &d);
        wgpuBindGroupLayoutRelease(l);
        bake_draw(s->dev, s->queue, s->ambPipe, bg, s->ambView);
        wgpuBindGroupRelease(bg);
    }
}
