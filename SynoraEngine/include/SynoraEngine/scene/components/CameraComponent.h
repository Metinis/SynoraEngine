#pragma once

namespace SYN {
struct CameraComponent {
    enum class RenderMode { Continuous, OnDemand };

    float fovDegrees = 90.f;
    float aspectRatio = 16.f / 9.f;
    float nearPlane = 0.1;
    float farPlane = 100.f;
    bool isPrimary;

    uint32_t depth = 0;
    RenderMode renderMode = RenderMode::Continuous;
    bool dirty = false;
};

// Attach with CameraComponent
struct FlyCameraComponent {
    bool tag;
};
} // namespace SYN
