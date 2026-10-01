#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push { float phase; } pc;
// Пульс синус/косинус — видно что кадр живой без единого юниформа/дескриптора.
void main() {
    outColor = vec4(0.5 + 0.5 * sin(pc.phase),
                    0.5 + 0.5 * sin(pc.phase + 2.094),
                    0.5 + 0.5 * sin(pc.phase + 4.188), 1.0);
}
