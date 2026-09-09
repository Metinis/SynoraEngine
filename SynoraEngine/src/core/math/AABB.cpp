#include <SynoraEngine/core/math/AABB.h>

namespace SYN {
glm::vec3 AABB::getPVertex(glm::vec3 normal) const {
    AABB aabb = *this;

    float x = normal.x >= 0 ? aabb.max.x : aabb.min.x;
    float y = normal.y >= 0 ? aabb.max.y : aabb.min.y;
    float z = normal.z >= 0 ? aabb.max.z : aabb.min.z;

    return glm::vec3(x, y, z);
}

glm::vec3 AABB::getNVertex(glm::vec3 normal) const {
    AABB aabb = *this;

    float x = normal.x >= 0 ? aabb.min.x : aabb.max.x;
    float y = normal.y >= 0 ? aabb.min.y : aabb.max.y;
    float z = normal.z >= 0 ? aabb.min.z : aabb.max.z;

    return glm::vec3(x, y, z);
}

AABB AABB::transform(glm::mat4 transform) const {
    AABB aabb = *this;

    glm::vec3 min = aabb.min;
    glm::vec3 max = aabb.max;

    float lengthX = max.x - min.x;
    float lengthY = max.y - min.y;
    float lengthZ = max.z - min.z;

    std::array<glm::vec3, 8> worldPoints = {
        min,
        min + glm::vec3(lengthX, 0, 0),
        min + glm::vec3(lengthX, 0, lengthZ),
        min + glm::vec3(0, 0, lengthZ),
        min + glm::vec3(0, lengthY, 0),
        min + glm::vec3(lengthX, lengthY, 0),
        min + glm::vec3(0, lengthY, lengthZ),
        max};

    for (glm::vec3 &worldPoint : worldPoints) {
        worldPoint = transform * glm::vec4(worldPoint, 1.0f);
    }

    float minFloat = std::numeric_limits<float>().lowest();
    float maxFloat = std::numeric_limits<float>().max();

    glm::vec3 newMin = worldPoints[0];
    glm::vec3 newMax = newMin;

    for (uint32_t i = 1; i < worldPoints.size(); ++i) {
        glm::vec3 worldPoint = worldPoints.at(i);
        newMin.x = glm::min(newMin.x, worldPoint.x);
        newMin.y = glm::min(newMin.y, worldPoint.y);
        newMin.z = glm::min(newMin.z, worldPoint.z);

        newMax.x = glm::max(newMax.x, worldPoint.x);
        newMax.y = glm::max(newMax.y, worldPoint.y);
        newMax.z = glm::max(newMax.z, worldPoint.z);
    }

    return {newMin, newMax};
}

} // namespace SYN
