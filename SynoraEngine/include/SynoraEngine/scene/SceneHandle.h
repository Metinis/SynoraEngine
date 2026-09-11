#pragma once

namespace SYN {
struct SceneHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    bool operator==(SceneHandle other) const {
        return index == other.index && generation == other.generation;
    }
};
} // namespace SYN
