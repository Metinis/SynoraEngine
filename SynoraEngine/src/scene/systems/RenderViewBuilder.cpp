#include "RenderViewBuilder.h"

#include <SynoraEngine/core/Application.h>
#include <SynoraEngine/renderer/DebugDraw.h>
#include <SynoraEngine/renderer/backends/IRenderViewBackend.h>
#include <SynoraEngine/scene/SceneManager.h>
#include <SynoraEngine/scene/components/Components.h>
#include <SynoraEngine/scene/view/RenderView3D.h>
#include <spdlog/spdlog.h>

namespace SYN {
void RenderViewBuilder::init(EngineContext *context) {
    m_SceneManager = context->sceneManager.get();
    m_Renderer = context->renderer.get();
    m_DebugDraw = context->debugDraw.get();
}

void RenderViewBuilder::onAttach() {
    spdlog::debug("RenderViewBuilder: attached");
}

void RenderViewBuilder::onUpdate(float dt) {}

void RenderViewBuilder::onRender() {
    SceneHandle currentSceneHandle = m_SceneManager->getActiveScene();
    if (!m_SceneManager->isSceneValid(currentSceneHandle))
        return;
    Scene *scene = m_SceneManager->getSceneMut(currentSceneHandle);

    RenderView3D renderView;

    scene->forEach<CameraComponent>([&](Entity entity,
                                        CameraComponent &camera) {
        glm::mat4 worldTransform = scene->getWorldTransformOf(entity);
        renderView.cameras.emplace_back(worldTransform, camera.fovDegrees,
                                        camera.aspectRatio, camera.nearPlane,
                                        camera.farPlane, camera.isPrimary);
    });

    scene->forEach<ModelComponent, BoundsComponent, TransformComponent>(
        [&](Entity entity, ModelComponent &model, BoundsComponent &bounds,
            TransformComponent &transform) {
            renderView.models.push_back(model.model.uuid());
            renderView.bounds.emplace_back(bounds.meshBounds);

            uint32_t modelIndex = renderView.models.size() - 1;

            glm::mat4 worldTransform = scene->getWorldTransformOf(entity);
            renderView.transforms.push_back(worldTransform);
            if (entity.hasComponent<MaterialComponent>()) {
                const MaterialComponent &material =
                    entity.getComponent<MaterialComponent>();
                for (const MaterialComponent::Submesh &submesh :
                     material.submeshes) {
                    renderView.materials.emplace_back(
                        modelIndex, submesh.meshIndex, submesh.material.uuid());
                }
            }
            if (entity.hasComponent<SkeletalAnimationComponent>()) {
                const SkeletalAnimationComponent &animation =
                    entity.getComponent<SkeletalAnimationComponent>();
                renderView.animations.emplace_back(
                    modelIndex, animation.player.getOutput());
            }
        });

    if (!m_DebugDraw->m_LinesDepth.empty()) {
        m_Renderer->submitLineList(m_DebugDraw->m_LinesDepth, true);
    }

    if (!m_DebugDraw->m_LinesNoDepth.empty()) {
        m_Renderer->submitLineList(m_DebugDraw->m_LinesNoDepth, false);
    }

    m_Renderer->submitFrame(renderView);
}

void RenderViewBuilder::onUIRender() {}

void RenderViewBuilder::onDettach() {
    spdlog::debug("RenderViewBuilder: detached");
}
} // namespace SYN
