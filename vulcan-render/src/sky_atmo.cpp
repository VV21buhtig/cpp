// demo-5c: bake T + MS LUT. Порт wc_atmo/wc_transmittance/wc_multiscatter GLSL в C++.
#include "sky_atmo.h"
#include <cmath>

namespace sky {
namespace {

struct V3 { float x, y, z; };
inline V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 mul3(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { float l = len(a); return l > 0.0f ? mul(a, 1.0f / l) : a; }

constexpr float PI = 3.14159265358979323846f;
constexpr float GROUND_R = 6.360f; // Мм
constexpr float ATMO_R = 6.460f;  // Мм (+100км)
constexpr float RAY_BASE[3] = {5.802f, 13.558f, 33.1f};
constexpr float MIE_SCAT = 3.996f;
constexpr float MIE_ABS = 4.4f;
constexpr float OZONE_BASE[3] = {0.650f, 1.881f, 0.085f};
constexpr float GROUND_ALBEDO = 0.3f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float safeacos(float x) { return std::acos(clampf(x, -1.0f, 1.0f)); }

struct Scat { V3 ray; float mie; V3 ext; };

void scatValues(V3 pos, Scat& out) {
    float altKm = (len(pos) - GROUND_R) * 1000.0f;
    float rd = std::exp(-altKm / 8.0f);
    float md = std::exp(-altKm / 1.2f);
    out.ray = {RAY_BASE[0] * rd, RAY_BASE[1] * rd, RAY_BASE[2] * rd};
    out.mie = MIE_SCAT * md;
    float ma = MIE_ABS * md;
    float oz = std::max(0.0f, 1.0f - std::fabs(altKm - 25.0f) / 15.0f);
    out.ext = {out.ray.x + out.mie + ma + OZONE_BASE[0] * oz,
               out.ray.y + out.mie + ma + OZONE_BASE[1] * oz,
               out.ray.z + out.mie + ma + OZONE_BASE[2] * oz};
}

float raySphere(V3 ro, V3 rd, float rad) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - rad * rad;
    if (c > 0.0f && b > 0.0f) return -1.0f;
    float d = b * b - c;
    if (d < 0.0f) return -1.0f;
    float s = std::sqrt(d);
    if (d > b * b) return -b + s;
    return -b - s;
}

inline float miePhase(float c) {
    float g = 0.8f;
    float scale = 3.0f / (8.0f * PI);
    float num = (1.0f - g * g) * (1.0f + c * c);
    float den = (2.0f + g * g) * std::pow(1.0f + g * g - 2.0f * g * c, 1.5f);
    return scale * num / den;
}
inline float rayPhase(float c) { return 3.0f / (16.0f * PI) * (1.0f + c * c); }

V3 sunTrans(V3 pos, V3 sun) {
    if (raySphere(pos, sun, GROUND_R) > 0.0f) return {0, 0, 0};
    float dist = raySphere(pos, sun, ATMO_R);
    float t = 0.0f;
    V3 tr{1, 1, 1};
    for (int i = 0; i < 40; i++) {
        float nt = ((float(i) + 0.3f) / 40.0f) * dist;
        float dt = nt - t;
        t = nt;
        Scat s;
        scatValues(add(pos, mul(sun, t)), s);
        tr = {tr.x * std::exp(-dt * s.ext.x),
              tr.y * std::exp(-dt * s.ext.y),
              tr.z * std::exp(-dt * s.ext.z)};
    }
    return tr;
}

} // namespace

std::vector<float> bakeTransmittance() {
    std::vector<float> out(TRANS_W * TRANS_H * 4);
    for (int y = 0; y < TRANS_H; y++) {
        for (int x = 0; x < TRANS_W; x++) {
            float u = (float(x) + 0.5f) / float(TRANS_W);
            float v = (float(y) + 0.5f) / float(TRANS_H);
            float cosT = 2.0f * u - 1.0f;
            float theta = safeacos(cosT);
            float h = GROUND_R + (ATMO_R - GROUND_R) * v;
            V3 pos{0, h, 0};
            V3 sun = norm(V3{0, cosT, -std::sin(theta)});
            V3 tr = sunTrans(pos, sun);
            float* p = &out[(y * TRANS_W + x) * 4];
            p[0] = tr.x; p[1] = tr.y; p[2] = tr.z; p[3] = 1.0f;
        }
    }
    return out;
}

std::vector<float> bakeMultiscatter() {
    std::vector<float> out(MS_W * MS_H * 4);
    constexpr int SQ = 8, STEPS = 20;
    const float inv = 1.0f / float(SQ * SQ);
    for (int y = 0; y < MS_H; y++) {
        for (int x = 0; x < MS_W; x++) {
            float u = (float(x) + 0.5f) / float(MS_W);
            float v = (float(y) + 0.5f) / float(MS_H);
            float cosT = 2.0f * u - 1.0f;
            float theta = safeacos(cosT);
            float h = GROUND_R + (ATMO_R - GROUND_R) * v;
            V3 pos{0, h, 0};
            V3 sun = norm(V3{0, cosT, -std::sin(theta)});
            V3 lum{0, 0, 0}, fms{0, 0, 0};
            for (int i = 0; i < SQ; i++) {
                for (int j = 0; j < SQ; j++) {
                    float th = 2.0f * PI * (float(i) + 0.5f) / float(SQ);
                    float ph = safeacos(1.0f - 2.0f * (float(j) + 0.5f) / float(SQ));
                    V3 rd{std::sin(ph) * std::sin(th), std::cos(ph), std::sin(ph) * std::cos(th)};
                    float ad = raySphere(pos, rd, ATMO_R);
                    float gd = raySphere(pos, rd, GROUND_R);
                    float tmax = gd > 0.0f ? gd : ad;
                    float ct = dot(rd, sun);
                    float mie = miePhase(ct);
                    float ray = rayPhase(-ct);
                    V3 l{0, 0, 0}, lf{0, 0, 0}, tr{1, 1, 1};
                    float t = 0.0f;
                    for (int s = 0; s < STEPS; s++) {
                        float nt = ((float(s) + 0.3f) / float(STEPS)) * tmax;
                        float dt = nt - t;
                        t = nt;
                        V3 np = add(pos, mul(rd, t));
                        Scat sv;
                        scatValues(np, sv);
                        V3 st{std::exp(-dt * sv.ext.x), std::exp(-dt * sv.ext.y), std::exp(-dt * sv.ext.z)};
                        V3 noPh{sv.ray.x + sv.mie, sv.ray.y + sv.mie, sv.ray.z + sv.mie};
                        V3 f{(noPh.x - noPh.x * st.x) / sv.ext.x,
                             (noPh.y - noPh.y * st.y) / sv.ext.y,
                             (noPh.z - noPh.z * st.z) / sv.ext.z};
                        lf = add(lf, mul3(tr, f));
                        V3 sunt = sunTrans(np, sun);
                        V3 insc{sv.ray.x * ray * sunt.x + sv.mie * mie * sunt.x,
                                sv.ray.y * ray * sunt.y + sv.mie * mie * sunt.y,
                                sv.ray.z * ray * sunt.z + sv.mie * mie * sunt.z};
                        V3 integ{(insc.x - insc.x * st.x) / sv.ext.x,
                                 (insc.y - insc.y * st.y) / sv.ext.y,
                                 (insc.z - insc.z * st.z) / sv.ext.z};
                        l = add(l, mul3(tr, integ));
                        tr = mul3(tr, st);
                    }
                    if (gd > 0.0f && dot(pos, sun) > 0.0f) {
                        V3 hit = mul(norm(add(pos, mul(rd, gd))), GROUND_R);
                        V3 at = sunTrans(hit, sun);
                        l = add(l, mul3(tr, mul(at, GROUND_ALBEDO)));
                    }
                    fms = add(fms, mul(lf, inv));
                    lum = add(lum, mul(l, inv));
                }
            }
            float* p = &out[(y * MS_W + x) * 4];
            p[0] = lum.x / std::max(1.0f - fms.x, 1e-3f);
            p[1] = lum.y / std::max(1.0f - fms.y, 1e-3f);
            p[2] = lum.z / std::max(1.0f - fms.z, 1e-3f);
            p[3] = 1.0f;
        }
    }
    return out;
}

} // namespace sky
