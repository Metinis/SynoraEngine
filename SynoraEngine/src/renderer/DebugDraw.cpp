#include <SynoraEngine/renderer/DebugDraw.h>

namespace SYN {
void DebugDraw::line(glm::vec3 a, glm::vec3 b, glm::vec3 color, float thickness,
                     bool depthTest) {
    if (a == b)
        return;

    thickness = glm::max(thickness, 0.0f);

    if (thickness == 0.0f)
        return;

    DebugDraw::Line newLine;
    newLine.pointA = a;
    newLine.pointB = b;
    newLine.thickness = thickness;
    newLine.color = color;

    if (depthTest) {
        m_LinesDepth.push_back(newLine);
    } else {
        m_LinesNoDepth.push_back(newLine);
    }
}
void DebugDraw::aabb(glm::vec3 min, glm::vec3 max, glm::vec3 color,
                     float thickness, bool depthTest) {
    glm::vec3 extents = max - min;

    glm::vec3 a = min;
    glm::vec3 b = min + glm::vec3(extents.x, 0.0f, 0.0);
    glm::vec3 c = min + glm::vec3(0.0f, 0.0f, extents.z);
    glm::vec3 d = min + glm::vec3(extents.x, 0.0f, extents.z);

    glm::vec3 e = min + glm::vec3(0.0f, extents.y, 0.0);
    glm::vec3 f = min + glm::vec3(extents.x, extents.y, 0.0f);
    glm::vec3 g = min + glm::vec3(0.0f, extents.y, extents.z);
    glm::vec3 h = max;

    line(a, b, color, thickness, depthTest);
    line(a, c, color, thickness, depthTest);
    line(b, d, color, thickness, depthTest);
    line(c, d, color, thickness, depthTest);

    line(e, f, color, thickness, depthTest);
    line(e, g, color, thickness, depthTest);
    line(f, h, color, thickness, depthTest);
    line(g, h, color, thickness, depthTest);
}
} // namespace SYN
