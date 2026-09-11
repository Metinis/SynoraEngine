#pragma once

namespace SYN {
struct RenderTargetData {
    enum class Format {
        R8,
        R32UI,
        RGBA8,
        RGBA8_SRGB,
        RGBA16F,
        RGBA32F,
        RG16F,
        RG32F,
        DEPTH24,
        DEPTH24_STENCIL8,
        DEPTH32
    };

    std::array<Format, 8> color;
    uint32_t colorCount = 0;

    std::optional<Format> depth = std::nullopt;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sampleCount = 1;
};
} // namespace SYN
