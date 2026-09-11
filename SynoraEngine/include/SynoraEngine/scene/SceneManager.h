#pragma once

#include "../core/ILayer.h"

#include "Scene.h"
#include "SceneHandle.h"

namespace SYN {

class SceneManager : public ILayer {
  public:
    SceneManager();
    ~SceneManager() = default;

    SceneHandle createScene(std::string_view name,
                            std::optional<uint32_t> priority = std::nullopt);
    void removeScene(SceneHandle &handle);

    // Increments to next scene in ordered list. Doesn't switch if already at
    // end.
    void nextScene(float delay = 0.0f);

    // Decrements to previous scene in ordered list. Doesn't switch if already
    // at start.
    void previousScene(float delay = 0.0f);

    SceneHandle getPrevious() const;
    SceneHandle getNext() const;

    // This function defers the actual scene switch to the end of the frame when
    // all updates and renders have finished.
    void switchTo(SceneHandle scene, float delay = 0.0f);

    std::vector<SceneHandle> getAllScenes() const;
    SceneHandle getActiveScene() const;
    SceneHandle findScene(std::string_view name) const;
    Scene *getSceneMut(SceneHandle handle);
    const Scene *getScene(SceneHandle handle) const;

    bool isSceneValid(SceneHandle scene) const;

  public:
    void onAttach() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onUIRender() override {};
    void onDettach() override;

  private:
    bool isSceneActive(SceneHandle scene) const;

    // Not next in the ordered list, but next to be active
    // as the scene manager is transitioning to it.
    bool isSceneNext(SceneHandle scene) const;

    friend class Application;
    void handleSwitch();

  private:
    std::unordered_map<std::string, SceneHandle> m_NameToHandle;

    std::optional<SceneHandle> m_CurrentScene = std::nullopt;

    std::optional<SceneHandle> m_NextScene = std::nullopt;
    float m_SceneSwitchDelay = 0.0f;

    std::vector<std::tuple<SceneHandle, uint32_t>> m_OrderedSceneList;

    std::vector<std::unique_ptr<Scene>> m_Scenes;
    std::vector<uint32_t> m_CurrentGeneration;
    std::vector<SceneHandle> m_FreeHandles;
};
} // namespace SYN
