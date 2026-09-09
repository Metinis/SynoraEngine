#pragma once

#include <glm/glm.hpp>

namespace SYN {
class DebugDraw {
  public:
    struct Line {
        glm::vec3 pointA;
        glm::vec3 pointB;
        glm::vec3 color;
        float thickness;
    };

  public:
    void line(glm::vec3 a, glm::vec3 b, glm::vec3 color, float thickness = 1.0f,
              bool depthTest = true);
    void aabb(glm::vec3 min, glm::vec3 max, glm::vec3 color,
              float thickness = 1.0f, bool depthTest = true);
    void cameraFrustum(glm::mat4 view, glm::mat4 projection, glm::vec3 color,
                       float thickness = 1.0f, bool depthTest = true);

    // Call at the end of the frame to clear all previously submitted draw data.
    void clear();

  private:
    friend class RenderViewBuilder;

    std::vector<Line> m_LinesDepth;
    std::vector<Line> m_LinesNoDepth;
};
} // namespace SYN
