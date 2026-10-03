#version 450
// demo-5a небо: аналитический градиент + диск/гало солнца (LUT — demo-5b).
// NDC из gl_FragCoord (Y вниз = Vulkan NDC, invVP уже с Y-флипом — сходится).
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 sunDir;
    vec4 sunCol;
    vec4 ambSky;
    vec4 ambGnd;
    vec4 fog;
    vec4 misc;
    vec4 viewPos;
} frame;

layout(push_constant) uniform Push { vec4 viewSize; } pc; // 16Б (invVP уже в UBO)
layout(location = 0) out vec4 o;

void main() {
    vec2 ndc = vec2(gl_FragCoord.x / pc.viewSize.x, gl_FragCoord.y / pc.viewSize.y) * 2.0 - 1.0;
    vec4 p = frame.invViewProj * vec4(ndc, 0.5, 1.0);
    p /= p.w;
    vec3 ray = normalize(p.xyz - frame.viewPos.xyz);
    vec3 zen = frame.ambSky.rgb * 1.4 + vec3(0.05, 0.10, 0.22);
    vec3 hor = frame.fog.rgb;
    vec3 col = mix(hor, zen, pow(clamp(ray.y, 0.0, 1.0), 0.6));
    col = mix(col, hor * 0.35, clamp(-ray.y * 4.0, 0.0, 1.0)); // под горизонтом
    float d = dot(ray, normalize(frame.sunDir.xyz));
    col += frame.sunCol.rgb * (smoothstep(0.9993, 0.9997, d) * 4.0 +
                               pow(max(d, 0.0), 350.0) * 0.5); // диск + гало
    o = vec4(col, 1.0);
}
