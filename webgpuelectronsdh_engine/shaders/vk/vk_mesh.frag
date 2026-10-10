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
    vec4 sdfMin;
} fr;
layout(set = 0, binding = 1) uniform sampler2DArray tiles;
layout(set = 0, binding = 2) uniform sampler2D specMap;
layout(set = 0, binding = 3) uniform sampler3D sdfVol;
layout(set = 0, binding = 4) uniform isampler2D sdfTag; // 11x11: чанк слота

// Мимо тега = далеко (мираж тороида): луч дальше не идёт, тень не трогаем.
float sdfAt(vec3 wpos, out int ok) {
    ivec2 ch = ivec2(floor(wpos.xz / 16.0));
    ivec2 org = ivec2(floor(fr.sdfMin.xz / 16.0));
    ivec2 slot = ivec2(mod(vec2(ch - org), vec2(11.0)));
    ok = 0;
    if (any(notEqual(texelFetch(sdfTag, slot, 0).xy, ch))) return 12.0;
    vec3 uvw = (wpos - fr.sdfMin.xyz) / vec3(176.0, 64.0, 176.0);
    if (uvw.x < 0.0 || uvw.x > 1.0 || uvw.z < 0.0 || uvw.z > 1.0 ||
        uvw.y < 0.0 || uvw.y > 1.0)
        return 12.0;
    ok = 1;
    return texture(sdfVol, uvw).r * 24.0 - 12.0;
}

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vTile;
layout(location = 4) in float vAO;
layout(location = 0) out vec4 outColor;

// Мягкая тень маршем по SDF-объёму (как Lumen soft shadow, без карт).
// Возврат 0..1 (1 = свет). Вне объёма считаем далеко (тени нет).
float sdfShadow(vec3 wpos, vec3 sundir) {
    if (fr.sdfMin.w < -0.5) return 1.0; // А/Б: VOX_NO_SDFSH=1
    float res = 1.0;
    // Старт 0.35, не 0: первые сэмплы сидят в интерполяционном скате самой
    // поверхности (R8 + linear дают ~0 на границе) — было зеброй акне.
    // В воксельной сетке ближе 0.35 только сама поверхность: угловые тени
    // стыков уже даёт вершинное AO, ничего не теряем.
    // Плюс тройка против круглых пятен на скользящих лучах:
    // минимальный шаг (не ползём по вмятинам фильтра), жёсткий ноль только
    // явно внутри, penumbra уже (k=16: далёкие скосы не темнят).
    float t = 0.35;
    for (int i = 0; i < 24; i++) {
        vec3 p = wpos + sundir * t;
        int ok = 0;
        float h = sdfAt(p, ok);
        if (ok == 0) break; // край данных — дальше не знаем, тень не трогаем
        if (h < -0.05) return 0.0; // внутри — глухая тень
        res = min(res, 16.0 * h / t);
        t += max(h, 0.2);
        if (t > 40.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

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
    float sh = sdfShadow(vPos, sunDirW);
    vec3 result = amb + direct * sh + fr.sunSpec.rgb * spec;
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
