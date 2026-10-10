#version 450
// Блит 2D-кадра. V перевёрнут: текстура хранится низом вверх (GL-порядок),
// UV фулскрина идёт снизу — совпадает без флипа.
uniform sampler2D frame;
in vec2 UV;
out vec4 FragColor;
void main() {
    FragColor = texture(frame, UV);
}
