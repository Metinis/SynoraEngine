#pragma once

#include <glm/glm.hpp>

namespace SYN {
class DebugDraw {
  public:
    void line(glm::vec3 a, glm::vec3 b, glm::vec3 color, float thickness = 1.0f,
              bool depthTest = true);
    void aabb(glm::vec3 min, glm::vec3 max, glm::vec3 color,
              float thickness = 1.0f, bool depthTest = true);

  private:
    struct Line {
        glm::vec3 pointA;
        glm::vec3 pointB;
        float thickness;
        glm::vec3 color;
    };

  private:
    std::vector<Line> m_LinesDepth;
    std::vector<Line> m_LinesNoDepth;
};
} // namespace SYN
