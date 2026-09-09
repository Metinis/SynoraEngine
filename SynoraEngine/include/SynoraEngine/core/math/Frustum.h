#pragma once

#include "AABB.h"
#include "Plane.h"

namespace SYN {
struct Frustum {
    std::vector<Plane> planes;

    static Frustum fromViewProjectionMatrix(glm::mat4 view,
                                            glm::mat4 projection);

    static std::vector<glm::vec4> getCornersWorldSpace(glm::mat4 view,
                                                       glm::mat4 projection);

    bool collidesWithAABB(AABB aabb) const;
};
} // namespace SYN
