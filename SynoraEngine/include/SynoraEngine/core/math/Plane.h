#pragma once

#include <glm/vec3.hpp>

namespace SYN {
struct Plane {
    float a;
    float b;
    float c;
    float d;

    static Plane from(glm::vec3 point, glm::vec3 normal);
};
} // namespace SYN
