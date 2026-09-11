#include "CameraSystem.h"
#include "../../include/SynoraEngine/renderer/Renderer.h"
#include "SynoraEngine/core/Input.h"
#include "SynoraEngine/core/InputContext.h"
#include "SynoraEngine/core/InputTypes.h"
#include "SynoraEngine/core/Window.h"
#include "SynoraEngine/scene/SceneManager.h"
#include "SynoraEngine/scene/components/CameraComponent.h"
#include "SynoraEngine/scene/components/TransformComponent.h"
#include "spdlog/spdlog.h"

namespace {
enum Action : SYN::ActionID {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Run,
    LookDelta,
    ToggleCursor
};
}

SYN::CameraSystem::CameraSystem() {}

SYN::Entity SYN::CameraSystem::getCameraEntity() {
    SceneHandle sceneHandle = m_SceneManager->getActiveScene();
    if (!m_SceneManager->isSceneValid(sceneHandle))
        return Entity();

    Scene *scene = m_SceneManager->getSceneMut(sceneHandle);
    for (auto e : scene->getEntities<CameraComponent>()) {
        if (e.getComponent<CameraComponent>().isPrimary) {
            return e;
        }
    }
    return Entity();
}

SYN::Camera SYN::CameraSystem::getCamera() {
    auto camEntity = getCameraEntity();

    if (!camEntity.isValid()) {
        spdlog::warn("Scene: No camera entity");
        return Camera();
    }

    auto camTC = camEntity.getComponent<TransformComponent>();
    auto camComp = camEntity.getComponent<CameraComponent>();
    Camera cam = {
        .transform = camTC,
        .fovDegrees = camComp.fovDegrees,
        .aspectRatio = camComp.aspectRatio,
        .nearPlane = camComp.nearPlane,
        .farPlane = camComp.farPlane,
    };
    return cam;
}

void SYN::CameraSystem::onAttach() { spdlog::debug("Camera System: Attached"); }

void SYN::CameraSystem::onDettach() {
    spdlog::debug("Camera System: Detached");
}

void SYN::CameraSystem::onUIRender() {}

void SYN::CameraSystem::flyCamera(float dt, Entity *cameraEntity) {
    if (m_LastCursorHiddenState != m_CursorHidden) {
        m_DyRot = 0;
        m_DxRot = 0;
    }
    m_LastCursorHiddenState = m_CursorHidden;

    if (!m_CursorHidden) {
        return;
    }

    float speed{m_WalkSpeed};
    if (m_IsRunning) {
        speed *= m_RunMultiplier;
    }

    auto &camTC = cameraEntity->getComponent<TransformComponent>();

    glm::vec3 lookDir{camTC.rotation * glm::vec3(0.f, 0.f, -1.f)};
    lookDir.y = 0.f;
    lookDir = glm::normalize(lookDir);

    glm::vec3 upDir(0.f, 1.f, 0.f);
    glm::vec3 sideDir{glm::cross(lookDir, upDir)};

    glm::vec3 dPos{(lookDir * m_Dz) + (sideDir * m_Dx) + (upDir * -m_Dy)};
    dPos *= dt * speed;

    camTC.position += dPos;

    float sensitivity{0.1f};
    glm::quat yaw{
        glm::angleAxis(glm::radians(static_cast<float>(m_DxRot * sensitivity)),
                       glm::vec3(0.f, 1.f, 0.f))};
    glm::vec3 forward(0.0, 0.0, -1.0);
    forward = camTC.rotation * forward;
    float currentPitch = glm::degrees(asin(glm::clamp(forward.y, -1.0f, 1.0f)));
    float target =
        glm::clamp(currentPitch - m_DyRot * sensitivity, -89.0f, 89.0f);
    float actualPitch = target - currentPitch;

    glm::quat pitch{
        glm::angleAxis(glm::radians(actualPitch), glm::vec3(1.f, 0.f, 0.f))};

    camTC.rotation = glm::normalize(yaw * camTC.rotation * pitch);

    m_DyRot = 0;
    m_DxRot = 0;
}

void SYN::CameraSystem::onUpdate(float dt) {
    auto camera = getCameraEntity();

    if (!camera.isValid()) {
        return;
    }

    if (camera.hasComponent<FlyCameraComponent>()) {
        flyCamera(dt, &camera);
    }
}

void SYN::CameraSystem::onRender() {}

void SYN::CameraSystem::init(EngineContext *ctx) {
    m_Window = ctx->window.get();
    m_SceneManager = ctx->sceneManager.get();

    std::optional<SYN::InputContextHandle> gameplayHandle{
        ctx->inputManager.get()->addInputContext(0)};

    InputContext *gameplayCtx{ctx->inputManager.get()
                                  ->getInputContext(gameplayHandle.value())
                                  .value()};

    gameplayCtx->setConsumesInput(false);
    gameplayCtx->bindActions(SYN::InputKey::W, {{Action::MoveForward, {}}});
    gameplayCtx->bindActions(SYN::InputKey::S, {{Action::MoveBackward, {}}});
    gameplayCtx->bindActions(SYN::InputKey::A, {{Action::MoveLeft, {}}});
    gameplayCtx->bindActions(SYN::InputKey::D, {{Action::MoveRight, {}}});
    gameplayCtx->bindActions(SYN::InputKey::Space, {{Action::MoveUp, {}}});
    gameplayCtx->bindActions(SYN::InputKey::LeftCtrl, {{Action::MoveDown, {}}});
    gameplayCtx->bindActions(SYN::InputKey::LeftShift, {{Action::Run, {}}});
    gameplayCtx->bindActions(SYN::RawInputType::MouseDelta,
                             {{Action::LookDelta, {}}});
    gameplayCtx->bindActions(SYN::InputKey::Escape,
                             {{Action::ToggleCursor, {}}});
    gameplayCtx->addInputVector("GroundMovement",
                                {Action::MoveForward, Action::MoveBackward,
                                 Action::MoveRight, Action::MoveLeft});

    gameplayCtx->addInputVector("FlyMovement",
                                {Action::MoveUp, Action::MoveDown});

    gameplayCtx->addInputVectorCallback("FlyMovement",
                                        [&](float x, float y) { m_Dy = -y; });

    gameplayCtx->addInputVectorCallback("GroundMovement",
                                        [&](float x, float z) {
                                            m_Dx = x;
                                            m_Dz = z;
                                        });

    gameplayCtx->addActionCallback(Action::ToggleCursor,
                                   [&](InputState inputState) {
                                       if (inputState == InputState::Up) {
                                           return;
                                       }

                                       if (m_CursorHidden) {
                                           m_Window->enableCursor();
                                       } else {
                                           m_Window->disableCursor();
                                       }
                                       m_CursorHidden = !m_CursorHidden;
                                   });

    gameplayCtx->addActionCallback(Action::Run, [&](InputState inputState) {
        if (inputState == InputState::Down) {
            m_IsRunning = true;
        } else {
            m_IsRunning = false;
        }
    });

    gameplayCtx->addActionCallback(Action::LookDelta,
                                   [&](double dx, double dy) {
                                       m_DxRot -= dx;
                                       m_DyRot += dy;
                                   });
}
