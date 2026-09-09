#include <SynoraEngine/core/math/Frustum.h>

namespace SYN {
Frustum Frustum::fromViewProjectionMatrix(glm::mat4 view,
                                          glm::mat4 projection) {
    // Transpose because math below assumes row major
    glm::mat4 vp = glm::transpose(projection * view);

    Frustum frustum;

    // Normals of planes aren't normalized. Doesn't matter
    // because only the sign is needed for frustum culling case
    frustum.planes = {
        // Left Plane
        Plane{vp[3][0] + vp[0][0], vp[3][1] + vp[0][1], vp[3][2] + vp[0][2],
              vp[3][3] + vp[0][3]},
        // Right Plane
        Plane{vp[3][0] - vp[0][0], vp[3][1] - vp[0][1], vp[3][2] - vp[0][2],
              vp[3][3] - vp[0][3]},
        // Bottom Plane
        Plane{vp[3][0] + vp[1][0], vp[3][1] + vp[1][1], vp[3][2] + vp[1][2],
              vp[3][3] + vp[1][3]},
        // Top Plane
        Plane{vp[3][0] - vp[1][0], vp[3][1] - vp[1][1], vp[3][2] - vp[1][2],
              vp[3][3] - vp[1][3]},
        // Near Plane
        Plane{vp[3][0] + vp[2][0], vp[3][1] + vp[2][1], vp[3][2] + vp[2][2],
              vp[3][3] + vp[2][3]},
        // Far Plane
        Plane{vp[3][0] - vp[2][0], vp[3][1] - vp[2][1], vp[3][2] - vp[2][2],
              vp[3][3] - vp[2][3]},
    };

    return frustum;
}

bool Frustum::collidesWithAABB(AABB aabb) const {
    for (Plane plane : planes) {
        glm::vec3 planeNormal = glm::vec3(plane.a, plane.b, plane.c);
        glm::vec3 pVertex = aabb.getPVertex(planeNormal);

        if (glm::dot(planeNormal, pVertex) < -plane.d)
            return false;
    }
    return true;
}

std::vector<glm::vec4> Frustum::getCornersWorldSpace(glm::mat4 view,
                                                     glm::mat4 projection) {
    glm::mat4 viewProjectionInverse = glm::inverse(projection * view);
    std::vector<glm::vec4> frustumCorners;
    frustumCorners.reserve(8);
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                glm::vec4 p(x * 2 - 1, y * 2 - 1, z * 2 - 1, 1.0f);
                p = viewProjectionInverse * p;
                p /= p.w;
                frustumCorners.push_back(p);
            }
        }
    }
    return frustumCorners;
}
} // namespace SYN
