#version 450 core
// P2i debug: глубина теневой карты серым (белое = далеко/свет, тёмное = близко).
// Рисуется вьюпортом 256x256, координаты берём из gl_FragCoord.
out vec4 frag;
uniform sampler2D depthMap;
void main() {
    vec2 uv = vec2(gl_FragCoord.x / 256.0, gl_FragCoord.y / 256.0);
    float d = texture(depthMap, uv).r;
    frag = vec4(vec3(d), 1.0);
}
