#version 450
// demo-4b рентген: теневая карта на весь экран (shadowmap-view, как в анриле).
// Сплит: слева полосы (рельеф), справа сырая глубина. res едет пушем [64,72).
layout(set = 0, binding = 6) uniform sampler2D shadowRaw;
layout(push_constant) uniform PushDbg {
    layout(offset = 64) vec2 res;
} dbg;
layout(location = 0) out vec4 o;
void main() {
    vec2 uv = gl_FragCoord.xy / dbg.res;
    float d = texture(shadowRaw, uv).r;
    float v = (gl_FragCoord.x < dbg.res.x * 0.5) ? (0.5 + 0.5 * sin(d * 120.0)) : d;
    o = vec4(v, v, v, 1.0);
}
