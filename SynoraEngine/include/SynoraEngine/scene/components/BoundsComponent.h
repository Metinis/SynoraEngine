#pragma once

#include <SynoraEngine/core/math/AABB.h>

namespace SYN {
struct BoundsComponent {
    std::vector<AABB> meshBounds;
};
} // namespace SYN
