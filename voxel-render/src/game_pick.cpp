#include "game/pick.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

glm::mat4 cubeModelMatrix(glm::vec3 pos, unsigned int index) {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, pos);
    m = glm::rotate(m, glm::radians(20.0f * index), glm::vec3(1.0f, 0.3f, 0.5f));
    return m;
}

float rayAABBLocal(glm::vec3 origin, glm::vec3 dir) {
    float tmin = 0.0f, tmax = 1e9f;
    for (int a = 0; a < 3; a++) {
        float o = origin[a], d = dir[a];
        if (fabs(d) < 1e-6f) {
            if (o < -0.5f || o > 0.5f) return -1.0f;
        } else {
            float t1 = (-0.5f - o) / d;
            float t2 = ( 0.5f - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1.0f;
        }
    }
    return tmin;
}

int pickCube(glm::vec3 rayOrigin, glm::vec3 rayDir,
             const glm::vec3* positions, int count, float maxDist) {
    int bestIdx = -1;
    float bestT = maxDist;
    for (int i = 0; i < count; i++) {
        glm::mat4 model = cubeModelMatrix(positions[i], i);
        glm::mat4 inv   = glm::inverse(model);
        glm::vec3 lo = glm::vec3(inv * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 ld = glm::vec3(inv * glm::vec4(rayDir,    0.0f));
        float t = rayAABBLocal(lo, ld);
        if (t > 0.0f && t < bestT) { bestT = t; bestIdx = i; }
    }
    return bestIdx;
}
