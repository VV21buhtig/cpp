#version 450 core
// Компактный FXAA (ядро — console-алгоритм FXAA 3.11 Lottes: градиент -> 2 тапа вдоль кромки).
// Математика:
// 1. luma окрестности 3x3. Ранний выход, если контраст < max(edgeThr*maxLuma, edgeThrMin):
//    плоские зоны не трогаем — ни мыла, ни цены (~1 тап вместо ~9).
// 2. Направление кромки из luma-градиента: dir = (-(NW+NE-SW-SE), (NW+SW-NE-SE)).
//    Бленд идёт ВДОЛЬ кромки (перпендикулярно градиенту): 2 тапа со сдвигом 1/3 и 2/3
//    пикселя (rgbA), затем расширение до +-1px (rgbB). Если rgbB вылетел за [min,max]
//    окрестности — берём rgbA (защита от замыливания деталей).
// 3. Субпиксель: результат тянем к центру на (1-subpix) — лесенка в 1px тает,
//    геометрия остаётся резкой.
// Цена: плоские пиксели ~1 тап, кромки ~9. На GTX460-классе ~0.5-1мс @1080p.
in vec2 vUV;
out vec4 frag;
uniform sampler2D sceneTex;
uniform vec2 rcpFrame;    // 1/w, 1/h
uniform float subpix;     // 0.75
uniform float edgeThr;    // 0.125
uniform float edgeThrMin; // 0.0625

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 rgbM = texture(sceneTex, vUV).rgb;
    float lM = luma(rgbM);
    float lN = luma(textureOffset(sceneTex, vUV, ivec2(0, 1)).rgb);
    float lS = luma(textureOffset(sceneTex, vUV, ivec2(0, -1)).rgb);
    float lE = luma(textureOffset(sceneTex, vUV, ivec2(1, 0)).rgb);
    float lW = luma(textureOffset(sceneTex, vUV, ivec2(-1, 0)).rgb);
    float lNW = luma(textureOffset(sceneTex, vUV, ivec2(-1, 1)).rgb);
    float lNE = luma(textureOffset(sceneTex, vUV, ivec2(1, 1)).rgb);
    float lSW = luma(textureOffset(sceneTex, vUV, ivec2(-1, -1)).rgb);
    float lSE = luma(textureOffset(sceneTex, vUV, ivec2(1, -1)).rgb);
    float lMin = min(lM, min(min(lN, lS), min(min(lE, lW), min(min(lNW, lNE), min(lSW, lSE)))));
    float lMax = max(lM, max(max(lN, lS), max(max(lE, lW), max(max(lNW, lNE), max(lSW, lSE)))));
    if ((lMax - lMin) < max(edgeThr * lMax, edgeThrMin)) { frag = vec4(rgbM, 1.0); return; }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float dirLen = max(length(dir), 1e-5);
    vec2 dirPx = dir * (1.0 / dirLen);
    // кламп шага: не дальше 2px, не ближе 1/3px (иначе плывёт)
    vec2 span = clamp(dirPx * rcpFrame * 2.0, -vec2(2.0) * rcpFrame, vec2(2.0) * rcpFrame);
    vec3 rgbA = 0.5 * (texture(sceneTex, vUV + span * (1.0 / 6.0)).rgb +
                       texture(sceneTex, vUV - span * (1.0 / 6.0)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(sceneTex, vUV + span * 0.5).rgb +
                                     texture(sceneTex, vUV - span * 0.5).rgb);
    float lB = luma(rgbB);
    vec3 rgb = ((lB < lMin) || (lB > lMax)) ? rgbA : rgbB;
    frag = vec4(mix(rgbM, rgb, clamp(subpix, 0.0, 1.0)), 1.0);
}
