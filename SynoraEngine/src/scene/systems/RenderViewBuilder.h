#pragma once

#include <SynoraEngine/core/ILayer.h>

namespace SYN {
class RenderViewBuilder : public ILayer {
  public:
    ~RenderViewBuilder() = default;

    void init(class EngineContext *context);

    void onAttach() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onUIRender() override;
    void onDettach() override;

  private:
    class SceneManager *m_SceneManager;
    class DebugDraw *m_DebugDraw;
    class IRenderViewBackend *m_Renderer;
};
} // namespace SYN
