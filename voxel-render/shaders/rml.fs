#version 450 core
// RmlUi цвета уже premultiplied — выход как есть, бленд ONE/ONE_MINUS_SRC_ALPHA.
uniform sampler2D _tex;
uniform int _useTex;
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
void main() {
    vec4 c = fragColor;
    if (_useTex != 0) c *= texture(_tex, fragTexCoord);
    finalColor = c;
}
