#version 450
// demo-4b рентген: сырая глубина теневой карты серым (x4 — дальность жмёт рельеф).
layout(set = 0, binding = 6) uniform sampler2D shadowRaw;
layout(location = 0) out vec4 o;
void main() {
    vec2 uv = vec2(gl_FragCoord.x / 256.0, gl_FragCoord.y / 256.0);
    float d = texture(shadowRaw, clamp(uv, vec2(0.0), vec2(1.0))).r;
    o = vec4(vec3(d * 4.0), 1.0);
}
