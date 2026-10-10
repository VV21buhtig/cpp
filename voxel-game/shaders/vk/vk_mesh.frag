#version 450
// Свет — их lighting.fs (V): fshade по осям, AO 0.5+0.5a^2, туман, гамма.
// Тени припаркованы (shadowOn=0), лампы/фонарь нули — честный минимум дня.
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 sunDir;
    vec4 ambSky;
    vec4 ambGnd;
    vec4 fogColor;
    vec4 fogSat;
    vec4 viewPos;
    vec4 sunDiff;
    vec4 sunSpec;
} fr;
layout(set = 0, binding = 1) uniform sampler2DArray tiles;
layout(set = 0, binding = 2) uniform sampler2D specMap;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vTile;
layout(location = 4) in float vAO;
layout(location = 0) out vec4 outColor;

// SDF-тени СНЯТЫ (не чинить маршем!): точный EDT + linear скругляют углы
// вокселей в сферы, и любой марш рисует круглые пятна вместо направленных
// теней. Честный путь — shadowmap (как их shadow.vert/frag) или RT.
// SDF-бейк (vox_sdf) живёт дальше как данные для будущего DFAO/коллизий.
void main() {
    vec3 n = normalize(vNrm);
    vec4 tx = texture(tiles, vec3(vUV, vTile));
    if (vTile > 5.5 && vTile < 6.5 && tx.a < 0.5) discard; // листва с дырками
    vec3 texel = tx.rgb;
    float gamma = fr.fogColor.w;
    // Лава светится сама (tile 5).
    if (vTile > 4.5 && vTile < 5.5) {
        float fd0 = length(fr.viewPos.xyz - vPos);
        float ff0 = clamp((fd0 - fr.fogSat.x) / (fr.fogSat.y - fr.fogSat.x), 0.0, 1.0);
        vec3 lc = mix(texel * 1.8, fr.fogColor.rgb, ff0);
        outColor = vec4(pow(lc, vec3(1.0 / gamma)), 1.0);
        return;
    }
    vec3 amb = mix(fr.ambGnd.rgb, fr.ambSky.rgb, n.y * 0.5 + 0.5) * texel;
    vec3 sunDirW = normalize(fr.sunDir.xyz);
    float diff = max(dot(n, sunDirW), 0.0);
    vec3 direct = fr.sunDiff.rgb * diff * texel;
    vec3 viewDir = normalize(fr.viewPos.xyz - vPos);
    vec3 hv = normalize(sunDirW + viewDir);
    float spec = pow(max(dot(n, hv), 0.0), fr.sunSpec.w) *
                 texture(specMap, fract(vUV)).r;
    vec3 result = amb + direct + fr.sunSpec.rgb * spec;
    float fshade = abs(n.y) > 0.9 ? (n.y > 0.0 ? 1.0 : 0.5)
                                  : (abs(n.x) > abs(n.z) ? 0.6 : 0.8);
    float aoV = clamp(vAO / 3.0, 0.0, 1.0);
    float aoC = 0.5 + 0.5 * aoV * aoV;
    vec3 shaded = result * fshade * aoC;
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, fr.fogSat.z);
    float fd = length(fr.viewPos.xyz - vPos);
    float ff = clamp((fd - fr.fogSat.x) / (fr.fogSat.y - fr.fogSat.x), 0.0, 1.0);
    vec3 col = mix(shaded, fr.fogColor.rgb, ff);
    outColor = vec4(pow(col, vec3(1.0 / gamma)), 1.0);
}
