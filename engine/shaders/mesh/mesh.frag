#version 450
// Меш-домен: Ламберт + hemispheric ambient, гамма. Минимум, не PBR.
uniform vec3 uSun; // направление НА солнце
layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec3 vCol;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 n = normalize(vNrm);
    vec3 sd = normalize(uSun);
    float dif = max(dot(n, sd), 0.0);
    vec3 amb = mix(vec3(0.25, 0.22, 0.20), vec3(0.55, 0.60, 0.68), n.y * 0.5 + 0.5);
    vec3 col = vCol * (amb + vec3(1.5) * dif);
    outColor = vec4(pow(col, vec3(1.0 / 1.2)), 1.0);
}
