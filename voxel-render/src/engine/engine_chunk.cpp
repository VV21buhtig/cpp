#include "engine/chunk.h"
#include <glad/gl.h>
#include <cstdio>

void ChunkMesh::upload(const std::vector<float>& data) {
    // формат pos3+norm3+uv2+tile1+ao1+day1+night1 = 12: чужой страйд = взрыв спайков (было)
    if (data.size() % 12 != 0) { printf("MESH STRIDE BUG: %zu floats\n", data.size()); return; }
    destroy();
    vertexCount = (int)(data.size() / 12);
    if (data.empty()) return;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(8*sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(9*sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(10*sizeof(float)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(11*sizeof(float)));
    glEnableVertexAttribArray(6);
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
