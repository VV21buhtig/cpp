#version 450
// Pick-фрагмент меша: плоский id сущности (RGB = id+1, 0 = мимо).
// Вершинный — общий mesh.vert (тот же uMVP).
uniform uint uId;
out vec4 C;
void main() {
    uint id = uId;
    C = vec4(float(id & 255u) / 255.0, float((id >> 8) & 255u) / 255.0,
             float((id >> 16) & 255u) / 255.0, 1.0);
}
