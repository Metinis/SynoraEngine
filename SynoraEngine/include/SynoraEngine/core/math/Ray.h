#pragma once

#include <SynoraEngine/scene/components/CameraComponent.h>
#include <SynoraEngine/scene/components/TransformComponent.h>

#include "AABB.h"
#include "Plane.h"
#include "Sphere.h"

namespace SYN {
struct Ray {
    struct Hit {
        float distance;
        glm::vec3 normal;
        glm::vec3 position;
    };

    glm::vec3 position;
    glm::vec3 direction;

    glm::vec3 positionAt(float t) const;
    static Ray from(glm::vec3 position, glm::vec3 direction);
    static Ray screenToWorld(glm::mat4 viewProjection, glm::vec3 cameraPosition,
                             glm::vec2 screenPos, uint32_t width,
                             uint32_t height);

    std::optional<Hit> collidesWithAABB(AABB aabb) const;
    std::optional<Hit> collidesWithSphere(Sphere sphere) const;
    std::optional<Hit> collidesWithPlane(Plane plane) const;
};
} // namespace SYN
