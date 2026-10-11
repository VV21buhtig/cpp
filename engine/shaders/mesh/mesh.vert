#version 450
// Меш-домен: вершина pos3/nrm3/col3, MVP + нормаль как есть (модель жёсткая).
uniform mat4 uMVP;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec3 vCol;
void main() {
    vNrm = aNrm;
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
