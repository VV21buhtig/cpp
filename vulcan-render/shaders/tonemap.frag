#version 450
// demo-5a tonemap: HDR * key/exp + Uchimura (дефолт книги, не AgX).
layout(set = 0, binding = 0) uniform sampler2D hdrImg;
layout(set = 0, binding = 1) uniform sampler2D expImg;
layout(set = 0, binding = 5) uniform sampler2D bloomImg; // demo-5b
layout(set = 0, binding = 6) uniform sampler2D ssaoImg; // demo-6
layout(push_constant) uniform Push { vec4 res; float aoK; } pc;
layout(location = 0) out vec4 o;

vec3 uchimura(vec3 x) {
    float P = 1.0, a = 1.05, m = 0.1, l = 0.8, c = 3.0, b = 0.0;
    float l0 = (P - m) * l / a, S0 = m + l0, S1 = m + a * l0;
    float C2 = a * P / (P - S1), CP = -C2 / P;
    vec3 w0 = vec3(1.0) - smoothstep(vec3(0.0), vec3(m), x);
    vec3 w2 = step(vec3(m) + vec3(l0), x);
    vec3 w1 = vec3(1.0) - w0 - w2;
    vec3 T = vec3(m) * pow(max(x / vec3(m), vec3(0.0)), vec3(c)) + vec3(b);
    vec3 L = vec3(m) + a * (x - vec3(m));
    vec3 S = vec3(P) - (vec3(P) - vec3(S1)) * exp(vec3(CP) * (x - vec3(S0)));
    return T * w0 + L * w1 + S * w2;
}

void main() {
    vec2 uv = gl_FragCoord.xy / pc.res.xy;
    vec3 col = texture(hdrImg, uv).rgb;
    col *= mix(1.0, clamp(texture(ssaoImg, uv * pc.res.zw).r, 0.0, 1.0), pc.aoK); // demo-6 AO (F2)
    float e = max(texture(expImg, vec2(0.5)).r, 1e-3);
    e = max(e, 0.33); // потолок буста x1.5: иначе тёмный центр кадра выбеливает песок
    col *= 0.5 / e;
    col += texture(bloomImg, uv).rgb * 0.05; // demo-5b: аддитивный bloom
    o = vec4(uchimura(max(col, vec3(0.0))), 1.0);
}
