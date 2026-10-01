#version 450
// Порт lighting.fs (GL-прототип, P1e/P2f/P2g/P2j впитаны), БЕЗ shadow-map
// (тени — demo-4 с настоящей картой). Point/spot выкинуты (игровые лампы — позже).
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 sunDir;
    vec4 sunCol;
    vec4 ambSky;
    vec4 ambGnd;
    vec4 fog;
    vec4 misc;
    vec4 viewPos;
} frame;
layout(set = 0, binding = 1) uniform sampler2DArray tiles;

layout(push_constant) uniform Push { mat4 model; } pc;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vTile;
layout(location = 4) in float vAO;
layout(location = 0) out vec4 outColor;

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
    if (vTile > 4.5 && vTile < 5.5) { // лава светится сама
        outColor = vec4(pow(tileTex * 1.8, vec3(1.0 / frame.misc.z)), 1.0);
        return;
    }

    vec3 sunL = normalize(frame.sunDir.xyz);
    float ndl = max(dot(norm, sunL), 0.0);
    vec3 amb = mix(frame.ambGnd.rgb, frame.ambSky.rgb, norm.y * 0.5 + 0.5) * tileTex;
    vec3 direct = frame.sunCol.rgb * ndl * tileTex;
    // Флуда нет: солнце напрямую, ambient весь (тени — shadowmap, окклюзия — RT AO).
    vec3 result = amb + direct;

    vec3 shaded = result * fshade * aoC;
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, frame.misc.y);
    float fd = length(frame.viewPos.xyz - vPos);
    float ff = clamp((fd - frame.misc.x) / (frame.fog.w - frame.misc.x), 0.0, 1.0);
    vec3 col = mix(shaded, frame.fog.rgb, ff);
    outColor = vec4(pow(col, vec3(1.0 / frame.misc.z)), 1.0);
}
