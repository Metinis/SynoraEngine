set(CMAKE_CXX_STANDARD 23)

set (APPLE_COMPILE_DEFINITIONS)

if(APPLE)
    # This is to enable VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME to make moltenvk work
    set(APPLE_COMPILE_DEFINITIONS VK_ENABLE_BETA_EXTENSIONS)
endif()

file(GLOB_RECURSE ENGINE_SOURCES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(SynoraEngine STATIC ${ENGINE_SOURCES})

add_custom_command(TARGET SynoraEngine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/../shaders
          ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/shaders/OpenGL
          COMMENT "Copying SynoraEngine shaders to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/shaders/OpenGL"
)

if (SYNORA_SHADER_DEBUG)
target_compile_definitions(SynoraEngine PRIVATE SHADER_DEBUG_PATH="${CMAKE_SOURCE_DIR}/SynoraEngine/shaders/")
endif()

target_precompile_headers(
  SynoraEngine
  PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../include/SynoraEngine/synpch.h>"
)
target_include_directories(
  SynoraEngine
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/tinyfiledialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/stb
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/imgui/backends
)
target_link_libraries(
  SynoraEngine
  PUBLIC
  glm::glm
  imgui
  imguizmo
  EnTT::EnTT
  spdlog
  PRIVATE
  glfw
  miniaudio
  Jolt
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
  assimp
  tinyfiledialogs
  glad
  TracyClient
)

# This is to make the projection matrices in glm work with vulkan
target_compile_definitions(SynoraEngine PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_LEFT_HANDED GLFW_INCLUDE_VULKAN ${APPLE_COMPILE_DEFINITIONS})
