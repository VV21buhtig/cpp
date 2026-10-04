#version 450
// demo-5c небо: физика Kaigen (wc_atmo/wc_skyview) — T 256x64 + MS 32x32 LUT,
// single-scatter raymarch 16 шагов в этом шейдере. Sun-диск как раньше.
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
layout(set = 0, binding = 1) uniform sampler2D transLut;
layout(set = 0, binding = 2) uniform sampler2D msLut;

layout(push_constant) uniform Push { vec4 viewSize; } pc;
layout(location = 0) out vec4 o;

const float GROUND_R = 6.360;
const float ATMO_R = 6.460;
const float PI = 3.141592653589793;

float safeacos(float x) { return acos(clamp(x, -1.0, 1.0)); }

void scatVals(vec3 pos, out vec3 rs, out float ms, out vec3 ext) {
    float altKm = (length(pos) - GROUND_R) * 1000.0;
    float rd = exp(-altKm / 8.0);
    float md = exp(-altKm / 1.2);
    rs = vec3(5.802, 13.558, 33.1) * rd;
    ms = 3.996 * md;
    float ma = 4.4 * md;
    vec3 oz = vec3(0.650, 1.881, 0.085) * max(0.0, 1.0 - abs(altKm - 25.0) / 15.0);
    ext = rs + vec3(ms + ma) + oz;
}
float raySphere(vec3 ro, vec3 rd, float rad) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - rad * rad;
    if (c > 0.0 && b > 0.0) return -1.0;
    float d = b * b - c;
    if (d < 0.0) return -1.0;
    if (d > b * b) return -b + sqrt(d);
    return -b - sqrt(d);
}
float miePhase(float c) {
    float g = 0.8;
    float scale = 3.0 / (8.0 * PI);
    float num = (1.0 - g * g) * (1.0 + c * c);
    float den = (2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * c, 1.5);
    return scale * num / den;
}
float rayPhase(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
vec2 lutUv(vec3 pos, vec3 sun) {
    float h = length(pos);
    vec3 up = pos / h;
    return vec2(clamp(0.5 + 0.5 * dot(sun, up), 0.0, 1.0),
                clamp((h - GROUND_R) / (ATMO_R - GROUND_R), 0.0, 1.0));
}

void main() {
    vec2 ndc = vec2(gl_FragCoord.x / pc.viewSize.x, gl_FragCoord.y / pc.viewSize.y) * 2.0 - 1.0;
    vec4 p = frame.invViewProj * vec4(ndc, 0.5, 1.0);
    p /= p.w;
    vec3 ray = normalize(p.xyz - frame.viewPos.xyz);
    vec3 sun = normalize(frame.sunDir.xyz);

    // Камера в Мм: земля +300м + до 2км за высоту вида (как Kaigen atmo_view_pos).
    float hFrac = clamp((frame.viewPos.y - 20.0) / 64.0, 0.0, 1.0);
    vec3 vpos = vec3(0.0, GROUND_R + 0.0003 + hFrac * 0.002, 0.0);

    float ad = raySphere(vpos, ray, ATMO_R);
    float gd = raySphere(vpos, ray, GROUND_R);
    bool hitG = gd > 0.0;
    float tmax = hitG ? gd : ad;
    if (tmax <= 0.0) { o = vec4(frame.fog.rgb, 1.0); return; }

    float ct = dot(ray, sun);
    float mie = miePhase(ct);
    float rph = rayPhase(-ct);
    vec3 lum = vec3(0.0);
    vec3 tr = vec3(1.0);
    float t = 0.0;
    const int N = 16;
    for (int i = 0; i < N; i++) {
        float f = (float(i) + 0.3) / float(N);
        float nt = tmax * f * f; // квадратично: плотно у камеры (рецепт skyview)
        float dt = nt - t;
        t = nt;
        vec3 np = vpos + t * ray;
        vec3 rs, ext;
        float m;
        scatVals(np, rs, m, ext);
        vec3 st = exp(-dt * ext);
        vec2 uv = lutUv(np, sun);
        vec3 sunT = texture(transLut, uv).rgb;
        vec3 psi = texture(msLut, uv).rgb;
        vec3 insc = rs * (rph * sunT + psi) + vec3(m) * (mie * sunT + psi);
        vec3 integ = (insc - insc * st) / max(ext, vec3(1e-4));
        lum += integ * tr;
        tr *= st;
    }
    // Под горизонтом — в туман террейна (стык с миром без шва).
    if (hitG) lum = mix(lum, frame.fog.rgb * 0.6, clamp(-ray.y * 4.0, 0.0, 1.0));
    // Диск + гало солнца поверх физики.
    float d = dot(ray, sun);
    lum += frame.sunCol.rgb * (smoothstep(0.9993, 0.9997, d) * 4.0 + pow(max(d, 0.0), 350.0) * 0.5);
    o = vec4(lum, 1.0);
}
