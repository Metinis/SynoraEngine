#include <SynoraEngine/core/math/Ray.h>

namespace SYN {
Ray Ray::from(glm::vec3 position, glm::vec3 direction) {
    return {position, glm::normalize(direction)};
}

Ray Ray::screenToWorld(CameraComponent camera, TransformComponent transform,
                       glm::vec2 screenPos, uint32_t width, uint32_t height) {
    glm::vec3 ndc;
    ndc.x = (2.0f * screenPos.x) / (float)width - 1.0f;
    ndc.y = 1.0f - (2.0f * screenPos.y) / (float)height;
    ndc.z = 1.0f;

    glm::vec3 forward = transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 up = transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 view =
        glm::lookAtRH(transform.position, transform.position + forward, up);

    glm::mat4 proj = glm::perspectiveRH_NO(glm::radians(camera.fovDegrees),
                                           camera.aspectRatio, camera.nearPlane,
                                           camera.farPlane);

    glm::mat4 invCam = glm::inverse(proj * view);

    glm::vec4 nearPoint = invCam * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    glm::vec4 farPoint = invCam * glm::vec4(ndc.x, ndc.y, ndc.z, 1.0f);

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    glm::vec3 dir = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));

    return Ray::from(transform.position, dir);
}

std::optional<Ray::Hit> Ray::collidesWithAABB(AABB aabb) const {
    float maxVal = std::numeric_limits<float>().max();

    float tMin = -maxVal;
    float tMax = maxVal;

    float epsilon = std::numeric_limits<float>().epsilon();

    uint32_t entryAxis = 0;
    uint32_t exitAxis = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        if (glm::abs(direction[i]) < epsilon) {
            if (position[i] < aabb.min[i] || position[i] > aabb.max[i])
                return std::nullopt;
            continue;
        }

        float dirInv = 1.0f / direction[i];
        float t0 = (aabb.min[i] - position[i]) * dirInv;
        float t1 = (aabb.max[i] - position[i]) * dirInv;

        if (t0 > t1)
            std::swap(t0, t1);

        if (t0 > tMin) {
            entryAxis = i;
            tMin = t0;
        }

        if (t1 < tMax) {
            exitAxis = i;
            tMax = t1;
        }

        if (tMin > tMax)
            return std::nullopt;
    }

    bool isInside = false;
    if (tMin < 0.0f) {
        tMin = tMax;
        isInside = true;
        if (tMin < 0.0f)
            return std::nullopt;
    }

    Hit info;
    info.distance = tMin;
    info.position = position + direction * info.distance;
    info.normal = glm::vec3(0.0f);
    uint32_t hitAxis = isInside ? exitAxis : entryAxis;
    info.normal[hitAxis] = isInside ? glm::sign(direction[hitAxis])
                                    : -glm::sign(direction[hitAxis]);

    return info;
}

std::optional<Ray::Hit> Ray::collidesWithSphere(Sphere sphere) const {
    glm::vec3 L = position - sphere.center;
    float a = glm::dot(direction, direction);
    float b = 2.0f * glm::dot(direction, L);
    float c = glm::dot(L, L) - (sphere.radius * sphere.radius);

    float tMin, tMax;

    float discriminant = b * b - 4 * a * c;
    if (discriminant < 0.0f)
        return std::nullopt;
    else if (discriminant == 0.0f)
        tMin = tMax = -0.5f * b / a;
    else {
        float q = -0.5 * (b + glm::sign(b) * sqrtf(discriminant));
        if (q == 0.0f) {
            float sqrtDisc = std::sqrt(discriminant);
            tMin = -b + sqrtDisc * -0.5f;
            tMax = -b - sqrtDisc * -0.5f;
        } else {
            tMin = q / a;
            tMax = c / q;
        }
    }

    if (tMin > tMax)
        std::swap(tMin, tMax);
    if (tMin < 0.0f) {
        tMin = tMax;
        if (tMin < 0.0f)
            return std::nullopt;
    }

    Hit info;
    info.distance = tMin;
    info.position = position + direction * info.distance;
    info.normal = glm::normalize(info.position - sphere.center);

    return info;
}

std::optional<Ray::Hit> Ray::collidesWithPlane(Plane plane) const {
    glm::vec3 normal = glm::normalize(glm::vec3(plane.a, plane.b, plane.c));

    float denom = glm::dot(normal, direction);

    if (glm::abs(denom) < std::numeric_limits<float>().epsilon()) {
        return std::nullopt;
    }

    float tMin = (-plane.d - glm::dot(normal, position)) / denom;

    if (tMin <= 0.0f)
        return std::nullopt;

    Hit info;
    info.distance = tMin;
    info.position = position + direction * info.distance;
    info.normal = denom > 0.0f ? -normal : normal;

    return info;
}

} // namespace SYN
