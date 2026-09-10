#pragma once

#include "../AssetRef.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <SynoraEngine/core/math/AABB.h>

namespace SYN {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
    glm::ivec4 boneIndices = glm::ivec4(0);
    glm::vec4 boneWeights = glm::vec4(0);
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    AssetRef material;

    glm::mat4 localTransform = glm::mat4(1.0);

    bool hasSkin = false;

    AABB aabb;

    // Bone index and bone AABB
    std::vector<std::pair<uint32_t, AABB>> boneAABBs;
};

struct Skeleton {
    struct Node {
        std::string name;
        std::optional<uint32_t> parentIndex;

        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };

    struct BoneInfo {
        uint32_t nodeIndex;
        glm::mat4 inverseBindMatrix = glm::mat4(1.0f);
    };

    std::vector<Node> nodes;

    std::vector<BoneInfo> boneInfo;

    glm::mat4 inverseRoot = glm::mat4(1.0f);
};

struct ModelData {
    std::vector<MeshData> meshes;
    Skeleton skeleton;
};

} // namespace SYN
