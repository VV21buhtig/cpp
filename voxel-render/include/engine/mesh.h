#ifndef MESH_H
#define MESH_H

// Engine: владение VBO/VAO куба. Порядок вызовов GL сохранён 1:1 из main.cpp,
// чтобы не утонуть в буферах: сначала VBO, потом cubeVAO (0,1,2), потом lightCubeVAO (0).
class CubeMesh {
public:
    unsigned int VBO = 0;
    unsigned int cubeVAO = 0;
    unsigned int lightCubeVAO = 0;

    void init();
    void bindCube() const;
    void bindLight() const;
    void destroy();
};

#endif
