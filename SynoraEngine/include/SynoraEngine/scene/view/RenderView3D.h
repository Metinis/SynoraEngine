#pragma once

#include <glm/glm.hpp>

#include <SynoraEngine/project/UUID.h>

#include <SynoraEngine/core/math/AABB.h>

namespace SYN {
struct CameraView {
    glm::mat4 worldTransform;
    float fov;
    float aspect;
    float near;
    float far;
    bool isPrimary;
};

struct MaterialView {
    uint32_t modelIndex;
    uint32_t meshIndex;
    UUID material;
};

struct AnimationView {
    uint32_t modelIndex;
    std::span<const glm::mat4> boneMatrices;
};

struct BoundsView {
    std::vector<AABB> meshBounds;
};

struct RenderView3D {
    std::vector<UUID> models;
    std::vector<BoundsView> bounds;
    std::vector<glm::mat4> transforms;
    std::vector<CameraView> cameras;
    std::vector<MaterialView> materials;
    std::vector<AnimationView> animations;
};
}; // namespace SYN
