
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog)

add_subdirectory(miniaudio)

add_subdirectory(entt)

set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)

add_subdirectory(JoltPhysics/Build)

add_subdirectory(glm)
add_subdirectory(VulkanMemoryAllocator)

add_library(glad STATIC glad/src/glad.c)
target_include_directories(
  glad 
  PUBLIC 
  glad/include
)

set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(assimp)

add_library(imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/imgui_demo.cpp
        imgui/backends/imgui_impl_glfw.cpp
        imgui/backends/imgui_impl_vulkan.cpp
        imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PRIVATE ${Vulkan_INCLUDE_DIR})

add_library(tinyfiledialogs STATIC
        tinyfiledialogs/tinyfiledialogs.c
)

target_include_directories(imgui
        PUBLIC
        imgui
        imgui/backends
)
target_link_libraries(imgui
        PRIVATE
        glfw
)
target_link_libraries(tinyfiledialogs
        PRIVATE
        glfw
)

set(IMGUIZMO_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(ImGuizmo)
target_link_libraries(imguizmo PRIVATE imgui)

option(SYNORA_ENABLE_PROFILING "Enable Tracy profiler instrumentation" ON)
set(TRACY_ENABLE ${SYNORA_ENABLE_PROFILING} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
add_subdirectory(tracy)
