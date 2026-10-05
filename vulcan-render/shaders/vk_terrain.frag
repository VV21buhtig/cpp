#version 450
// Порт lighting.fs (GL-прототип, P1e/P2f/P2g/P2j впитаны), БЕЗ shadow-map
// (тени — demo-4 с настоящей картой). Point/spot выкинуты (игровые лампы — позже).
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 sunDir;
    vec4 sunCol;
    vec4 ambSky;
    vec4 ambGnd;
    vec4 fog;
    vec4 misc;
    vec4 viewPos;
} frame;
layout(set = 0, binding = 1) uniform sampler2DArray tiles;
// demo-4: compare-сэмплер (железный 2x2 PCF на тап) + ручной 3x3 поверх.
layout(set = 0, binding = 5) uniform sampler2DShadow shadowMap;
// demo-8: объём плотности (solid=1, листва=0.5) для RT AO.
layout(set = 0, binding = 10) uniform sampler3D occTex;
layout(push_constant) uniform PushOcc {
    layout(offset = 64) vec4 volMinK;
    layout(offset = 80) vec4 volSize;
    layout(offset = 96) float noShadow; // F6
} occ;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vTile;
layout(location = 4) in float vAO;
layout(location = 5) in vec4 vShadow;
layout(location = 0) out vec4 outColor;

// demo-4 PCF3x3 (рецепт Ch10/11) + фейды из GL-опыта (grazing/край/даль).
// lightSpace БЕЗ sb (иначе карта в четверти!): clip [-1,1] -> UV [0,1] здесь.
// z тоже маппим (*0.5+0.5): GLM даёт [-1,1], ZERO_TO_ONE у нас нет.
float calcShadow(vec4 sp, vec3 norm, vec3 sunDir, vec3 camPos, vec3 fragPos)
{
    vec3 p = sp.xyz / sp.w;
    p.xy = p.xy * 0.5 + 0.5;
    p.z = p.z * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float s = 0.0;
    vec2 t = vec2(1.0 / 2048.0);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            s += texture(shadowMap, vec3(p.xy + vec2(i, j) * t, p.z));
    s /= 9.0;
    float ndl = max(dot(norm, sunDir), 0.0);
    float gFade = smoothstep(0.0, 0.2, ndl);
    float eFade = smoothstep(0.0, 0.10, p.x) * smoothstep(1.0, 0.90, p.x) *
                  smoothstep(0.0, 0.10, p.y) * smoothstep(1.0, 0.90, p.y);
    float cd = length(camPos - fragPos);
    float dFade = 1.0 - smoothstep(25.0, 60.0, cd);
    return mix(1.0, s, gFade * eFade * dFade);
}

// demo-8 RT AO: 6 лучей полусферы x 3 шага по объёму плотности.
// Старт в полвокселя от поверхности (свою грань не цепляем).
float rtAO(vec3 pos, vec3 n) {
    vec3 uvw0 = (pos - occ.volMinK.xyz + 0.5) / occ.volSize.xyz;
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t0 = normalize(cross(up, n));
    vec3 t1 = cross(n, t0);
    vec3 dirs[6];
    dirs[0] = n;
    dirs[1] = normalize(n + t0); dirs[2] = normalize(n - t0);
    dirs[3] = normalize(n + t1); dirs[4] = normalize(n - t1);
    dirs[5] = normalize(n + t0 + t1);
    vec3 voxUv = 1.0 / occ.volSize.xyz;
    float o = 0.0;
    for (int i = 0; i < 6; i++)
        for (int s = 1; s <= 3; s++)
            o += texture(occTex, uvw0 + dirs[i] * (float(s) * 1.1) * voxUv).r / float(s * s);
    return clamp(1.0 - o * 0.55, 0.35, 1.0); // пол 0.35: небо не гаснет в ноль
}

void main() {
    vec3 norm = normalize(vNrm);
    vec3 sunL = normalize(frame.sunDir.xyz);
    if (occ.volSize.w > 0.5) { // F5: куда светит солнце (не освещение!)
        outColor = vec4(dot(norm, sunL) > 0.0 ? vec3(0.1, 0.8, 0.1) : vec3(0.9, 0.1, 0.1), 1.0);
        return;
    }
    vec3 viewDir = normalize(frame.viewPos.xyz - vPos);
    // MC-шейдинг по осям: верх 1.0, низ 0.5, X 0.6, Z 0.8
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.5)
                                     : (abs(norm.x) > abs(norm.z) ? 0.6 : 0.8);
    float aoV = clamp(vAO / 3.0, 0.0, 1.0);
    float aoC = 0.5 + 0.5 * aoV * aoV; // MC-мягкое
    float rt = rtAO(vPos, norm);
    aoC *= mix(1.0, rt, occ.volMinK.w); // demo-8 RT AO поверх вершинного (F4)

    vec4 tileTexA = texture(tiles, vec3(vUV, vTile));
    vec3 tileTex = vec3(tileTexA);
    if (vTile > 4.5 && vTile < 5.5) { // лава светится сама (линейно в HDR!)
        outColor = vec4(tileTex * 1.8, 1.0);
        return;
    }
    // demo-9 A2C: листва пишет SHARPENED coverage (рецепт книги Ch10/03 + bgolus):
    // coverage = clamp((a-cutoff)/max(thickness*fwidth(a),eps)+0.5).
    // Сырая альфа на мипах тает в серое 0.5 и кипит — fwidth держит край чётким
    // на любой дистанции. Остальные пишут 1.0 (полное покрытие).
    float alpha = 1.0;
    if (vTile > 5.5 && vTile < 6.5 && frame.misc.z > 0.5) { // F7: A2C выкл = полное покрытие
        float aa = fwidth(tileTexA.a);
        alpha = clamp((tileTexA.a - 0.5) / max(4.0 * aa, 0.0001) + 0.5, 0.0, 1.0);
    }

    float ndl = max(dot(norm, sunL), 0.0);
    vec3 amb = mix(frame.ambGnd.rgb, frame.ambSky.rgb, norm.y * 0.5 + 0.5) * tileTex;
    vec3 direct = frame.sunCol.rgb * ndl * tileTex;
    // demo-4: тень гасит ТОЛЬКО прямой свет, ambient живёт (иначе чернота).
    float sh = calcShadow(vShadow, norm, sunL, frame.viewPos.xyz, vPos);
    if (occ.noShadow > 0.5) sh = 1.0; // F6: карта теней выкл (диагностика!)
    // demo-4: тень гасит ТОЛЬКО прямой свет, ambient живёт (иначе чернота).
    // fshade на ambient половинный: небесный свет полусферический, не направленный.
    vec3 shaded = (amb * mix(1.0, fshade, 0.5) + direct * sh * fshade) * aoC;
    // Под водой темнее и синее (SEA=20 константа мира): иначе песок светит
    // сквозь воду белым. Пещеры с воздухом ниже моря тоже темнеют — как в MC.
    float uw = clamp((20.0 - vPos.y) / 8.0, 0.0, 1.0);
    shaded *= 1.0 - uw * 0.55;
    shaded = mix(shaded, shaded * vec3(0.45, 0.75, 1.1), uw);
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, frame.misc.y);
    float fd = length(frame.viewPos.xyz - vPos);
    float ff = clamp((fd - frame.misc.x) / (frame.fog.w - frame.misc.x), 0.0, 1.0);
    vec3 col = mix(shaded, frame.fog.rgb, ff);
    outColor = vec4(col, alpha); // линейно в HDR; гамма — в самом конце (tonemap)
}
