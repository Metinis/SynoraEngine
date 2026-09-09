#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <SynoraEngine/core/Application.h>
#include <SynoraEngine/core/Window.h>
#include <SynoraEngine/gfx/gl/GL.h>
#include <SynoraEngine/project/AssetManager.h>
#include <SynoraEngine/project/assets/AnimationClipData.h>
#include <SynoraEngine/project/assets/MaterialData.h>
#include <SynoraEngine/project/assets/ModelData.h>
#include <SynoraEngine/project/assets/TextureData.h>
#include <SynoraEngine/scene/3d/AnimationPlayer.h>
#include <SynoraEngine/scene/SceneManager.h>

#include <SynoraEngine/scene/components/Components.h>

using namespace SYN::gfx;

constexpr uint32_t WINDOW_WIDTH = 1920;
constexpr uint32_t WINDOW_HEIGHT = 1080;

class GraphicsScene : public SYN::ILayer {
  public:
    GraphicsScene() {}

    void onAttach() override {};
    void onDettach() override {};
    void init(SYN::EngineContext *engineContext) {
        m_Context = engineContext->glContext.get();
        m_Window = engineContext->window.get();
        m_SceneManager = engineContext->sceneManager.get();

        m_Context->enableVSync(false);

        SYN::AssetManager *assetManager =
            engineContext->projectConfig.assetManager.get();

        m_Renderer = static_cast<gl::Renderer *>(engineContext->renderer.get());

        m_DebugDraw = engineContext->debugDraw.get();

        m_SphereScene = m_SceneManager->createScene("Sphere");
        m_CabinScene = m_SceneManager->createScene("Cabin");

        m_Renderer->setRenderScale(0.7f);

        m_CSMLayers = m_Renderer->getCSMTextures(*m_Context);

        m_Waltuh =
            assetManager->load<SYN::ModelData>("resources/assets/waltuh.glb")
                .value();

        m_Cabin =
            assetManager
                ->load<SYN::ModelData>("resources/assets/Cabin/scene.gltf")
                .value();

        m_Sphere =
            assetManager->load<SYN::ModelData>("resources/assets/Sphere.glb")
                .value();

        m_Dancer =
            assetManager->load<SYN::ModelData>("resources/assets/dancer.glb")
                .value();

        m_DancerClips = {assetManager
                             ->loadWithKey<SYN::AnimationClipData>(
                                 "resources/assets/dancer.glb", "dancerAnim")
                             .value()};

        m_Parasite =
            assetManager->load<SYN::ModelData>("resources/assets/parasite.glb")
                .value();
        m_ParasiteClips = assetManager
                              ->loadGroup<SYN::AnimationClipData>(
                                  "resources/assets/parasite.glb")
                              .value();
        assert(m_ParasiteClips.size() > 0 &&
               "Unable to load parasite animations!");
        m_Weight = 0.0f;

        m_Renderer->setDirectionalLight(m_Light);

        SYN::UUID nx =
            assetManager
                ->load<SYN::TextureData>("resources/assets/GhibliSkybox/nx.png")
                .value();
        SYN::UUID ny =
            assetManager
                ->load<SYN::TextureData>("resources/assets/GhibliSkybox/ny.png")
                .value();
        SYN::UUID nz =
            assetManager
                ->load<SYN::TextureData>("resources/assets/GhibliSkybox/nz.png")
                .value();
        SYN::UUID px =
            assetManager
                ->load<SYN::TextureData>("resources/assets/GhibliSkybox/px.png")
                .value();
        SYN::UUID py =
            assetManager
                ->load<SYN::TextureData>("resources/assets/GhibliSkybox/py.png")
                .value();
        SYN::UUID pz =
            assetManager
                ->load<SYN::TextureData>("resources/assets/GhibliSkybox/pz.png")
                .value();

        m_Environments[0] = {gl::Environment::Type::Cubemap,
                             glm::vec4(1.0f),
                             {px, nx, py, ny, pz, nz},
                             1.0f,
                             2.2f,
                             true};
        m_EnvironmentNames[0] = "Ghibli";

        stbi_set_flip_vertically_on_load(true);
        SYN::UUID hdr = assetManager
                            ->load<SYN::TextureData>(
                                "resources/assets/cowboy_town_saloon_4k.hdr")
                            .value();

        m_Environments[1].type = gl::Environment::Type::HdrMap;
        m_Environments[1].cubemap = {hdr};
        m_EnvironmentNames[1] = "Saloon";

        hdr = assetManager
                  ->load<SYN::TextureData>(
                      "resources/assets/suburban_garden_4k.hdr")
                  .value();

        m_Environments[2].type = gl::Environment::Type::HdrMap;
        m_Environments[2].cubemap = {hdr};
        m_EnvironmentNames[2] = "Suburbs";

        hdr = assetManager->load<SYN::TextureData>("resources/assets/lobby.hdr")
                  .value();

        m_Environments[3].type = gl::Environment::Type::HdrMap;
        m_Environments[3].cubemap = {hdr};
        m_EnvironmentNames[3] = "Lobby";

        stbi_set_flip_vertically_on_load(false);

        for (uint32_t i = 0; i < m_EnvironmentNames.size(); ++i) {
            m_Renderer->createEnvironment(*m_Context, m_EnvironmentNames.at(i),
                                          m_Environments.at(i));
        }

        m_Renderer->setEnvironment(m_EnvironmentNames.at(0));

        m_AssetManager = assetManager;

        for (int row = 0; row < 7; ++row) {
            float metal = (float)row / 6.0f;
            for (int column = 0; column < 7; ++column) {
                float rough = (float)column / 6.0f;
                SYN::MaterialData data{};
                data.metallic = metal;
                data.roughness = rough;
                data.tint = glm::vec4(1.0f);
                data.alphaCutoff = 1.0f;
                data.name = std::format("DefaultMat{}{}", row, column);

                m_AssetManager->add<SYN::MaterialData>(data, data.name);
            }
        }

        createSphereScene();
        createCabinScene();

        m_SceneManager->switchTo(m_SphereScene);
    }

    void onUpdate(float dt) override {
        m_FrameHistory[m_FrameIdx] = dt * 1000.0f; // seconds -> milliseconds
        (++m_FrameIdx) %= 120;

        m_FrameAvg = 0.0f;
        for (float history : m_FrameHistory)
            m_FrameAvg += history;
        m_FrameAvg /= 120.0f;

        auto updateSceneCamera = [&](SYN::SceneHandle sceneHandle) {
            SYN::Scene *scene = m_SceneManager->getSceneMut(sceneHandle);
            scene->forEach<SYN::CameraComponent>(
                [&](SYN::Entity e, SYN::CameraComponent &camera) {
                    auto [w, h] = m_Window->getScreenSize();
                    camera.aspectRatio = (float)w / h;
                });
        };

        updateSceneCamera(m_SphereScene);
        updateSceneCamera(m_CabinScene);
    }

    void createCabinScene() {
        SYN::Scene *cabinScene = m_SceneManager->getSceneMut(m_CabinScene);

        SYN::Entity cameraEntity = cabinScene->createEntity("Camera");

        auto &camera = cameraEntity.addComponent<SYN::CameraComponent>();
        camera.isPrimary = true;

        cameraEntity.addComponent<SYN::FlyCameraComponent>();

        SYN::Entity cabinEntity = cabinScene->createEntity("Cabin");
        cabinEntity.addComponent<SYN::ModelComponent>(
            m_AssetManager->acquire(m_Cabin));

        {
            SYN::Entity walterEntity = cabinScene->createEntity("Walter");
            walterEntity.addComponent<SYN::ModelComponent>(
                m_AssetManager->acquire(m_Waltuh));
            auto &transform =
                walterEntity.getComponent<SYN::TransformComponent>();
            transform.position = glm::vec3(-10.0f, 0.0f, 0.0f);
        }

        {
            SYN::Entity dancerEntity = cabinScene->createEntity("Dancer");
            dancerEntity.addComponent<SYN::ModelComponent>(
                m_AssetManager->acquire(m_Dancer));
            auto &player =
                dancerEntity.addComponent<SYN::SkeletalAnimationComponent>(
                    m_AssetManager);
            player.player.setClip(m_DancerClips[0]);

            m_DancerPlayer = &player.player;

            auto &transform =
                dancerEntity.getComponent<SYN::TransformComponent>();
            transform.position = glm::vec3(-10.0f, 0.0f, 3.0f);
        }

        {
            SYN::Entity parasiteEntity = cabinScene->createEntity("Parasite");
            parasiteEntity.addComponent<SYN::ModelComponent>(
                m_AssetManager->acquire(m_Parasite));
            auto &player =
                parasiteEntity.addComponent<SYN::SkeletalAnimationComponent>(
                    m_AssetManager);
            player.player.setClip(m_ParasiteClips[0]);
            player.player.setTargetClip(m_ParasiteClips[1]);
            player.player.setLoop(true);
            player.player.play();

            m_ParasitePlayer = &player.player;

            auto &transform =
                parasiteEntity.getComponent<SYN::TransformComponent>();
            transform.position = glm::vec3(-8.0f, 0.0f, 1.5f);
        }
    }

    void createSphereScene() {

        SYN::Scene *sphereScene = m_SceneManager->getSceneMut(m_SphereScene);
        SYN::Entity cameraEntity = sphereScene->createEntity("Camera");

        cameraEntity.addComponent<SYN::CameraComponent>();
        cameraEntity.addComponent<SYN::FlyCameraComponent>();

        auto &cameraTransform =
            cameraEntity.getComponent<SYN::TransformComponent>();
        cameraTransform.position = glm::vec3(0.0f, 0.0f, 20.0f);
        auto &camera = cameraEntity.getComponent<SYN::CameraComponent>();
        camera.isPrimary = true;

        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 7; ++column) {
                glm::vec3 position(column * 2.5 - 7.5f, row * 2.5 - 7.5f, 0.0);

                SYN::UUID matUUID = m_AssetManager
                                        ->uuidFromKey(std::format(
                                            "DefaultMat{}{}", row, column))
                                        .value();

                SYN::Entity entity = sphereScene->createEntity(
                    std::format("Sphere{}{}", row, column));
                entity.addComponent<SYN::ModelComponent>(
                    m_AssetManager->acquire(m_Sphere));
                entity.addComponent<SYN::MaterialComponent>(
                    SYN::MaterialComponent{
                        {{m_AssetManager->acquire(matUUID), 0}}});
                SYN::TransformComponent &transform =
                    entity.getComponent<SYN::TransformComponent>();
                transform.position = position;
            }
        }
    }

    void onRender() override {
        if (m_DrawDebugLines) {
            m_DebugDraw->line(glm::vec3(0.0f), glm::vec3(10.0f),
                              glm::vec3(1.0, 0.0, 0.0));
            m_DebugDraw->line(glm::vec3(10.0f), glm::vec3(0.0, 0.0, 20.0f),
                              glm::vec3(0.0, 1.0, 0.0), 5.0f);
            m_DebugDraw->aabb(glm::vec3(0.0f), glm::vec3(-10.0f),
                              glm::vec3(1.0f, 1.0f, 0.0f), 1.0f, false);
        }
    }

    void onUIRender() override {
        if (ImGui::Begin("Renderer Config")) {
            ImGui::Checkbox("Test debug lines", &m_DrawDebugLines);

            const char *aa[] = {"None", "FXAA", "MSAA 2x", "MSAA 4x",
                                "MSAA 8x"};
            if (ImGui::Combo("Anti Aliasing", (int *)&m_AntiAliasMode, aa,
                             IM_ARRAYSIZE(aa))) {
                m_Renderer->setAntiAliasMode(m_AntiAliasMode);
            }

            const char *anisotropicFiltering[] = {"None", "2x", "4x", "8x",
                                                  "16x"};
            if (ImGui::Combo("Anisotropic Filtering", &m_AnisotropicMode,
                             anisotropicFiltering,
                             IM_ARRAYSIZE(anisotropicFiltering))) {
                switch (m_AnisotropicMode) {
                case 1:
                    m_Renderer->setAnisotropicFiltering(2.0f);
                    break;
                case 2:
                    m_Renderer->setAnisotropicFiltering(4.0f);
                    break;
                case 3:
                    m_Renderer->setAnisotropicFiltering(8.0f);
                    break;
                case 4:
                    m_Renderer->setAnisotropicFiltering(16.0f);
                    break;
                default:
                    m_Renderer->setAnisotropicFiltering(1.0f);
                }
            }

            if (ImGui::SliderFloat("Gamma", &m_Gamma, gl::MIN_GAMMA,
                                   gl::MAX_GAMMA)) {
                m_Renderer->setGamma(m_Gamma);
            }
            if (ImGui::SliderFloat("Exposure", &m_Exposure, 0.0f, 10.0f)) {
                m_Renderer->setExposure(m_Exposure);
            }
            if (ImGui::SliderFloat("Render Scale", &m_RenderScale, 0.01f,
                                   2.0f)) {
                m_Renderer->setRenderScale(m_RenderScale);
            }
            if (ImGui::Button("Reload shaders")) {
                m_Renderer->reloadInternalShaders(*m_Context);
            }

            std::vector<const char *> names;
            for (const std::string &name : m_EnvironmentNames)
                names.push_back(name.c_str());
            if (ImGui::Combo("Environment", &m_EnvironmentIdx, &names[0],
                             m_EnvironmentNames.size())) {
                m_Renderer->setEnvironment(
                    m_EnvironmentNames.at(m_EnvironmentIdx));
            }

            const char *renderMode[] = {"Cabin", "Sphere"};
            if (ImGui::Combo("Render mode", &m_RenderModeIdx, renderMode,
                             IM_ARRAYSIZE(renderMode))) {
                if (m_RenderModeIdx == 0) {
                    m_SceneManager->switchTo(m_CabinScene);
                } else {
                    m_SceneManager->switchTo(m_SphereScene);
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("Light control")) {
            if (ImGui::ColorEdit3("Color", &m_Light.color[0])) {
                m_Renderer->setDirectionalLight(m_Light);
            }
            if (ImGui::SliderFloat3("Direction", &m_Light.direction[0], -1.0f,
                                    1.0f)) {
                m_Renderer->setDirectionalLight(m_Light);
            }
            if (ImGui::SliderFloat("Intensity", &m_Light.intensity, 0.0f,
                                   10000.0f)) {
                m_Renderer->setDirectionalLight(m_Light);
            }
            if (ImGui::Checkbox("Cast shadow", &m_Light.castsShadows)) {
                m_Renderer->setDirectionalLight(m_Light);
            }
        }
        ImGui::End();

        if (ImGui::Begin("Material")) {
            if (ImGui::ColorEdit3("Color", &m_Color[0])) {
                for (int row = 0; row < 7; ++row) {
                    float metal = (float)row / 6.0f;
                    for (int column = 0; column < 7; ++column) {
                        float rough = (float)column / 6.0f;
                        SYN::MaterialData *data =
                            m_AssetManager->getMut<SYN::MaterialData>(
                                m_AssetManager
                                    ->uuidFromKey(std::format("DefaultMat{}{}",
                                                              row, column))
                                    .value());
                        data->tint = glm::vec4(m_Color, 1.0f);
                    }
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("Performance")) {
            ImGui::Text("%.3f ms (%.1f FPS)", m_FrameAvg, 1000.0f / m_FrameAvg);
            ImGui::PlotLines("Frame ms", m_FrameHistory, 120, m_FrameIdx,
                             nullptr, 0.0f, 33.3f, ImVec2(0, 80));
        }
        ImGui::End();

        if (ImGui::Begin("Animation Control")) {
            bool isPlaying = m_DancerPlayer->isPlaying();
            if (ImGui::Button(isPlaying ? "Pause" : "Play")) {
                if (m_DancerPlayer->isPlaying()) {
                    m_DancerPlayer->stop();
                } else {
                    m_DancerPlayer->play();
                }
            }
            if (ImGui::Checkbox("Toggle loop", &m_IsLooping)) {
                m_DancerPlayer->setLoop(m_IsLooping);
            }

            ImGui::Text("Blending (Parasite)");
            ImGui::Separator();

            if (ImGui::Button("Reset")) {
                m_ParasitePlayer->setLoop(true);
                m_ParasitePlayer->setClip(m_ParasiteClips[0]);
                m_ParasitePlayer->setTargetClip(m_ParasiteClips[1]);
                m_ParasitePlayer->play(0);
            }

            if (ImGui::SliderFloat("Blend weight", &m_Weight, 0.0f, 1.0f)) {
                m_ParasitePlayer->setBlendWeight(m_Weight);
            }

            ImGui::SliderFloat("Crossfade Duration", &m_CrossfadeDuration, 0.0f,
                               10.0f);

            if (ImGui::Button("Crossfade Idle to Run")) {
                m_ParasitePlayer->setClip(m_ParasiteClips[0]);
                m_ParasitePlayer->crossfadeTo(m_ParasiteClips[1],
                                              m_CrossfadeDuration);
            }

            ImGui::SliderFloat("Ease in", &m_EaseIn, 0.0f, 10.0f);
            ImGui::SliderFloat("Ease out", &m_EaseOut, 0.0f, 10.0f);
            if (ImGui::Button("Run to dance to idle")) {
                m_ParasitePlayer->setClip(m_ParasiteClips[1]);
                m_ParasitePlayer->playOneShot(
                    m_DancerClips[0], m_ParasiteClips[0], m_EaseIn, m_EaseOut);
            }
        }
        ImGui::End();

        if (ImGui::Begin("CSM Debug")) {
            const float tileSize = 256.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float availWidth = ImGui::GetContentRegionAvail().x;
            int perRow = (int)((availWidth + spacing) / (tileSize + spacing));
            if (perRow < 1)
                perRow = 1;

            for (size_t i = 0; i < m_CSMLayers.size(); ++i) {
                if (i % perRow != 0)
                    ImGui::SameLine();

                ImGui::BeginGroup();
                ImGui::Text("Cascade %zu", i);
                ImGui::Image((ImTextureID)(intptr_t)m_CSMLayers[i],
                             ImVec2(tileSize, tileSize), ImVec2(0, 1),
                             ImVec2(1, 0));
                ImGui::EndGroup();
            }

            if (ImGui::SliderFloat("CSM Distance", &m_CSMDistance, 0.001f,
                                   500.0f)) {
                m_Renderer->setCSMDistance(m_CSMDistance);
            }
        }
        ImGui::End();
    }

  private:
    gl::Context *m_Context;
    SYN::Window *m_Window;
    gl::Renderer *m_Renderer;
    SYN::DebugDraw *m_DebugDraw;

    SYN::SceneManager *m_SceneManager;
    SYN::AssetManager *m_AssetManager;

    SYN::SceneHandle m_CabinScene;
    SYN::SceneHandle m_SphereScene;

    float m_FrameHistory[120] = {};
    int m_FrameIdx = 0;

    SYN::UUID m_Waltuh;

    SYN::UUID m_Dancer;
    std::vector<SYN::UUID> m_DancerClips;
    SYN::AnimationPlayer *m_DancerPlayer;

    SYN::UUID m_Parasite;
    std::vector<SYN::UUID> m_ParasiteClips;
    SYN::AnimationPlayer *m_ParasitePlayer;
    float m_Weight = 0.0f;
    float m_CrossfadeDuration = 0.0f;

    float m_EaseIn = 0.0f;
    float m_EaseOut = 0.0f;

    SYN::UUID m_Cabin;
    SYN::UUID m_Sphere;
    std::array<gl::Environment, 4> m_Environments;
    std::array<std::string, 4> m_EnvironmentNames;

    float m_FrameAvg = 0.0f;

    float m_CSMDistance = 20.0f;
    std::vector<uint32_t> m_CSMLayers;

    int m_EnvironmentIdx = 0;
    float m_Gamma = 2.2f;
    float m_Exposure = 1.0f;
    float m_RenderScale = 0.7f;

    // Animation controls
    bool m_IsPlaying = false;
    bool m_IsLooping = false;

    bool m_DrawDebugLines = false;

    glm::vec3 m_Color;

    std::string m_RenderMode = "Sphere";
    int m_RenderModeIdx = 1;

    gl::AntiAliasMode m_AntiAliasMode = gl::AntiAliasMode::MSAA_4x;
    int m_AnisotropicMode = 3;

    gl::DirectionalLight m_Light;
};

class GLRenderTest : public SYN::Application {
  public:
    GLRenderTest() { m_Graphics = std::make_unique<GraphicsScene>(); }

    void init() override {
        m_EngineContext.windowConfig = SYN::WindowConfig{
            "GLRenderer", WINDOW_WIDTH, WINDOW_HEIGHT, SYN::OpenGLConfig{4, 5}};

        SYN::Application::init();
        m_Graphics->init(&m_EngineContext);
        m_Layers.push_front(m_Graphics.get());
    }
    ~GLRenderTest() override {}

  private:
    std::unique_ptr<GraphicsScene> m_Graphics;
};

int main(void) {
    std::unique_ptr<SYN::Application> app = std::make_unique<GLRenderTest>();

    app->init();
    app->run();
    app->shutdown();
}
