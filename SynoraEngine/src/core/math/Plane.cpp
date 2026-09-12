#include <SynoraEngine/core/math/Plane.h>

#include <glm/glm.hpp>

namespace SYN {
Plane Plane::from(glm::vec3 point, glm::vec3 normal) {
    normal = glm::normalize(normal);
    return {normal.x, normal.y, normal.z, -glm::dot(point, normal)};
}
} // namespace SYN
