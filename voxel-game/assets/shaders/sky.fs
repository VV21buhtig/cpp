#version 450 core
out vec4 FragColor;

uniform mat4 invVP;
uniform vec3 topColor;
uniform vec3 horizonColor;
uniform vec3 sunDir;   // направление НА солнце
uniform float nightF;  // 0 день, 1 ночь
uniform vec2 res;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec2 ndc = (gl_FragCoord.xy / res) * 2.0 - 1.0;
    vec4 w = invVP * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w);
    if (dir.y < 0.0) dir.y = 0.0; // под горизонтом — горизонт

    float h = clamp(dir.y, 0.0, 1.0);
    vec3 col = mix(horizonColor, topColor, pow(h, 0.6));

    // солнце + гало
    float s = max(dot(dir, sunDir), 0.0);
    col += vec3(1.0, 0.85, 0.6) * pow(s, 900.0) * 3.0;  // диск
    col += vec3(1.0, 0.6, 0.3) * pow(s, 8.0) * 0.25 * (1.0 - nightF); // гало днём
    // закатная полоса
    col += vec3(0.9, 0.3, 0.1) * pow(1.0 - abs(dir.y), 6.0) * pow(1.0 - abs(sunDir.y), 4.0) * 0.6;

    // луна напротив солнца
    float m = max(dot(dir, -sunDir), 0.0);
    col += vec3(0.85, 0.9, 1.0) * pow(m, 1500.0) * 2.0;
    col += vec3(0.4, 0.5, 0.7) * pow(m, 100.0) * 0.15;

    // звёзды ночью
    vec3 cell = floor(dir * 120.0);
    float st = step(0.997, hash(cell)) * nightF * smoothstep(0.05, 0.3, dir.y);
    col += vec3(st) * (0.5 + 0.5 * hash(cell + 7.0));

    FragColor = vec4(col, 1.0);
}
