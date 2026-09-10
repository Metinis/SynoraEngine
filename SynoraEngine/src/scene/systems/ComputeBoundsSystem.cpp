#include "ComputeBoundsSystem.h"

#include <SynoraEngine/core/Application.h>
#include <SynoraEngine/scene/SceneManager.h>

#include <SynoraEngine/scene/components/BoundsComponent.h>
#include <SynoraEngine/scene/components/ModelComponent.h>
#include <SynoraEngine/scene/components/SkeletalAnimationComponent.h>
#include <SynoraEngine/scene/components/TransformComponent.h>

#include <SynoraEngine/project/AssetManager.h>
#include <SynoraEngine/project/assets/ModelData.h>

namespace SYN {
void ComputeBoundsSystem::init(class EngineContext *ctx) {
    m_SceneManager = ctx->sceneManager.get();
    m_AssetManager = ctx->projectConfig.assetManager.get();
}

// TODO: Add dirty flag so aabbs for static meshes aren't recomputed every
// frame.
void ComputeBoundsSystem::onUpdate(float dt) {
    SceneHandle currentSceneHandle = m_SceneManager->getActiveScene();
    if (!m_SceneManager->isSceneValid(currentSceneHandle))
        return;
    Scene *currentSceneRef = m_SceneManager->getSceneMut(currentSceneHandle);

    std::vector<Entity> boundsToRemove;

    auto computeAnimatedAABB = [](const MeshData &meshData,
                                  const std::vector<glm::mat4> *jointMatrices =
                                      nullptr,
                                  const Skeleton *skeleton = nullptr) -> AABB {
        if (meshData.boneAABBs.empty())
            return meshData.aabb;

        AABB animated = AABB::empty();

        for (auto [boneIndex, boneAABB] : meshData.boneAABBs) {
            AABB localAABB = boneAABB;

            glm::mat4 jointMatrix = jointMatrices->at(boneIndex);

            AABB current = localAABB.transform(jointMatrix);

            animated = animated.unionWith(current);
        }
        return animated;
    };

    auto computeMeshAABB =
        [&computeAnimatedAABB](
            const MeshData &meshData, glm::mat4 worldTransform,
            const std::vector<glm::mat4> *jointMatrices = nullptr,
            const Skeleton *skeleton = nullptr) -> AABB {
        AABB meshAABB;

        if (jointMatrices != nullptr && skeleton != nullptr)
            meshAABB = computeAnimatedAABB(meshData, jointMatrices, skeleton);
        else
            meshAABB = meshData.aabb;

        return meshAABB.transform(worldTransform * meshData.localTransform);
    };

    // If a model component was just added to an entity, add a corresponding
    // bounds component.
    currentSceneRef->forEach<ModelComponent>(
        [](Entity entity, ModelComponent &modelComp) {
            if (!entity.hasComponent<BoundsComponent>()) {
                entity.addComponent<BoundsComponent>();
            }
        });

    currentSceneRef->forEach<BoundsComponent>(
        [&boundsToRemove, &computeMeshAABB, &currentSceneRef,
         this](Entity entity, BoundsComponent &boundsComp) {
            // BoundsComponent must have a corresponding model component. If
            // that model component was removed then remove the bounds
            // component too.
            if (!entity.hasComponent<ModelComponent>()) {
                boundsToRemove.push_back(entity);
                return;
            }

            boundsComp.meshBounds.clear();

            auto &modelComp = entity.getComponent<ModelComponent>();
            UUID modelId = modelComp.model.uuid();

            const ModelData *modelData =
                m_AssetManager->get<ModelData>(modelId);

            const std::vector<glm::mat4> *jointMatrices = nullptr;
            const SkeletalAnimationComponent *animationComp =
                entity.tryGetComponent<SkeletalAnimationComponent>();
            const Skeleton *skeleton = nullptr;
            if (animationComp != nullptr) {
                jointMatrices = &animationComp->player.getOutput();
                skeleton = &modelData->skeleton;
            }

            glm::mat4 world = currentSceneRef->getWorldTransformOf(entity);
            for (const MeshData &meshData : modelData->meshes)
                boundsComp.meshBounds.emplace_back(
                    computeMeshAABB(meshData, world, jointMatrices, skeleton));
        });

    for (Entity &entity : boundsToRemove) {
        entity.removeComponent<BoundsComponent>();
    }
}
} // namespace SYN
