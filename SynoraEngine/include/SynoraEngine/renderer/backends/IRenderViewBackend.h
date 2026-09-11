#pragma once

#include <SynoraEngine/renderer/DebugDraw.h>
#include <SynoraEngine/scene/components/CameraComponent.h>
#include <SynoraEngine/scene/view/RenderView3D.h>

#include <imgui.h>

namespace SYN {
// This interface takes in backend neutral scene data and decides
// how to render it. This is not meant to abstract a graphics API,
// but instead a full renderer which may come in any form.

class IRenderViewBackend {
  public:
    IRenderViewBackend() = default;
    virtual ~IRenderViewBackend() {}

    virtual void init(class EngineContext *context) {}
    virtual void shutdown() {}

    virtual void beforeDraw() {}
    virtual void afterDraw() {}

    virtual std::optional<ImTextureID>
    getHandleForImGui(UUID renderTarget,
                      std::optional<uint32_t> attachmentIndex) {
        return std::nullopt;
    };

    virtual void beginFrame(const RenderView3D &sceneDescription) = 0;

    // if renderTarget is not specified then draw to default framebuffer
    virtual void draw(CameraComponent camera, glm::mat4 cameraTransform,
                      std::optional<UUID> renderTarget) = 0;

    virtual void endFrame() = 0;

    virtual void submitLineList(const std::vector<DebugDraw::Line> &lines,
                                bool depthTest) {};
};
} // namespace SYN
