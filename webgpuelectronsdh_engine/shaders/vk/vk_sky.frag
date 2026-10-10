#version 450
// Небо — их sky.fs (V): градиент, солнце+гало, закат, луна, звёзды.
layout(push_constant) uniform Push {
    mat4 invVP;
    vec4 sunDirNight;
    vec4 topColor;
    vec4 horColor;
} pc;
layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    // gl_FragCoord во фреймбуфере сверху вниз — отражаем Y под GL-формулы.
    vec2 ndc = vec2((gl_FragCoord.x / pc.horColor.w) * 2.0 - 1.0,
                    1.0 - (gl_FragCoord.y / pc.topColor.w) * 2.0);
    vec4 w = pc.invVP * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w);
    if (dir.y < 0.0) dir.y = 0.0;
    float nightF = pc.sunDirNight.w;
    vec3 sunDir = pc.sunDirNight.xyz;
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 col = mix(pc.horColor.rgb, pc.topColor.rgb, pow(h, 0.6));
    float s = max(dot(dir, sunDir), 0.0);
    col += vec3(1.0, 0.85, 0.6) * pow(s, 900.0) * 3.0;
    col += vec3(1.0, 0.6, 0.3) * pow(s, 8.0) * 0.25 * (1.0 - nightF);
    col += vec3(0.9, 0.3, 0.1) * pow(1.0 - abs(dir.y), 6.0) * pow(1.0 - abs(sunDir.y), 4.0) * 0.6;
    float m = max(dot(dir, -sunDir), 0.0);
    col += vec3(0.85, 0.9, 1.0) * pow(m, 1500.0) * 2.0;
    col += vec3(0.4, 0.5, 0.7) * pow(m, 100.0) * 0.15;
    vec3 cell = floor(dir * 120.0);
    float st = step(0.997, hash(cell)) * nightF * smoothstep(0.05, 0.3, dir.y);
    col += vec3(st) * (0.5 + 0.5 * hash(cell + 7.0));
    outColor = vec4(col, 1.0);
}
