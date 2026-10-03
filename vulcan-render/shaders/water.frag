#version 450
// demo-5x вода по глубине сцены (K-рецепт поглощения): мелко — плитка и дно,
// глубоко — тёмная синь. Глубина из копии буфера глубины (фидбэк запрещён!).
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
layout(set = 0, binding = 9) uniform sampler2D sceneDepth;

layout(push_constant) uniform Push { vec2 res; } pc;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vAO;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 norm = normalize(vNrm);
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.5)
                                     : (abs(norm.x) > abs(norm.z) ? 0.6 : 0.8);
    float aoV = clamp(vAO / 3.0, 0.0, 1.0);
    float aoC = 0.5 + 0.5 * aoV * aoV;

    vec3 tileTex = vec3(texture(tiles, vec3(vUV, 4.0)));
    tileTex = mix(vec3(dot(tileTex, vec3(0.3333))), tileTex, 1.8);
    // Толщина воды: сцена за пикселем минус сам пиксель (линейные дистанции).
    // Глубина в цели [0,1] от NDC[-1,1] (GLM), near 0.1 far 600.
    float dz = texture(sceneDepth, gl_FragCoord.xy / pc.res).r * 2.0 - 1.0;
    float sceneZ = (2.0 * 0.1 * 600.0) / (600.0 + 0.1 - dz * (600.0 - 0.1));
    float waterZ = length(frame.viewPos.xyz - vPos);
    float thick = clamp(sceneZ - waterZ, 0.0, 30.0);
    // Тело воды: мелко плитка, глубоко тёмная синь (микс по глубине —
    // поглощение K: красный дохнет первым, остаётся синь).
    float wdeep = clamp(thick / 6.0, 0.0, 1.0);
    vec3 deepCol = vec3(0.03, 0.12, 0.28);
    vec3 bodyTex = mix(tileTex, deepCol, wdeep);
    vec3 sunL = normalize(frame.sunDir.xyz);
    float ndl = max(dot(norm, sunL), 0.0);
    vec3 amb = mix(frame.ambGnd.rgb, frame.ambSky.rgb, norm.y * 0.5 + 0.5) * bodyTex;
    vec3 direct = frame.sunCol.rgb * ndl * bodyTex * 0.3;
    vec3 result = amb + direct;
    // Стенки мутные (иначе glass-танк): свет гаснет с глубиной, аппроксимация.
    if (abs(norm.y) < 0.9) result *= 0.55;

    vec3 shaded = result * fshade * aoC;
    // Под водой темнее и синее (как террейн): глубина гасит свет.
    float uw = clamp((20.0 - vPos.y) / 8.0, 0.0, 1.0);
    shaded *= 1.0 - uw * 0.55;
    shaded = mix(shaded, shaded * vec3(0.45, 0.75, 1.1), uw);
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, frame.misc.y);
    float fd = length(frame.viewPos.xyz - vPos);
    float ff = clamp((fd - frame.misc.x) / (frame.fog.w - frame.misc.x), 0.0, 1.0);
    vec3 col = mix(shaded, frame.fog.rgb, ff);
    float alpha = mix(0.55, 0.92, wdeep); // мелко прозрачнее, глубоко глухо
    outColor = vec4(col, alpha);
}
