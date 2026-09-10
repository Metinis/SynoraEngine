#pragma once
#include "ILayer.h"
#include "SynoraEngine/project/Project.h"
#include "WindowConfig.h"
#define GLM_ENABLE_EXPERIMENTAL

namespace SYN {
class Window;
class Input;
class IRenderViewBackend;
class SceneManager;

class CameraSystem;
class AnimationPlayerSystem;
class ComputeBoundsSystem;
class RenderViewBuilder;
class DebugDraw;

namespace gfx {
namespace gl {
class Context;
}
} // namespace gfx

struct EngineContext {
    std::unique_ptr<Window> window;
    std::unique_ptr<gfx::gl::Context> glContext;
    ProjectConfig projectConfig;
    std::unique_ptr<Input> inputManager;
    std::unique_ptr<DebugDraw> debugDraw;
    std::unique_ptr<IRenderViewBackend> renderer;
    std::unique_ptr<SceneManager> sceneManager;

    std::unique_ptr<RenderViewBuilder> renderViewBuilder;
    std::unique_ptr<ComputeBoundsSystem> computeBoundsSystem;
    std::unique_ptr<AnimationPlayerSystem> animationPlayerSystem;
    std::unique_ptr<CameraSystem> cameraSystem;

    WindowConfig windowConfig;
};

class Application {
  public:
    Application();
    virtual void init();
    void run();
    void shutdown();
    static Application &get() { return *s_Instance; }
    virtual ~Application();

    std::unique_ptr<Input> &GetInput();

  protected:
    static Application *s_Instance;

    EngineContext m_EngineContext;

    std::deque<ILayer *> m_Layers;

    bool m_IsRunning{};
};
} // namespace SYN
