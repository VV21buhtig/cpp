#include "engine/chunk.h"
#include <glad/gl.h>

// 6 граней единичного куба [0,1]^3, CCW (выведено из фиксенного куба гл.25).
// Формат: pos3 + norm3 + uv2. Позиции для блока (x,y,z) = base + таблица.
static const float kFaces[6][6][8] = {
    // back (-z)
    {{0,1,0, 0,0,-1, 0,1},{1,1,0, 0,0,-1, 1,1},{1,0,0, 0,0,-1, 1,0},
     {1,0,0, 0,0,-1, 1,0},{0,0,0, 0,0,-1, 0,0},{0,1,0, 0,0,-1, 0,1}},
    // front (+z)
    {{0,0,1, 0,0,1, 0,0},{1,0,1, 0,0,1, 1,0},{1,1,1, 0,0,1, 1,1},
     {1,1,1, 0,0,1, 1,1},{0,1,1, 0,0,1, 0,1},{0,0,1, 0,0,1, 0,0}},
    // left (-x)
    {{0,1,1, -1,0,0, 1,0},{0,1,0, -1,0,0, 1,1},{0,0,0, -1,0,0, 0,1},
     {0,0,0, -1,0,0, 0,1},{0,0,1, -1,0,0, 0,0},{0,1,1, -1,0,0, 1,0}},
    // right (+x)
    {{1,0,0, 1,0,0, 0,1},{1,1,0, 1,0,0, 1,1},{1,1,1, 1,0,0, 1,0},
     {1,1,1, 1,0,0, 1,0},{1,0,1, 1,0,0, 0,0},{1,0,0, 1,0,0, 0,1}},
    // bottom (-y)
    {{0,0,0, 0,-1,0, 0,1},{1,0,0, 0,-1,0, 1,1},{1,0,1, 0,-1,0, 1,0},
     {1,0,1, 0,-1,0, 1,0},{0,0,1, 0,-1,0, 0,0},{0,0,0, 0,-1,0, 0,1}},
    // top (+y)
    {{1,1,1, 0,1,0, 1,0},{1,1,0, 0,1,0, 1,1},{0,1,0, 0,1,0, 0,1},
     {0,1,0, 0,1,0, 0,1},{0,1,1, 0,1,0, 0,0},{1,1,1, 0,1,0, 1,0}},
};
static const int kNb[6][3] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};

std::vector<float> Chunk::buildMesh() const {
    std::vector<float> out;
    out.reserve(4096 * 8);
    for (int z = 0; z < SZ; z++)
    for (int y = 0; y < SY; y++)
    for (int x = 0; x < SX; x++) {
        if (get(x, y, z) == 0) continue;
        for (int f = 0; f < 6; f++) {
            int nx = x + kNb[f][0], ny = y + kNb[f][1], nz = z + kNb[f][2];
            if (get(nx, ny, nz) != 0) continue; // сосед занят — грань скрыта
            for (int v = 0; v < 6; v++) {
                out.push_back(kFaces[f][v][0] + x);
                out.push_back(kFaces[f][v][1] + y);
                out.push_back(kFaces[f][v][2] + z);
                out.push_back(kFaces[f][v][3]);
                out.push_back(kFaces[f][v][4]);
                out.push_back(kFaces[f][v][5]);
                float tile = (f == 5) ? 1.0f : 0.0f; // top=container2, бока/низ=container
                out.push_back(kFaces[f][v][6]);
                out.push_back(kFaces[f][v][7]);
                out.push_back(0.0f); // legacy: tile0
                out.push_back(3.0f); // legacy: no AO
            }
        }
    }
    return out;
}

void ChunkMesh::upload(const std::vector<float>& data) {
    destroy();
    vertexCount = (int)(data.size() / 10);
    if (data.empty()) return;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(8*sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(9*sizeof(float)));
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);
}

void ChunkMesh::draw() const {
    if (!VAO || !vertexCount) return;
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void ChunkMesh::destroy() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    VAO = VBO = 0; vertexCount = 0;
}
