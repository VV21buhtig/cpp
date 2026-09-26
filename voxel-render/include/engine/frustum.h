#ifndef FRUSTUM_H
#define FRUSTUM_H

#include <glm/glm.hpp>

// Engine: 6 плоскостей из VP + тест AABB чанка. Header-only.
struct Frustum {
    glm::vec4 p[6];
    static Frustum fromVP(const glm::mat4& vp) {
        Frustum f;
        glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
        f.p[0] = r3 + r0; // left
        f.p[1] = r3 - r0; // right
        f.p[2] = r3 + r1; // bottom
        f.p[3] = r3 - r1; // top
        f.p[4] = r3 + r2; // near
        f.p[5] = r3 - r2; // far
        for (int i = 0; i < 6; i++) f.p[i] /= length(f.p[i].xyz);
        return f;
    }
    bool visible(glm::vec3 mn, glm::vec3 mx) const {
        for (int i = 0; i < 6; i++) {
            glm::vec3 pv(
                p[i].x >= 0 ? mx.x : mn.x,
                p[i].y >= 0 ? mx.y : mn.y,
                p[i].z >= 0 ? mx.z : mn.z);
            if (glm::dot(p[i].xyz, pv) + p[i].w < 0.0f) return false;
        }
        return true;
    }
};

#endif
