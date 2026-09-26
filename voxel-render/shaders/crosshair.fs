#version 450 core
out vec4 FragColor;
uniform vec2 res;
uniform float t;

// полупрозрачный прицел с RGB-разводом по времени
void main() {
    vec2 p = (gl_FragCoord.xy / res - 0.5) * vec2(res.x / res.y, 1.0);
    float px = 1.0 / res.y; // пиксель в единицах высоты
    float len = 0.012, gap = 0.004, w = 1.6 * px;
    float bx = (1.0 - smoothstep(w, w + px, abs(p.y))) * step(gap, abs(p.x)) * (1.0 - step(len, abs(p.x)));
    float by = (1.0 - smoothstep(w, w + px, abs(p.x))) * step(gap, abs(p.y)) * (1.0 - step(len, abs(p.y)));
    float m = max(bx, by);
    if (m <= 0.0) discard;
    float r = 0.5 + 0.5 * sin(t * 2.0 + p.x * 60.0);
    float g = 0.5 + 0.5 * sin(t * 2.0 + 2.1 + p.y * 60.0);
    float b = 0.5 + 0.5 * sin(t * 2.0 + 4.2 - p.x * 60.0);
    FragColor = vec4(r, g, b, m * 0.55);
}
