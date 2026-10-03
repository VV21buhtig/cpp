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

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vTile;
layout(location = 4) in float vAO;
layout(location = 5) in vec4 vShadow;
layout(location = 0) out vec4 outColor;

// demo-4 PCF3x3 (рецепт Ch10/11) + фейды из GL-опыта (grazing/край/даль).
float calcShadow(vec4 sp, vec3 norm, vec3 sunDir, vec3 camPos, vec3 fragPos)
{
    vec3 p = sp.xyz / sp.w; // scale/bias уже в матрице
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float s = 0.0;
    vec2 t = vec2(1.0 / 2048.0);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            s += texture(shadowMap, vec3(p.xy + vec2(i, j) * t, p.z));
    s /= 9.0;
    float ndl = max(dot(norm, sunDir), 0.0);
    float gFade = smoothstep(0.0, 0.2, ndl);
    float eFade = smoothstep(0.0, 0.05, p.x) * smoothstep(1.0, 0.95, p.x) *
                  smoothstep(0.0, 0.05, p.y) * smoothstep(1.0, 0.95, p.y);
    float cd = length(camPos - fragPos);
    float dFade = 1.0 - smoothstep(25.0, 60.0, cd);
    return mix(1.0, s, gFade * eFade * dFade);
}

void main() {
    vec3 norm = normalize(vNrm);
    vec3 viewDir = normalize(frame.viewPos.xyz - vPos);
    // MC-шейдинг по осям: верх 1.0, низ 0.5, X 0.6, Z 0.8
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.5)
                                     : (abs(norm.x) > abs(norm.z) ? 0.6 : 0.8);
    float aoV = clamp(vAO / 3.0, 0.0, 1.0);
    float aoC = 0.5 + 0.5 * aoV * aoV; // MC-мягкое

    vec4 tileTexA = texture(tiles, vec3(vUV, vTile));
    if (vTile > 5.5 && vTile < 6.5 && tileTexA.a < 0.5) discard;
    vec3 tileTex = vec3(tileTexA);
    if (vTile > 4.5 && vTile < 5.5) { // лава светится сама (линейно в HDR!)
        outColor = vec4(tileTex * 1.8, 1.0);
        return;
    }

    vec3 sunL = normalize(frame.sunDir.xyz);
    float ndl = max(dot(norm, sunL), 0.0);
    vec3 amb = mix(frame.ambGnd.rgb, frame.ambSky.rgb, norm.y * 0.5 + 0.5) * tileTex;
    vec3 direct = frame.sunCol.rgb * ndl * tileTex;
    // demo-4: тень гасит ТОЛЬКО прямой свет, ambient живёт (иначе чернота).
    float sh = calcShadow(vShadow, norm, sunL, frame.viewPos.xyz, vPos);
    vec3 result = amb + direct * sh;

    vec3 shaded = result * fshade * aoC;
    // Под водой темнее и синее (SEA=20 константа мира): иначе песок светит
    // сквозь воду белым. Пещеры с воздухом ниже моря тоже темнеют — как в MC.
    float uw = clamp((20.0 - vPos.y) / 8.0, 0.0, 1.0);
    shaded *= 1.0 - uw * 0.55;
    shaded = mix(shaded, shaded * vec3(0.45, 0.75, 1.1), uw);
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, frame.misc.y);
    float fd = length(frame.viewPos.xyz - vPos);
    float ff = clamp((fd - frame.misc.x) / (frame.fog.w - frame.misc.x), 0.0, 1.0);
    vec3 col = mix(shaded, frame.fog.rgb, ff);
    outColor = vec4(col, 1.0); // линейно в HDR; гамма — в самом конце (tonemap)
}
