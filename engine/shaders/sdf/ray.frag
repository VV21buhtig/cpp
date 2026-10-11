#version 450
// SDF-реймарш (сферотрейсинг): сцена из UBO, 64 примитива макс.
// Виды: 0 сфера 1 коробка 2 тор 3 плоскость 4 капсула (RcSdfObj 1:1).
// Небо и свет — те же формулы что sky.fs/lighting (V): солнце+гало, закат,
// луна, звёзды, hemispheric ambient, туман, гамма.
layout(std140, binding = 0) uniform Scene {
    vec4 camPos;    // xyz
    vec4 camFwd;
    vec4 camRight;
    vec4 camUp;
    vec4 resF;      // w,h,focal,time
    vec4 sunNight;  // xyz sunDir, w nightF
    vec4 prims[192]; // 64 x (a=p0+r, b=p1+mat, c=kind+0)
};
uniform int nPrim;
out vec4 FragColor;

float sdPrim(vec3 p, int i) {
    vec4 a = prims[3 * i], b = prims[3 * i + 1], c = prims[3 * i + 2];
    int kind = int(c.x + 0.5);
    if (kind == 0) return length(p - a.xyz) - a.w; // сфера
    if (kind == 1) { // коробка: p0 центр, p1 полуразмеры
        vec3 q = abs(p - a.xyz) - b.xyz;
        return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
    }
    if (kind == 2) { // тор в XZ: R=a.w, r=b.x
        vec2 q = vec2(length(p.xz - a.xz) - a.w, p.y - a.y);
        return length(q) - b.x;
    }
    if (kind == 3) return dot(p, a.xyz) + a.w; // плоскость: нормаль+сдвиг
    // капсула p0..p1 радиус r
    vec3 pa = p - a.xyz, ba = b.xyz - a.xyz;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-9), 0.0, 1.0);
    return length(pa - ba * h) - a.w;
}

vec2 map(vec3 p) { // x=дистанция, y=материал
    vec2 r = vec2(1e9, -1.0);
    for (int i = 0; i < 64; i++) {
        if (i >= nPrim) break;
        float d = sdPrim(p, i);
        if (d < r.x) r = vec2(d, prims[3 * i + 1].w);
    }
    return r;
}

vec3 matColor(float m) {
    if (m < 0.5) return vec3(0.75, 0.12, 0.10);
    if (m < 1.5) return vec3(0.10, 0.45, 0.12);
    if (m < 2.5) return vec3(0.12, 0.25, 0.75);
    if (m < 3.5) return vec3(0.75, 0.65, 0.15);
    if (m < 4.5) return vec3(0.55, 0.57, 0.60);
    return vec3(0.80, 0.80, 0.82);
}

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

vec3 skyColor(vec3 dir, vec3 sunDir, float nightF) {
    vec3 d = dir;
    if (d.y < 0.0) d.y = 0.0;
    float h = clamp(d.y, 0.0, 1.0);
    vec3 col = mix(vec3(0.55, 0.60, 0.68), vec3(0.16, 0.32, 0.58), pow(h, 0.6));
    float s = max(dot(d, sunDir), 0.0);
    col += vec3(1.0, 0.85, 0.6) * pow(s, 900.0) * 3.0;
    col += vec3(1.0, 0.6, 0.3) * pow(s, 8.0) * 0.25 * (1.0 - nightF);
    col += vec3(0.9, 0.3, 0.1) * pow(1.0 - abs(d.y), 6.0) * pow(1.0 - abs(sunDir.y), 4.0) * 0.6;
    float m = max(dot(d, -sunDir), 0.0);
    col += vec3(0.85, 0.9, 1.0) * pow(m, 1500.0) * 2.0;
    col += vec3(0.4, 0.5, 0.7) * pow(m, 100.0) * 0.15;
    vec3 cell = floor(d * 120.0);
    float st = step(0.997, hash(cell)) * nightF * smoothstep(0.05, 0.3, d.y);
    col += vec3(st) * (0.5 + 0.5 * hash(cell + 7.0));
    return col;
}

void main() {
    vec2 res = resF.xy;
    vec2 uv = vec2((gl_FragCoord.x - 0.5 * res.x) / res.y,
                   (gl_FragCoord.y - 0.5 * res.y) / res.y);
    vec3 ro = camPos.xyz;
    vec3 rd = normalize(uv.x * camRight.xyz + uv.y * camUp.xyz + resF.z * camFwd.xyz);
    vec3 sunDir = sunNight.xyz;
    float nightF = sunNight.w;
    // Марш. Около поверхностей шаги мельчают и лимит может сгореть раньше
    // plane — тогда был бы ореол неба вокруг объектов («сингулярность»).
    // Лечим closest-hit фолбэком с относительным порогом (~2px на любой
    // дистанции): абсолютный мазал мимо силуэтов (шишка на капсуле).
    float t = 0.0;
    float mat = -1.0;
    float best = 1e9, bestT = 0.0, bestMat = -1.0;
    for (int i = 0; i < 192; i++) {
        vec2 h = map(ro + rd * t);
        if (h.x < best) { best = h.x; bestT = t; bestMat = h.y; }
        if (h.x < 0.0015) { mat = h.y; break; }
        t += h.x;
        if (t > 200.0) break;
    }
    if (mat < -0.5 && bestT > 1.0 && best < 0.004 * bestT) { mat = bestMat; t = bestT; }
    vec3 col;
    if (mat < -0.5) {
        col = skyColor(rd, sunDir, nightF);
    } else {
        vec3 p = ro + rd * t;
        vec2 e = vec2(0.002, 0.0);
        vec3 n = normalize(vec3(map(p + e.xyy).x - map(p - e.xyy).x,
                                map(p + e.yxy).x - map(p - e.yxy).x,
                                map(p + e.yyx).x - map(p - e.yyx).x));
        vec3 tex = matColor(mat);
        vec3 amb = mix(vec3(0.27, 0.24, 0.21), vec3(0.54, 0.60, 0.69), n.y * 0.5 + 0.5) * tex;
        float dif = max(dot(n, sunDir), 0.0);
        // Тень: марш к солнцу.
        float sh = 1.0;
        float st = 0.02;
        for (int i = 0; i < 32; i++) {
            float h = map(p + n * 0.004 + sunDir * st).x;
            if (h < 0.002) { sh = 0.15; break; }
            st += h;
            if (st > 30.0) break;
        }
        vec3 lit = amb + vec3(1.7, 1.6, 1.45) * dif * sh * tex;
        float fd = length(p - ro);
        float ff = clamp(fd / 260.0, 0.0, 1.0);
        col = mix(lit, vec3(0.30, 0.36, 0.46), ff);
    }
    col = pow(col, vec3(1.0 / 1.2));
    FragColor = vec4(col, 1.0);
}
