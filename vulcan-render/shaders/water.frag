#version 450
// demo-5w вода: тот же свет что террейн, БЕЗ резких теней (мутная гладь их
// не держит — видно дно с тенью сквозь alpha), alpha 0.75. Тайл всегда вода=4.
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

layout(set = 0, binding = 1) uniform sampler2DArray tiles;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vAO;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 norm = normalize(vNrm);
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.5)
                                     : (abs(norm.x) > abs(norm.z) ? 0.6 : 0.8);
    float aoV = clamp(vAO / 3.0, 0.0, 1.0);
    float aoC = 0.5 + 0.5 * aoV * aoV;

    vec3 tileTex = vec3(texture(tiles, vec3(vUV, 4.0)));
    vec3 sunL = normalize(frame.sunDir.xyz);
    float ndl = max(dot(norm, sunL), 0.0);
    vec3 amb = mix(frame.ambGnd.rgb, frame.ambSky.rgb, norm.y * 0.5 + 0.5) * tileTex;
    // Вода отражает небо, а не ламберт: прямой давим x0.3, иначе полдень
    // выбивает белую плитку в молоко (1.6 -> Uchimura в белое). Проверено рентгеном.
    vec3 direct = frame.sunCol.rgb * ndl * tileTex * 0.3;
    vec3 result = amb + direct;
    // Стенки мутные (иначе glass-танк): свет гаснет с глубиной, аппроксимация.
    if (abs(norm.y) < 0.9) result *= 0.55;

    vec3 shaded = result * fshade * aoC;
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, frame.misc.y);
    float fd = length(frame.viewPos.xyz - vPos);
    float ff = clamp((fd - frame.misc.x) / (frame.fog.w - frame.misc.x), 0.0, 1.0);
    vec3 col = mix(shaded, frame.fog.rgb, ff);
    outColor = vec4(col, 0.65); // дно должно читаться (иначе кусок ткани, не вода)
}
