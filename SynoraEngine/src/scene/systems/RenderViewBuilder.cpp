#include "RenderViewBuilder.h"

#include <queue>

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

    struct RenderTargetSubmission {
        CameraComponent camera;
        glm::mat4 transform;
        UUID target;
    };

    struct {
        bool operator()(const RenderTargetSubmission &a,
                        const RenderTargetSubmission &b) const {
            return a.camera.depth > b.camera.depth;
        }
    } renderTargetSubmissionCompare;

    std::vector<RenderTargetSubmission> renderTargetList;
    scene->forEach<CameraComponent, TransformComponent, RenderTargetComponent>(
        [&](Entity entity, CameraComponent &camera,
            TransformComponent &transform,
            RenderTargetComponent &renderTarget) {
            glm::mat4 cameraTransform = scene->getWorldTransformOf(entity);
            if (((camera.renderMode == CameraComponent::RenderMode::OnDemand) &&
                 camera.dirty) ||
                (camera.renderMode ==
                 CameraComponent::RenderMode::Continuous)) {
                renderTargetList.emplace_back(camera, cameraTransform,
                                              renderTarget.renderTarget.uuid());
            }
        });

    std::priority_queue renderTargetQueue(renderTargetList.cbegin(),
                                          renderTargetList.cend(),
                                          renderTargetSubmissionCompare);

    bool primaryAdded = false;
    RenderTargetSubmission primaryFrame;
    primaryFrame.transform = glm::mat4(1.0f);
    scene->forEach<CameraComponent, TransformComponent>(
        [&](Entity entity, CameraComponent &camera,
            TransformComponent &transform) {
            if (camera.isPrimary && primaryAdded) {
                spdlog::warn(
                    "A scene should only have one primary camera. This "
                    "scene has more than one. Extra primary cameras do not "
                    "contribute anything to the scene.");
                return;
            }
            if (camera.isPrimary) {
                primaryAdded = true;
                primaryFrame.camera = camera;
                primaryFrame.transform = scene->getWorldTransformOf(entity);
            }
        });

    RenderView3D mainSceneView = RenderView3D::fromScene(scene);
    m_Renderer->beginFrame(mainSceneView);

    if (!m_DebugDraw->m_LinesDepth.empty()) {
        m_Renderer->submitLineList(m_DebugDraw->m_LinesDepth, true);
    }

    if (!m_DebugDraw->m_LinesNoDepth.empty()) {
        m_Renderer->submitLineList(m_DebugDraw->m_LinesNoDepth, false);
    }

    while (!renderTargetQueue.empty()) {
        RenderTargetSubmission command = renderTargetQueue.top();
        m_Renderer->draw(command.camera, command.transform, command.target);
        renderTargetQueue.pop();
    }

    m_Renderer->draw(primaryFrame.camera, primaryFrame.transform, std::nullopt);

    m_Renderer->endFrame();
}

void RenderViewBuilder::onUIRender() {}

void RenderViewBuilder::onDettach() {
    spdlog::debug("RenderViewBuilder: detached");
}
} // namespace SYN
