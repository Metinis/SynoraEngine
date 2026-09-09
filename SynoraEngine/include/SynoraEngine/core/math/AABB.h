#pragma once

#include <glm/glm.hpp>

namespace SYN {
struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    glm::vec3 getPVertex(glm::vec3 normal) const;
    glm::vec3 getNVertex(glm::vec3 normal) const;

    AABB transform(glm::mat4 transform) const;
};
} // namespace SYN
