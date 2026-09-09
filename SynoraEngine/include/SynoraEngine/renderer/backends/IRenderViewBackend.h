#pragma once

#include <SynoraEngine/scene/view/RenderView3D.h>

#include <SynoraEngine/renderer/DebugDraw.h>

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

    virtual void onBeginFrame() {}
    virtual void onEndFrame() {}

    virtual void submitFrame(const RenderView3D &sceneDescription) = 0;

    virtual void submitLineList(const std::vector<DebugDraw::Line> &lines,
                                bool depthTest) {};

    virtual void drawScene() = 0;
};
} // namespace SYN
