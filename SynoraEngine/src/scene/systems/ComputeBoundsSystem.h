#pragma once

#include <SynoraEngine/core/ILayer.h>

namespace SYN {
class ComputeBoundsSystem : public ILayer {
  public:
    ComputeBoundsSystem() = default;

    void init(class EngineContext *ctx);
    void onUpdate(float dt) override;

    void onAttach() override {};
    void onDettach() override {};
    void onRender() override {};
    void onUIRender() override {};

    ~ComputeBoundsSystem() = default;

  private:
    class SceneManager *m_SceneManager;
    class AssetManager *m_AssetManager;
};
}; // namespace SYN
