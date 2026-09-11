#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <SynoraEngine/core/Application.h>
#include <SynoraEngine/core/Input.h>
#include <SynoraEngine/core/InputContext.h>
#include <SynoraEngine/core/Window.h>
#include <SynoraEngine/gfx/gl/GL.h>
#include <SynoraEngine/project/AssetManager.h>
#include <SynoraEngine/project/Project.h>
#include <SynoraEngine/renderer/DebugDraw.h>
#include <SynoraEngine/renderer/backends/IRenderViewBackend.h>
#include <SynoraEngine/scene/SceneManager.h>

#include "scene/systems/AnimationPlayerSystem.h"
#include "scene/systems/CameraSystem.h"
#include "scene/systems/ComputeBoundsSystem.h"
#include "scene/systems/RenderViewBuilder.h"

#include <tracy/Tracy.hpp>

using namespace SYN;

Application *Application::s_Instance = nullptr;
Application::Application() {
    s_Instance = this;

    m_EngineContext.window = std::make_unique<Window>();
    m_EngineContext.inputManager = std::make_unique<Input>();
    m_EngineContext.sceneManager = std::make_unique<SceneManager>();
    m_EngineContext.cameraSystem = std::make_unique<CameraSystem>();
    m_EngineContext.animationPlayerSystem =
        std::make_unique<AnimationPlayerSystem>();
    m_EngineContext.computeBoundsSystem =
        std::make_unique<ComputeBoundsSystem>();
    m_EngineContext.debugDraw = std::make_unique<DebugDraw>();
    m_EngineContext.renderViewBuilder = std::make_unique<RenderViewBuilder>();

    ProjectConfig projectConfig{
        .resourceRoot = "", // todo add root
        .assetManager = std::make_unique<AssetManager>(),
    };
    m_EngineContext.projectConfig = std::move(projectConfig);
    m_EngineContext.windowConfig = WindowConfig{"Synora Engine", 1280, 720};
}

void Application::init() {
    spdlog::set_level(spdlog::level::debug);
    // create window etc
    m_IsRunning = true;

    m_EngineContext.window->init(m_EngineContext.windowConfig);
    m_EngineContext.projectConfig.assetManager->init();
    m_EngineContext.inputManager->init(&m_EngineContext);

    if (m_EngineContext.windowConfig.openGLConfig.has_value()) {
        m_EngineContext.glContext = std::make_unique<gfx::gl::Context>(
            gfx::gl::Context::createContext(
                {m_EngineContext.window->getHandle(),
                 m_EngineContext.windowConfig.width,
                 m_EngineContext.windowConfig.height})
                .value());
        m_EngineContext.renderer =
            std::make_unique<gfx::gl::Renderer>(gfx::gl::RendererConfig{});
        m_EngineContext.renderer->init(&m_EngineContext);
    } else {
        // For other backends non-OpenGL this is where you can
        // set the renderer to something else.
    }

    m_EngineContext.renderViewBuilder->init(&m_EngineContext);
    m_EngineContext.cameraSystem->init(&m_EngineContext);
    m_EngineContext.animationPlayerSystem->init(&m_EngineContext);
    m_EngineContext.computeBoundsSystem->init(&m_EngineContext);

    m_Layers.push_back(m_EngineContext.sceneManager.get());
    m_Layers.push_back(m_EngineContext.cameraSystem.get());
    m_Layers.push_back(m_EngineContext.animationPlayerSystem.get());
    m_Layers.push_back(m_EngineContext.computeBoundsSystem.get());
    m_Layers.push_back(m_EngineContext.renderViewBuilder.get());
}

void Application::run() {
    bool usingGL = m_EngineContext.windowConfig.openGLConfig.has_value();

    // while running and window open
    double lastTime{glfwGetTime()};
    while (m_IsRunning && m_EngineContext.window->isRunning()) {
        glfwPollEvents();
        m_EngineContext.inputManager->processInputQueue();
        m_EngineContext.sceneManager->handleSwitch();

        m_EngineContext.renderer->beforeDraw();

        double currentTime{glfwGetTime()};
        float dt{static_cast<float>(currentTime - lastTime)};
        lastTime = currentTime;
        for (auto &l : m_Layers) {
            l->onUpdate(dt);
        }

        if (!usingGL) {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            for (auto &l : m_Layers) {
                l->onUIRender();
            }
            ImGui::Render();
            ImGui::EndFrame();

            for (auto &l : m_Layers) {
                l->onRender();
            }
        } else {
            for (auto &l : m_Layers) {
                l->onRender();
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            for (auto &l : m_Layers) {
                l->onUIRender();
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            gfx::gl::Context *context = m_EngineContext.glContext.get();
            context->present();
        }

        m_EngineContext.projectConfig.assetManager->resolvePendingDeletions();
        m_EngineContext.renderer->afterDraw();

        if (usingGL) {
            m_EngineContext.glContext->flushDeferredDeletes();
        }

        FrameMark;
    }
}

void Application::shutdown() {
    if (!m_EngineContext.windowConfig.openGLConfig.has_value()) {
        m_EngineContext.renderer->shutdown();
    }
}

std::unique_ptr<Input> &Application::GetInput() {
    return m_EngineContext.inputManager;
}

Application::~Application() = default;
