#include <SynoraEngine/renderer/DebugDraw.h>

#include <SynoraEngine/core/math/Frustum.h>
#include <SynoraEngine/renderer/backends/IRenderViewBackend.h>

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

    line(a, e, color, thickness, depthTest);
    line(b, f, color, thickness, depthTest);
    line(c, g, color, thickness, depthTest);
    line(d, h, color, thickness, depthTest);
}

void DebugDraw::cameraFrustum(glm::mat4 view, glm::mat4 projection,
                              glm::vec3 color, float thickness,
                              bool depthTest) {
    std::vector<glm::vec4> corners =
        Frustum::getCornersWorldSpace(view, projection);

    line(corners[0], corners[1], color, thickness, depthTest);
    line(corners[0], corners[4], color, thickness, depthTest);
    line(corners[0], corners[2], color, thickness, depthTest);

    line(corners[5], corners[4], color, thickness, depthTest);
    line(corners[5], corners[1], color, thickness, depthTest);
    line(corners[5], corners[7], color, thickness, depthTest);

    line(corners[2], corners[3], color, thickness, depthTest);
    line(corners[2], corners[6], color, thickness, depthTest);

    line(corners[7], corners[3], color, thickness, depthTest);
    line(corners[7], corners[6], color, thickness, depthTest);

    line(corners[4], corners[6], color, thickness, depthTest);
    line(corners[1], corners[3], color, thickness, depthTest);
}

void DebugDraw::flush(IRenderViewBackend *renderer) {
    if (!m_LinesDepth.empty()) {
        renderer->submitLineList(m_LinesDepth, true);
    }

    if (!m_LinesNoDepth.empty()) {
        renderer->submitLineList(m_LinesNoDepth, false);
    }

    m_LinesDepth.clear();
    m_LinesNoDepth.clear();
}
} // namespace SYN
