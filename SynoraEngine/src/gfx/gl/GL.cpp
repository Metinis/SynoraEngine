#include <SynoraEngine/project/AssetManager.h>
#include <SynoraEngine/project/assets/MaterialData.h>
#include <SynoraEngine/project/assets/ModelData.h>
#include <SynoraEngine/project/assets/TextureData.h>

#include <SynoraEngine/core/Application.h>

#include <SynoraEngine/gfx/gl/GL.h>

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <SynoraEngine/core/Window.h>

#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

#include <fstream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>

#include <glm/gtx/matrix_decompose.hpp>

struct {
    using UniformLocations = std::unordered_map<std::string_view, int>;
    std::unordered_map<uint32_t, UniformLocations> shaderUniformCache;
    float maxAnisotropy;
} Globals;

static bool hasGLExtension(std::string_view name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char *ext =
            reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && name == ext)
            return true;
    }
    return false;
}

SYN::gfx::gl::Handle<SYN::gfx::gl::Texture>
createDefaultColoredTexture(SYN::gfx::gl::Context &context,
                            std::array<uint8_t, 4> color) {
    SYN::gfx::gl::TextureDesc desc{
        .width = 1,
        .height = 1,
        .format = SYN::gfx::gl::TextureFormat::SRGBA,
    };
    return context.createTexture(desc, &color[0]).value();
}

SYN::gfx::gl::Handle<SYN::gfx::gl::Shader>
createDefaultShader(SYN::gfx::gl::Context &context) {
    std::string defaultVertexSource = R"(
        #version 450 core
        void main() {
            gl_Position = vec4(vec3(0.0f), 1.0f);
        }
    )";

    std::string defaultFragmentSource = R"(
        #version 450 core
        out vec4 fragColor;
        void main() {
            fragColor = vec4(1.0f);    
        }
    )";

    return context.createShader(defaultVertexSource, defaultFragmentSource)
        .value();
}

SYN::gfx::gl::Mesh
SYN::gfx::gl::Renderer::createMesh(SYN::gfx::gl::Context &context,
                                   const MeshData &meshData) {
    SYN::gfx::gl::Mesh mesh;

    mesh.vbo = context
                   .createBuffer({SYN::gfx::gl::BufferType::Vertex,
                                  SYN::gfx::gl::MemoryUsage::CpuToGPU,
                                  uint32_t(sizeof(SYN::gfx::gl::Vertex) *
                                           meshData.vertices.size())},
                                 &meshData.vertices[0])
                   .value();

    mesh.ebo = context
                   .createBuffer(
                       {SYN::gfx::gl::BufferType::Index,
                        SYN::gfx::gl::MemoryUsage::CpuToGPU,
                        uint32_t(sizeof(uint32_t) * meshData.indices.size())},
                       &meshData.indices[0])
                   .value();

    mesh.vao = context
                   .createVertexArray(
                       {mesh.vbo,
                        sizeof(SYN::gfx::gl::Vertex),
                        mesh.ebo,
                        {{
                            {0, SYN::gfx::gl::VertexFormat::Float3,
                             offsetof(SYN::gfx::gl::Vertex, position)},

                            {1, SYN::gfx::gl::VertexFormat::Float3,
                             offsetof(SYN::gfx::gl::Vertex, normal)},

                            {2, SYN::gfx::gl::VertexFormat::Float2,
                             offsetof(SYN::gfx::gl::Vertex, uv)},

                            {3, SYN::gfx::gl::VertexFormat::Float4,
                             offsetof(SYN::gfx::gl::Vertex, tangent)},

                            {4, SYN::gfx::gl::VertexFormat::Int4,
                             offsetof(SYN::gfx::gl::Vertex, boneIndices)},

                            {5, SYN::gfx::gl::VertexFormat::Float4,
                             offsetof(SYN::gfx::gl::Vertex, boneWeights)},
                        }},
                        6})
                   .value();

    mesh.indexCount = meshData.indices.size();
    mesh.localTransform = meshData.localTransform;
    mesh.hasSkin = meshData.hasSkin;

    const MaterialData *material =
        m_AssetManager->get<MaterialData>(meshData.material.uuid());
    mesh.material = loadMaterial(context, *material);

    return mesh;
}

uint32_t getShaderFeatures(const SYN::gfx::gl::Mesh &mesh,
                           const SYN::gfx::gl::Material &material) {
    uint32_t featureFlag = 0;

    featureFlag |=
        mesh.hasSkin ? (uint32_t)SYN::gfx::gl::ShaderFeature::Skinned : 0;

    featureFlag |=
        material.metallicRoughnessMap.has_value()
            ? (uint32_t)SYN::gfx::gl::ShaderFeature::MetallicRoughness
            : 0;

    featureFlag |= material.normalMap.has_value()
                       ? (uint32_t)SYN::gfx::gl::ShaderFeature::Normal
                       : 0;

    featureFlag |= material.alphaCutoff < 1.0f
                       ? (uint32_t)SYN::gfx::gl::ShaderFeature::AlphaTest
                       : 0;

    return featureFlag;
}

void setSamplerParameters(uint32_t samplerId,
                          const SYN::gfx::gl::SamplerDesc &desc) {
    using namespace SYN::gfx::gl;

    auto getFilterFormat = [](SampleFilter filter) {
        switch (filter) {
        case SampleFilter::Nearest:
            return GL_NEAREST;
        case SampleFilter::Linear:
            return GL_LINEAR;
        case SampleFilter::Nearest_Mipmap_Nearest:
            return GL_NEAREST_MIPMAP_NEAREST;
        default:
            return GL_LINEAR_MIPMAP_LINEAR;
        }
    };

    auto getWrapFormat = [](WrapMode wrap) {
        switch (wrap) {
        case WrapMode::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case WrapMode::Repeat:
            return GL_REPEAT;
        case WrapMode::ClampToBorder:
            return GL_CLAMP_TO_BORDER;
        default:
            return GL_MIRRORED_REPEAT;
        }
    };

    glSamplerParameteri(samplerId, GL_TEXTURE_MIN_FILTER,
                        getFilterFormat(desc.minFilter));
    glSamplerParameteri(samplerId, GL_TEXTURE_MAG_FILTER,
                        getFilterFormat(desc.magFilter));
    glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_S,
                        getWrapFormat(desc.wrapU));
    glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_T,
                        getWrapFormat(desc.wrapV));
    glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_R,
                        getWrapFormat(desc.wrapW));
    glSamplerParameterfv(samplerId, GL_TEXTURE_BORDER_COLOR,
                         &desc.borderColor[0]);

    if (desc.compareMode) {
        glSamplerParameteri(samplerId, GL_TEXTURE_COMPARE_MODE,
                            GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(samplerId, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    float aniso =
        glm::clamp(desc.anisotropicLevel, 1.0f, Globals.maxAnisotropy);
    glSamplerParameterf(samplerId, GL_TEXTURE_MAX_ANISOTROPY, aniso);
}

GLenum getPrimitiveType(SYN::gfx::gl::PrimitiveTopology type) {
    using namespace SYN::gfx::gl;
    GLenum drawMode = GL_TRIANGLES;
    switch (type) {
    case PrimitiveTopology::Triangles:
        drawMode = GL_TRIANGLES;
        break;
    case PrimitiveTopology::Lines:
        drawMode = GL_LINES;
        break;
    case PrimitiveTopology::Points:
        drawMode = GL_POINTS;
        break;
    case PrimitiveTopology::TriangleStrip:
        drawMode = GL_TRIANGLE_STRIP;
        break;
    }
    return drawMode;
}

GLenum getIndexType(SYN::gfx::gl::IndexType type) {
    using namespace SYN::gfx::gl;
    GLenum indexType = GL_UNSIGNED_SHORT;
    switch (type) {
    case IndexType::Unsigned16:
        indexType = GL_UNSIGNED_SHORT;
        break;
    case IndexType::Unsigned32:
        indexType = GL_UNSIGNED_INT;
        break;
    }
    return indexType;
}

bool validateShaderCompileStatus(uint32_t shader, bool isVertex) {
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        constexpr size_t LOG_BUFFER_SIZE = 512;
        char buf[LOG_BUFFER_SIZE];
        glGetShaderInfoLog(shader, LOG_BUFFER_SIZE, nullptr, buf);
        std::string_view shaderType = isVertex ? "VERTEX" : "FRAGMENT";
        spdlog::error("{} SHADER ERROR:\n{}\n", shaderType, buf);
        return false;
    }
    return true;
}

bool validateProgramLinkStatus(uint32_t program) {
    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        constexpr size_t LOG_BUFFER_SIZE = 512;
        char buf[LOG_BUFFER_SIZE];
        glGetProgramInfoLog(program, LOG_BUFFER_SIZE, nullptr, buf);
        spdlog::error("PROGRAM LINK ERROR:\n{}\n", buf);
        return false;
    }
    return true;
}

// Pass
// Use to define render passes per frame by setting OpenGL state with
// PipelineState and issuing draw commands/binding resources. If creating
// multiple passes within the same function, separate passes with scopes because
// destructor resets prior OpenGL state. Create a pass with
// Context::beginPass(PassDesc{}).

// Adds shader uniform to cache if it doesn't exist. Retrieves uniform
// location which may be -1 if it doesn't exist in the shader.
int SYN::gfx::gl::Pass::getShaderUniformLocation(std::string_view uniform) {
    assert(m_CurrentShader.has_value() &&
           "Shader not set. Are you binding a pipeline, and does it have its "
           "shader set?");

    Shader shader = m_CurrentShader.value();

    if (Globals.shaderUniformCache[shader.program].find(uniform) ==
        Globals.shaderUniformCache[shader.program].cend()) {
        int uniformLoc = glGetUniformLocation(shader.program, uniform.data());
        if (uniformLoc == -1)
            return -1;
        Globals.shaderUniformCache[shader.program][uniform] = uniformLoc;
    }

    return Globals.shaderUniformCache[shader.program].at(uniform);
}

GLenum getInternalTextureFormat(SYN::gfx::gl::TextureFormat format) {
    switch (format) {
    case SYN::gfx::gl::TextureFormat::RGBA8:
        return GL_RGBA8;
    case SYN::gfx::gl::TextureFormat::RGB8:
        return GL_RGB8;
    case SYN::gfx::gl::TextureFormat::RG8:
    case SYN::gfx::gl::TextureFormat::R8:
        return GL_R8;
    case SYN::gfx::gl::TextureFormat::Depth24:
        return GL_DEPTH_COMPONENT24;
    case SYN::gfx::gl::TextureFormat::Depth16:
        return GL_DEPTH_COMPONENT16;
    case SYN::gfx::gl::TextureFormat::Depth24Stencil8:
        return GL_DEPTH24_STENCIL8;
    case SYN::gfx::gl::TextureFormat::SRGB:
        return GL_SRGB8;
    case SYN::gfx::gl::TextureFormat::SRGBA:
        return GL_SRGB8_ALPHA8;
    case SYN::gfx::gl::TextureFormat::RG16F:
        return GL_RG16F;
    case SYN::gfx::gl::TextureFormat::RG32F:
        return GL_RG32F;
    case SYN::gfx::gl::TextureFormat::RGB16F:
        return GL_RGB16F;
    case SYN::gfx::gl::TextureFormat::RGBA16F:
        return GL_RGBA16F;
    case SYN::gfx::gl::TextureFormat::RGB32F:
        return GL_RGB32F;
    case SYN::gfx::gl::TextureFormat::RGBA32F:
        return GL_RGBA32F;
    }
}

GLenum getTextureFormat(SYN::gfx::gl::TextureFormat format) {
    switch (format) {
    case SYN::gfx::gl::TextureFormat::RGBA8:
        return GL_RGBA;
    case SYN::gfx::gl::TextureFormat::RGB8:
        return GL_RGB;
    case SYN::gfx::gl::TextureFormat::RG8:
        return GL_RG;
    case SYN::gfx::gl::TextureFormat::R8:
        return GL_RED;
    case SYN::gfx::gl::TextureFormat::Depth16:
        return GL_DEPTH_COMPONENT;
    case SYN::gfx::gl::TextureFormat::Depth24:
        return GL_DEPTH_COMPONENT;
    case SYN::gfx::gl::TextureFormat::Depth24Stencil8:
        return GL_DEPTH_STENCIL;
    case SYN::gfx::gl::TextureFormat::SRGB:
        return GL_RGB;
    case SYN::gfx::gl::TextureFormat::SRGBA:
        return GL_RGBA;
    case SYN::gfx::gl::TextureFormat::RG16F:
        return GL_RG;
    case SYN::gfx::gl::TextureFormat::RG32F:
        return GL_RG;
    case SYN::gfx::gl::TextureFormat::RGB16F:
        return GL_RGB;
    case SYN::gfx::gl::TextureFormat::RGBA16F:
        return GL_RGBA;
    case SYN::gfx::gl::TextureFormat::RGB32F:
        return GL_RGB;
    case SYN::gfx::gl::TextureFormat::RGBA32F:
        return GL_RGBA;
    }
}

GLenum getTextureDataTypeFromFormat(SYN::gfx::gl::TextureFormat format) {
    GLenum dataType = GL_UNSIGNED_BYTE;

    switch (format) {
    case SYN::gfx::gl::TextureFormat::Depth24:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::Depth16:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::Depth24Stencil8:
        dataType = GL_UNSIGNED_INT_24_8;
        break;
    case SYN::gfx::gl::TextureFormat::RGB16F:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::RGB32F:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::RGBA16F:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::RGBA32F:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::RG16F:
        dataType = GL_FLOAT;
        break;
    case SYN::gfx::gl::TextureFormat::RG32F:
        dataType = GL_FLOAT;
        break;
    default:
        break;
    }

    return dataType;
}

SYN::gfx::gl::Pass::Pass(Context *context, const PassDesc &desc) {
    assert(context != nullptr && "OpenGL pass context cannot be NULL!");

    m_ContextPtr = context;

    if (desc.framebufferHandle.has_value()) {
        std::optional<Framebuffer> framebuffer =
            context->getFramebuffer(desc.framebufferHandle.value());

        assert(framebuffer.has_value() &&
               "Framebuffer in render pass doesn't exist!");

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.value().id);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLbitfield clearMask = 0;

    if (desc.clearColor.has_value()) {
        clearMask |= GL_COLOR_BUFFER_BIT;
    }

    if (desc.clearDepth) {
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }

    if (desc.enableDepthTest) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    if (desc.enableStencilTest) {
        glEnable(GL_STENCIL_TEST);
        clearMask |= GL_STENCIL_BUFFER_BIT;
    } else
        glDisable(GL_STENCIL_TEST);

    if (desc.viewportOverride.has_value()) {
        Viewport viewport = desc.viewportOverride.value();
        glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    }
    if (desc.scissorOverride.has_value()) {
        ScissorRect scissor = desc.scissorOverride.value();
        glScissor(scissor.x, scissor.y, scissor.width, scissor.height);
    }

    if (clearMask != 0) {
        if (desc.clearColor.has_value()) {
            glm::vec4 clearColor = desc.clearColor.value();
            glClearColor(clearColor.r, clearColor.g, clearColor.b,
                         clearColor.a);
        }
        if (clearMask & GL_DEPTH_BUFFER_BIT) {
            glDepthMask(GL_TRUE);
        }
        glClear(clearMask);
    }
}

SYN::gfx::gl::Pass::~Pass() {
    glUseProgram(0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (uint32_t slot : m_TextureSlotsBound) {
        glBindTextureUnit(slot, 0);
        glBindSampler(slot, 0);
    }
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, float v0) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform1f(loc, v0);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, float v0,
                                     float v1) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform2f(loc, v0, v1);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, float v0, float v1,
                                     float v2) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform3f(loc, v0, v1, v2);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, float v0, float v1,
                                     float v2, float v3) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform4f(loc, v0, v1, v2, v3);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, int32_t v0) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform1i(loc, v0);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, int32_t v0,
                                     int32_t v1) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform2i(loc, v0, v1);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, int32_t v0,
                                     int32_t v1, int32_t v2) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform3i(loc, v0, v1, v2);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, int32_t v0,
                                     int32_t v1, int32_t v2, int32_t v3) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform4i(loc, v0, v1, v2, v3);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, uint32_t v0) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform1ui(loc, v0);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, uint32_t v0,
                                     uint32_t v1) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform2ui(loc, v0, v1);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, uint32_t v0,
                                     uint32_t v1, uint32_t v2) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform3ui(loc, v0, v1, v2);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name, uint32_t v0,
                                     uint32_t v1, uint32_t v2, uint32_t v3) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniform4ui(loc, v0, v1, v2, v3);
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name,
                                     const glm::mat4 &v) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(v));
}

void SYN::gfx::gl::Pass::bindUniform(std::string_view name,
                                     const std::vector<glm::mat4> &v,
                                     std::optional<uint32_t> offset,
                                     std::optional<uint32_t> size) {
    int loc = getShaderUniformLocation(name);
    if (loc == -1) {
        spdlog::warn("Shader uniform: {} does not exist!", name);
        return;
    }
    glUniformMatrix4fv(loc, size.value_or(v.size()), GL_FALSE,
                       glm::value_ptr(v[offset.value_or(0)]));
}

void SYN::gfx::gl::Pass::bindTexture(uint32_t binding,
                                     Handle<Texture> textureHandle,
                                     Handle<Sampler> samplerHandle) {
    std::optional<Texture> textureOpt = m_ContextPtr->getTexture(textureHandle);
    std::optional<Sampler> samplerOpt = m_ContextPtr->getSampler(samplerHandle);

    assert(textureOpt.has_value() && "Texture handle is invalid");
    assert(samplerOpt.has_value() && "Sampler handle is invalid");

    Texture texture = textureOpt.value();
    Sampler sampler = samplerOpt.value();

    glBindTextureUnit(binding, texture.id);
    glBindSampler(binding, sampler.id);

    m_TextureSlotsBound.insert(binding);
}

void SYN::gfx::gl::Pass::bindUniformBuffer(uint32_t binding,
                                           Handle<Buffer> bufferHandle) {
    std::optional<Buffer> buffer = m_ContextPtr->getBuffer(bufferHandle);
    if (!buffer.has_value())
        return;
    if (buffer.value().type != BufferType::Uniform)
        return;
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer.value().id);
}

void SYN::gfx::gl::Pass::usePipeline(const PipelineState &pipelineState) {
    std::optional<Shader> currentShaderOpt =
        m_ContextPtr->getShader(pipelineState.shader);
    assert(currentShaderOpt.has_value() &&
           "Shader handle is invalid. Cannot set pipeline.");
    Shader shader = currentShaderOpt.value();

    m_CurrentShader = shader;

    switch (pipelineState.cullMode) {
    case CullMode::None:
        glDisable(GL_CULL_FACE);
        break;
    case CullMode::Front:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        break;
    case CullMode::Back:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        break;
    }

    if (pipelineState.frontFaceCcw)
        glFrontFace(GL_CCW);
    else
        glFrontFace(GL_CW);

    switch (pipelineState.polygonMode) {
    case PolygonMode::Fill:
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        break;
    case PolygonMode::Line:
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        break;
    case PolygonMode::Point:
        glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        break;
    }

    glDepthMask(pipelineState.depth.writeEnabled);
    switch (pipelineState.depth.test) {
    case DepthFunc::Less:
        glDepthFunc(GL_LESS);
        break;
    case DepthFunc::LessEqual:
        glDepthFunc(GL_LEQUAL);
        break;
    case DepthFunc::Greater:
        glDepthFunc(GL_GREATER);
        break;
    case DepthFunc::GreaterEqual:
        glDepthFunc(GL_GEQUAL);
        break;
    case DepthFunc::NotEqual:
        glDepthFunc(GL_NOTEQUAL);
        break;
    case DepthFunc::Equal:
        glDepthFunc(GL_EQUAL);
        break;
    case DepthFunc::Always:
        glDepthFunc(GL_ALWAYS);
        break;
    case DepthFunc::Never:
        glDepthFunc(GL_NEVER);
        break;
    }

    glColorMask(pipelineState.color.r, pipelineState.color.g,
                pipelineState.color.b, pipelineState.color.a);

    m_CurrentDrawTopology = pipelineState.topology;

    glUseProgram(shader.program);
}

void SYN::gfx::gl::Pass::bindVertexArray(
    Handle<VertexArray> vertexArrayHandle) {
    std::optional<VertexArray> currentVaoOpt =
        m_ContextPtr->getVertexArray(vertexArrayHandle);
    assert(currentVaoOpt.has_value() && "Vertex Array handle is invalid.");
    VertexArray vertexArray = currentVaoOpt.value();
    glBindVertexArray(vertexArray.id);
    if (vertexArray.indexType.has_value()) {
        m_CurrentIndexType = vertexArray.indexType.value();
    } else {
        m_CurrentIndexType = IndexType::Unsigned32;
    }
}

void SYN::gfx::gl::Pass::draw(uint32_t vertexCount, uint32_t firstVertex) {
    GLenum drawMode = getPrimitiveType(m_CurrentDrawTopology);
    glDrawArrays(drawMode, firstVertex, vertexCount);
}

void SYN::gfx::gl::Pass::drawInstanced(uint32_t vertexCount,
                                       uint32_t instanceCount,
                                       uint32_t firstVertex) {
    GLenum drawMode = getPrimitiveType(m_CurrentDrawTopology);
    glDrawArraysInstanced(drawMode, firstVertex, vertexCount, instanceCount);
}

void SYN::gfx::gl::Pass::drawIndexed(uint32_t indexCount) {
    GLenum drawMode = getPrimitiveType(m_CurrentDrawTopology);
    GLenum indexType = getIndexType(m_CurrentIndexType);

    glDrawElements(drawMode, indexCount, indexType, nullptr);
}

void SYN::gfx::gl::Pass::drawInstancedIndexed(uint32_t indexCount,
                                              uint32_t instanceCount) {
    GLenum drawMode = getPrimitiveType(m_CurrentDrawTopology);

    GLenum indexType = getIndexType(m_CurrentIndexType);

    glDrawElementsInstanced(drawMode, indexCount, indexType, nullptr,
                            instanceCount);
}

// Context
// Owns OpenGL resources internally. Use to create resource handles
// (vertex arrays, shaders, framebuffers, textures, etc.)
//
// Also for creating render passes with Context::beginPass(PassDesc{}).
//
// Create one Context per project using
// Context::createContext(ContextInitDesc{}).

SYN::gfx::gl::Pass SYN::gfx::gl::Context::beginPass(const PassDesc &desc) {
    return Pass(this, desc);
}

void SYN::gfx::gl::Context::updateTexture(Handle<Texture> textureHandle,
                                          uint32_t mipLevel, uint32_t x,
                                          uint32_t y, uint32_t width,
                                          uint32_t height, uint32_t face,
                                          TextureFormat format,
                                          const void *data) {
    std::optional<Texture> textureOpt =
        m_TextureRegistry.getResource(textureHandle);

    if (!textureOpt.has_value())
        return;

    Texture texture = textureOpt.value();

    GLenum glFormat = getTextureFormat(format);
    GLenum dataType = getTextureDataTypeFromFormat(format);

    if (texture.type == TextureType::Tex2D) {
        glTextureSubImage2D(texture.id, mipLevel, x, y, width, height, glFormat,
                            dataType, data);
    }
    if (texture.type == TextureType::Cubemap ||
        texture.type == TextureType::Tex2DArray) {
        glTextureSubImage3D(texture.id, mipLevel, x, y, face, width, height, 1,
                            glFormat, dataType, data);
    }
}

SYN::gfx::gl::Context::Context(const ContextInitDesc &desc) {
    m_Window = desc.windowHandle;
}

SYN::gfx::gl::Context::~Context() {
    if (!m_MainContext)
        return;
    std::vector<VertexArray> vertexArrays =
        m_VertexArrayRegistry.getAllResources();
    for (VertexArray vao : vertexArrays)
        glDeleteVertexArrays(1, &vao.id);

    std::vector<Buffer> buffers = m_BufferRegistry.getAllResources();
    for (Buffer buffer : buffers)
        glDeleteBuffers(1, &buffer.id);

    std::vector<Shader> shaders = m_ShaderRegistry.getAllResources();
    for (Shader shader : shaders) {
        glDeleteProgram(shader.program);
        if (shader.vertex != 0)
            glDeleteShader(shader.vertex);
        if (shader.fragment != 0)
            glDeleteShader(shader.fragment);
    }

    std::vector<Texture> textures = m_TextureRegistry.getAllResources();
    for (Texture texture : textures)
        glDeleteTextures(1, &texture.id);

    std::vector<Sampler> samplers = m_SamplerRegistry.getAllResources();
    for (Sampler sampler : samplers)
        glDeleteSamplers(1, &sampler.id);

    std::vector<Framebuffer> framebuffers =
        m_FramebufferRegistry.getAllResources();
    for (Framebuffer framebuffer : framebuffers)
        glDeleteFramebuffers(1, &framebuffer.id);

    std::vector<Renderbuffer> renderbuffers =
        m_RenderbufferRegistry.getAllResources();
    for (Renderbuffer renderbuffer : renderbuffers)
        glDeleteRenderbuffers(1, &renderbuffer.id);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

SYN::gfx::gl::Context::Context(Context &&other) {
    m_MainContext = true;
    m_Window = other.m_Window;

    other.m_MainContext = false;
    other.m_Window = nullptr;
}

SYN::gfx::gl::Context &SYN::gfx::gl::Context::operator=(Context &&other) {
    if (this != &other) {
        m_MainContext = true;
        m_Window = other.m_Window;

        other.m_Window = nullptr;
        other.m_MainContext = false;
    }
    return *this;
}

void SYN::gfx::gl::Context::enableVSync(bool vsync) { glfwSwapInterval(vsync); }

void SYN::gfx::gl::Context::present() { glfwSwapBuffers(m_Window); }

std::optional<SYN::gfx::gl::Context>
SYN::gfx::gl::Context::createContext(const ContextInitDesc &desc) {
    if (desc.windowHandle == nullptr) {
        spdlog::error(
            "Unable to establish OpenGL context with NULL window handle.");
        return std::nullopt;
    }

    glfwMakeContextCurrent(desc.windowHandle);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        spdlog::error(
            "Unable to load OpenGL functions. Cannot proceed with context.");
        return std::nullopt;
    }

    glViewport(0, 0, desc.width, desc.height);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // keyboard controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // gamepad controls (optional)

    ImGui_ImplGlfw_InitForOpenGL(desc.windowHandle, false);

    ImGui_ImplOpenGL3_Init("#version 450");

    TracyGpuContext;

    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &Globals.maxAnisotropy);

    return std::move(Context(desc));
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Buffer>>
SYN::gfx::gl::Context::createBuffer(const BufferDesc &desc,
                                    const void *initialData) {
    GLenum memoryUsage = 0;
    if (desc.usage == MemoryUsage::CpuToGPU) {
        memoryUsage = GL_DYNAMIC_STORAGE_BIT;
    }

    Buffer buffer;
    buffer.type = desc.bufferType;
    glCreateBuffers(1, &buffer.id);

    if (desc.size > 0) {
        glNamedBufferStorage(buffer.id, desc.size, initialData, memoryUsage);
    }

    return m_BufferRegistry.createHandle(buffer);
}

void SYN::gfx::gl::Context::updateBuffer(Handle<Buffer> bufferHandle,
                                         uint32_t offset, uint32_t size,
                                         const void *data) {
    std::optional<Buffer> buffer = m_BufferRegistry.getResource(bufferHandle);
    if (!buffer.has_value())
        return;
    glNamedBufferSubData(buffer.value().id, offset, size, data);
}

void SYN::gfx::gl::Context::updateBufferAndResize(Handle<Buffer> bufferHandle,
                                                  uint32_t size,
                                                  const void *data) {
    std::optional<Buffer> buffer = m_BufferRegistry.getResource(bufferHandle);
    if (!buffer.has_value())
        return;
    glNamedBufferData(buffer.value().id, size, data, GL_DYNAMIC_DRAW);
}

void SYN::gfx::gl::Context::deleteBuffer(Handle<Buffer> bufferHandle) {
    std::optional<Buffer> buffer = m_BufferRegistry.getResource(bufferHandle);
    if (!buffer.has_value())
        return;
    uint32_t bufferId = buffer.value().id;
    m_PendingDeleteBuffers.push_back(bufferId);
    m_BufferRegistry.releaseHandle(bufferHandle);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Framebuffer>>
SYN::gfx::gl::Context::createFramebuffer(const FramebufferDesc &desc) {
    Framebuffer framebuffer;
    glCreateFramebuffers(1, &framebuffer.id);

    size_t colorIndex = 0;
    for (const AttachmentDesc &colorAttachment : desc.colorAttachments) {
        if (std::holds_alternative<Handle<Texture>>(colorAttachment.handle)) {
            Texture texture = m_TextureRegistry
                                  .getResource(std::get<Handle<Texture>>(
                                      colorAttachment.handle))
                                  .value();
            glNamedFramebufferTexture(framebuffer.id,
                                      GL_COLOR_ATTACHMENT0 + colorIndex,
                                      texture.id, 0);
        }
        if (std::holds_alternative<Handle<Renderbuffer>>(
                colorAttachment.handle)) {
            Renderbuffer renderbuffer =
                m_RenderbufferRegistry
                    .getResource(
                        std::get<Handle<Renderbuffer>>(colorAttachment.handle))
                    .value();
            glNamedFramebufferRenderbuffer(framebuffer.id,
                                           GL_COLOR_ATTACHMENT0 + colorIndex,
                                           GL_RENDERBUFFER, renderbuffer.id);
        }

        ++colorIndex;
    }

    if (desc.depthStencilAttachment.has_value()) {
        const AttachmentDesc &attachmentDesc =
            desc.depthStencilAttachment.value();

        GLenum attachmentType = desc.isDepthOnly ? GL_DEPTH_ATTACHMENT
                                                 : GL_DEPTH_STENCIL_ATTACHMENT;

        if (std::holds_alternative<Handle<Texture>>(attachmentDesc.handle)) {
            Texture texture = m_TextureRegistry
                                  .getResource(std::get<Handle<Texture>>(
                                      attachmentDesc.handle))
                                  .value();

            glNamedFramebufferTexture(framebuffer.id, attachmentType,
                                      texture.id, 0);
        }
        if (std::holds_alternative<Handle<Renderbuffer>>(
                attachmentDesc.handle)) {
            Renderbuffer renderbuffer =
                m_RenderbufferRegistry
                    .getResource(
                        std::get<Handle<Renderbuffer>>(attachmentDesc.handle))
                    .value();
            glNamedFramebufferRenderbuffer(framebuffer.id, attachmentType,
                                           GL_RENDERBUFFER, renderbuffer.id);
        }
    }

    if (desc.colorAttachments.empty()) {
        glNamedFramebufferDrawBuffer(framebuffer.id, GL_NONE);
        glNamedFramebufferReadBuffer(framebuffer.id, GL_NONE);
    }

    if (glCheckNamedFramebufferStatus(framebuffer.id, GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        return std::nullopt;
    }

    return m_FramebufferRegistry.createHandle(framebuffer);
}

void SYN::gfx::gl::Context::setColorAttachment(Handle<Framebuffer> handle,
                                               uint32_t index,
                                               Handle<Texture> texture,
                                               uint32_t mip,
                                               std::optional<uint32_t> layer) {
    std::optional<Framebuffer> framebuffer =
        m_FramebufferRegistry.getResource(handle);
    if (!framebuffer.has_value())
        return;
    std::optional<Texture> textureOpt = m_TextureRegistry.getResource(texture);
    if (!textureOpt.has_value())
        return;

    uint32_t framebufferId = framebuffer.value().id;
    uint32_t textureId = textureOpt.value().id;

    if (layer.has_value()) {
        glNamedFramebufferTextureLayer(framebufferId,
                                       GL_COLOR_ATTACHMENT0 + index, textureId,
                                       mip, layer.value());
    } else {
        glNamedFramebufferTexture(framebufferId, GL_COLOR_ATTACHMENT0 + index,
                                  textureId, mip);
    }

    if (glCheckNamedFramebufferStatus(framebufferId, GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Unable to set color attachment for framebuffer!");
    }
}

void SYN::gfx::gl::Context::setDepthAttachment(Handle<Framebuffer> handle,
                                               Handle<Texture> texture,
                                               uint32_t mip,
                                               std::optional<uint32_t> layer) {
    std::optional<Framebuffer> framebuffer =
        m_FramebufferRegistry.getResource(handle);
    if (!framebuffer.has_value())
        return;
    std::optional<Texture> textureOpt = m_TextureRegistry.getResource(texture);
    if (!textureOpt.has_value())
        return;

    uint32_t framebufferId = framebuffer.value().id;
    uint32_t textureId = textureOpt.value().id;

    if (layer.has_value()) {
        glNamedFramebufferTextureLayer(framebufferId, GL_DEPTH_ATTACHMENT,
                                       textureId, mip, layer.value());
    } else {
        glNamedFramebufferTexture(framebufferId, GL_DEPTH_ATTACHMENT, textureId,
                                  mip);
    }

    if (glCheckNamedFramebufferStatus(framebufferId, GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Unable to set depth attachment for framebuffer!");
    }
}

void SYN::gfx::gl::Context::deleteFramebuffer(
    Handle<Framebuffer> framebufferHandle) {
    std::optional<Framebuffer> framebuffer =
        m_FramebufferRegistry.getResource(framebufferHandle);

    if (!framebuffer.has_value())
        return;

    uint32_t framebufferId = framebuffer.value().id;
    m_PendingDeleteFramebuffers.push_back(framebufferId);

    m_FramebufferRegistry.releaseHandle(framebufferHandle);
}

void SYN::gfx::gl::Context::blitFramebuffer(
    std::optional<Handle<Framebuffer>> readHandle,
    std::optional<Handle<Framebuffer>> writeHandle, Viewport sourceRect,
    Viewport destRect) {

    if (!readHandle) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    } else {
        Framebuffer framebuffer =
            m_FramebufferRegistry.getResource(readHandle.value()).value();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer.id);
    }

    if (!writeHandle) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    } else {
        Framebuffer framebuffer =
            m_FramebufferRegistry.getResource(writeHandle.value()).value();

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.id);
    }

    glBlitFramebuffer(sourceRect.x, sourceRect.y, sourceRect.width,
                      sourceRect.height, destRect.x, destRect.y, destRect.width,
                      destRect.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Renderbuffer>>
SYN::gfx::gl::Context::createRenderbuffer(const RenderbufferDesc &desc) {
    Renderbuffer renderbuffer;
    glCreateRenderbuffers(1, &renderbuffer.id);
    if (desc.sampleCount > 1) {
        glNamedRenderbufferStorageMultisample(
            renderbuffer.id, desc.sampleCount,
            getInternalTextureFormat(desc.format), desc.width, desc.height);
    } else {
        glNamedRenderbufferStorage(renderbuffer.id,
                                   getInternalTextureFormat(desc.format),
                                   desc.width, desc.height);
    }
    return m_RenderbufferRegistry.createHandle(renderbuffer);
}

void SYN::gfx::gl::Context::deleteRenderbuffer(
    Handle<Renderbuffer> renderbufferHandle) {
    std::optional<Renderbuffer> renderbufferOpt =
        m_RenderbufferRegistry.getResource(renderbufferHandle);

    if (!renderbufferOpt.has_value())
        return;

    Renderbuffer renderbuffer = renderbufferOpt.value();

    m_PendingDeleteRenderbuffers.push_back(renderbuffer.id);
    m_RenderbufferRegistry.releaseHandle(renderbufferHandle);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Shader>>
SYN::gfx::gl::Context::createShader(std::string_view vertexSource,
                                    std::string_view fragmentSource) {
    uint32_t vertexShader = glCreateShader(GL_VERTEX_SHADER);
    uint32_t fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *vertexSourceChar = vertexSource.data();
    const char *fragmentSourceChar = fragmentSource.data();

    glShaderSource(vertexShader, 1, &vertexSourceChar, nullptr);
    glShaderSource(fragmentShader, 1, &fragmentSourceChar, nullptr);

    glCompileShader(vertexShader);
    glCompileShader(fragmentShader);

    bool isVertexSuccessful = validateShaderCompileStatus(vertexShader, true);
    bool isFragmentSuccessful =
        validateShaderCompileStatus(fragmentShader, false);

    if (!isVertexSuccessful || !isFragmentSuccessful) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return std::nullopt;
    }

    uint32_t program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    if (!validateProgramLinkStatus(program)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program);
        return std::nullopt;
    }

    return m_ShaderRegistry.createHandle(
        Shader{program, vertexShader, fragmentShader});
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Shader>>
SYN::gfx::gl::Context::createShader(uint32_t vertexId, uint32_t fragmentId) {
    uint32_t program = glCreateProgram();
    glAttachShader(program, vertexId);
    glAttachShader(program, fragmentId);
    glLinkProgram(program);

    if (!validateProgramLinkStatus(program)) {
        glDeleteProgram(program);
        return std::nullopt;
    }

    return m_ShaderRegistry.createHandle(Shader{program, 0, 0});
}

void SYN::gfx::gl::Context::deleteShader(Handle<Shader> shaderHandle) {
    std::optional<Shader> shader = m_ShaderRegistry.getResource(shaderHandle);
    if (!shader.has_value())
        return;
    uint32_t programId = shader.value().program;
    uint32_t vertexId = shader.value().vertex;
    uint32_t fragmentId = shader.value().fragment;

    if (Globals.shaderUniformCache.find(programId) !=
        Globals.shaderUniformCache.cend()) {
        Globals.shaderUniformCache.erase(programId);
    }

    m_PendingDeletePrograms.push_back(programId);
    if (vertexId != 0)
        m_PendingDeleteShaders.push_back(vertexId);
    if (fragmentId != 0)
        m_PendingDeleteShaders.push_back(fragmentId);

    m_ShaderRegistry.releaseHandle(shaderHandle);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::VertexArray>>
SYN::gfx::gl::Context::createVertexArray(const VertexArrayDesc &desc) {
    std::optional<Buffer> vertexBuffer =
        m_BufferRegistry.getResource(desc.vertexBufferHandle);
    if (!vertexBuffer.has_value()) {
        return std::nullopt;
    }

    uint32_t vaoId;
    glCreateVertexArrays(1, &vaoId);

    for (uint32_t i = 0; i < desc.attributeCount; ++i) {
        VertexAttribDesc attribute = desc.attributes[i];
        glEnableVertexArrayAttrib(vaoId, attribute.location);
        glVertexArrayAttribBinding(vaoId, attribute.location,
                                   attribute.bindingIndex);

        GLenum type = GL_FLOAT;

        uint32_t size = 1;
        switch (attribute.format) {
        case VertexFormat::Float1:
            size = 1;
            break;
        case VertexFormat::Float2:
            size = 2;
            break;
        case VertexFormat::Float3:
            size = 3;
            break;
        case VertexFormat::Float4:
            size = 4;
            break;
        case VertexFormat::Int1:
            type = GL_INT;
            size = 1;
            break;
        case VertexFormat::Int2:
            type = GL_INT;
            size = 2;
            break;
        case VertexFormat::Int3:
            type = GL_INT;
            size = 3;
            break;
        case VertexFormat::Int4:
            type = GL_INT;
            size = 4;
            break;
        };

        if (type == GL_INT) {
            glVertexArrayAttribIFormat(vaoId, attribute.location, size, type,
                                       attribute.offset);
        } else {
            glVertexArrayAttribFormat(vaoId, attribute.location, size, type,
                                      attribute.normalized, attribute.offset);
        }
    }

    for (const auto &divisorDesc : desc.divisors) {
        glVertexArrayBindingDivisor(vaoId, divisorDesc.bindingIndex,
                                    divisorDesc.divisor);
    }

    assert(vertexBuffer.value().type == BufferType::Vertex);
    glVertexArrayVertexBuffer(vaoId, 0, vertexBuffer.value().id, 0,
                              desc.vertexBufferStride);
    std::optional<Buffer> indexBuffer =
        m_BufferRegistry.getResource(desc.indexBufferHandle);
    std::optional<IndexType> indexType = std::nullopt;
    if (indexBuffer.has_value()) {
        indexType = desc.indexType;
        assert(indexBuffer.value().type == BufferType::Index);
        glVertexArrayElementBuffer(vaoId, indexBuffer.value().id);
    }

    return m_VertexArrayRegistry.createHandle(VertexArray{vaoId, indexType});
}

void SYN::gfx::gl::Context::updateVertexArrayVertexBuffer(
    Handle<VertexArray> vertexArrayHandle, Handle<Buffer> bufferHandle,
    uint32_t bindingIndex, uint32_t offset, uint32_t stride) {
    std::optional<VertexArray> vaoOpt =
        m_VertexArrayRegistry.getResource(vertexArrayHandle);
    std::optional<Buffer> bufferOpt =
        m_BufferRegistry.getResource(bufferHandle);
    if (!vaoOpt.has_value() || !bufferOpt.has_value())
        return;
    VertexArray vao = vaoOpt.value();
    Buffer buffer = bufferOpt.value();
    assert(buffer.type == BufferType::Vertex &&
           "Buffer handle should be a vertex buffer.");
    glVertexArrayVertexBuffer(vao.id, bindingIndex, buffer.id, offset, stride);
}

void SYN::gfx::gl::Context::deleteVertexArray(
    Handle<VertexArray> vertexArrayHandle) {
    std::optional<VertexArray> vertexArray =
        m_VertexArrayRegistry.getResource(vertexArrayHandle);
    if (!vertexArray.has_value())
        return;
    uint32_t vaoId = vertexArray.value().id;
    m_PendingDeleteVertexArrays.push_back(vaoId);

    m_VertexArrayRegistry.releaseHandle(vertexArrayHandle);
}

void SYN::gfx::gl::Context::flushDeferredDeletes() {
    for (uint32_t shader : m_PendingDeleteShaders) {
        glDeleteShader(shader);
    }
    for (uint32_t program : m_PendingDeletePrograms) {
        glDeleteProgram(program);
    }

    if (!m_PendingDeleteVertexArrays.empty()) {
        glDeleteVertexArrays(m_PendingDeleteVertexArrays.size(),
                             &m_PendingDeleteVertexArrays[0]);
    }

    if (!m_PendingDeleteBuffers.empty()) {
        glDeleteBuffers(m_PendingDeleteBuffers.size(),
                        &m_PendingDeleteBuffers[0]);
    }

    if (!m_PendingDeleteTextures.empty()) {
        glDeleteTextures(m_PendingDeleteTextures.size(),
                         &m_PendingDeleteTextures[0]);
    }

    if (!m_PendingDeleteSamplers.empty()) {
        glDeleteSamplers(m_PendingDeleteSamplers.size(),
                         &m_PendingDeleteSamplers[0]);
    }

    if (!m_PendingDeleteFramebuffers.empty()) {
        glDeleteFramebuffers(m_PendingDeleteFramebuffers.size(),
                             &m_PendingDeleteFramebuffers[0]);
    }

    if (!m_PendingDeleteRenderbuffers.empty()) {
        glDeleteRenderbuffers(m_PendingDeleteRenderbuffers.size(),
                              &m_PendingDeleteRenderbuffers[0]);
    }

    m_PendingDeleteShaders.clear();
    m_PendingDeletePrograms.clear();
    m_PendingDeleteVertexArrays.clear();
    m_PendingDeleteBuffers.clear();
    m_PendingDeleteTextures.clear();
    m_PendingDeleteSamplers.clear();
    m_PendingDeleteFramebuffers.clear();
    m_PendingDeleteRenderbuffers.clear();
}

std::optional<SYN::gfx::gl::VertexArray>
SYN::gfx::gl::Context::getVertexArray(Handle<VertexArray> vertexArrayHandle) {
    return m_VertexArrayRegistry.getResource(vertexArrayHandle);
}
std::optional<SYN::gfx::gl::Buffer>
SYN::gfx::gl::Context::getBuffer(Handle<Buffer> bufferHandle) {
    return m_BufferRegistry.getResource(bufferHandle);
}

std::optional<SYN::gfx::gl::Texture>
SYN::gfx::gl::Context::getTexture(Handle<Texture> textureHandle) {
    return m_TextureRegistry.getResource(textureHandle);
}
std::optional<SYN::gfx::gl::Sampler>
SYN::gfx::gl::Context::getSampler(Handle<Sampler> samplerHandle) {
    return m_SamplerRegistry.getResource(samplerHandle);
}

std::optional<SYN::gfx::gl::Shader>
SYN::gfx::gl::Context::getShader(Handle<Shader> shaderHandle) {
    return m_ShaderRegistry.getResource(shaderHandle);
}

std::optional<SYN::gfx::gl::Framebuffer>
SYN::gfx::gl::Context::getFramebuffer(Handle<Framebuffer> framebufferHandle) {
    return m_FramebufferRegistry.getResource(framebufferHandle);
}

std::optional<SYN::gfx::gl::Renderbuffer>
SYN::gfx::gl::Context::getRenderbuffer(
    Handle<Renderbuffer> renderbufferHandle) {
    return m_RenderbufferRegistry.getResource(renderbufferHandle);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Texture>>
SYN::gfx::gl::Context::createTexture(const TextureDesc &desc,
                                     const void *initialData) {
    Texture texture{};

    GLenum internalFormat = getInternalTextureFormat(desc.format);
    GLenum format = getTextureFormat(desc.format);
    GLenum dataType = getTextureDataTypeFromFormat(desc.format);

    if (desc.type == TextureType::Tex2D) {
        if (desc.sampleCount > 1) {
            glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &texture.id);
            glTextureStorage2DMultisample(texture.id, desc.sampleCount,
                                          internalFormat, desc.width,
                                          desc.height, GL_TRUE);
        } else {
            glCreateTextures(GL_TEXTURE_2D, 1, &texture.id);
            glTextureStorage2D(texture.id, desc.mipLevel, internalFormat,
                               desc.width, desc.height);
        }

        if (initialData != nullptr) {
            glTextureSubImage2D(texture.id, 0, 0, 0, desc.width, desc.height,
                                format, dataType, initialData);
        }
    }

    if (desc.type == TextureType::Cubemap) {
        glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &texture.id);
        assert(desc.width == desc.height &&
               "Width and height must be the same for cubemap textures!");
        glTextureStorage2D(texture.id, desc.mipLevel, internalFormat,
                           desc.width, desc.height);
        if (initialData != nullptr) {
            glTextureSubImage3D(texture.id, 0, 0, 0, 0, desc.width, desc.height,
                                6, format, dataType, initialData);
        }
    }

    if (desc.type == TextureType::Tex2DArray) {
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texture.id);
        glTextureStorage3D(texture.id, desc.mipLevel, internalFormat,
                           desc.width, desc.height, desc.arraySize);
        if (initialData != nullptr) {
            glTextureSubImage3D(texture.id, 0, 0, 0, 0, desc.width, desc.height,
                                desc.arraySize, format, dataType, initialData);
        }
    }

    if (desc.mipLevel > 1) {
        glGenerateTextureMipmap(texture.id);
    }

    texture.type = desc.type;

    return m_TextureRegistry.createHandle(texture);
}

std::optional<uint32_t>
SYN::gfx::gl::Context::getTextureId(Handle<Texture> textureHandle) {
    std::optional<Texture> textureOpt =
        m_TextureRegistry.getResource(textureHandle);

    if (!textureOpt.has_value())
        return std::nullopt;

    Texture texture = textureOpt.value();
    return texture.id;
}

void SYN::gfx::gl::Context::deleteTexture(Handle<Texture> textureHandle) {
    std::optional<Texture> textureOpt =
        m_TextureRegistry.getResource(textureHandle);

    if (!textureOpt.has_value())
        return;

    Texture texture = textureOpt.value();

    m_PendingDeleteTextures.push_back(texture.id);
    m_TextureRegistry.releaseHandle(textureHandle);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Sampler>>
SYN::gfx::gl::Context::createSampler(const SamplerDesc &desc) {
    Sampler sampler;

    glCreateSamplers(1, &sampler.id);

    setSamplerParameters(sampler.id, desc);

    return m_SamplerRegistry.createHandle(sampler);
}

void SYN::gfx::gl::Context::updateSampler(Handle<Sampler> samplerHandle,
                                          const SamplerDesc &desc) {
    std::optional<Sampler> samplerOpt =
        m_SamplerRegistry.getResource(samplerHandle);

    if (!samplerOpt.has_value())
        return;

    Sampler sampler = samplerOpt.value();

    setSamplerParameters(sampler.id, desc);
}

void SYN::gfx::gl::Context::deleteSampler(Handle<Sampler> samplerHandle) {
    std::optional<Sampler> samplerOpt =
        m_SamplerRegistry.getResource(samplerHandle);

    if (!samplerOpt.has_value())
        return;

    Sampler sampler = samplerOpt.value();
    m_PendingDeleteSamplers.push_back(sampler.id);

    m_SamplerRegistry.releaseHandle(samplerHandle);
}

void SYN::gfx::gl::Context::generateMipmap(Handle<Texture> textureHandle) {
    std::optional<Texture> textureOpt =
        m_TextureRegistry.getResource(textureHandle);

    if (!textureOpt.has_value())
        return;

    Texture texture = textureOpt.value();

    glGenerateTextureMipmap(texture.id);
}

// Shader Cache
// Organizes shaders and allows for addition of features
// based on macros. Also serves as a preprocessor replacing
// #include with the proper code.

void SYN::gfx::gl::ShaderCache::registerIncludes(std::string_view includePath) {
    std::string includePathStr(includePath.data());
    std::fstream file(SHADER_PATH + includePathStr);
    if (!file.is_open()) {
        spdlog::error("Could not open file [{}]", includePath);
        return;
    }

    std::string includeContents =
        "\n" +
        std::string(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>()) +
        "\n";

    m_ShaderIncludes[includePathStr] = includeContents;
}

void SYN::gfx::gl::ShaderCache::registerFeature(std::string_view featureMacro,
                                                ShaderFeature feature) {
    double index = std::log2((uint32_t)feature);
    if (glm::fract(index) != 0.0) {
        spdlog::error("Feature integer for [{}] should be a power of 2",
                      featureMacro);
        return;
    }

    if ((uint32_t)index >= MAX_SHADER_FEATURES) {
        spdlog::error("Too many features. Max limit of {}",
                      MAX_SHADER_FEATURES);
        return;
    }

    if (!m_Features.at((uint32_t)index).empty()) {
        spdlog::error(
            "[{}] cannot replace an existing macro. Use another index.",
            featureMacro);
    }

    m_Features[(uint32_t)index] = featureMacro;
}

void SYN::gfx::gl::ShaderCache::registerShader(std::string_view name,
                                               std::string_view filePath) {
    std::fstream file(SHADER_PATH + std::string(filePath.data()));
    if (!file.is_open()) {
        spdlog::error("Could not open file [{}]", filePath);
        return;
    }

    m_ShaderSources[std::string(name)] = std::string(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

SYN::gfx::gl::Handle<SYN::gfx::gl::Shader>
SYN::gfx::gl::ShaderCache::getShaderHandle(Context &context,
                                           std::string_view name,
                                           uint32_t featureFlags) {
    if (!m_DefaultShader.has_value()) {
        m_DefaultShader = createDefaultShader(context);
    }

    std::string shaderName(name);
    ShaderKey key{std::string(shaderName), featureFlags};
    if (m_ShaderCache.find(key) != m_ShaderCache.cend()) {
        return m_ShaderCache.at(key);
    }
    if (m_ShaderSources.find(shaderName) == m_ShaderSources.cend()) {
        spdlog::error("Unable to find shader [{}]", name);
        return m_DefaultShader.value();
    }

    std::string header = "#version 450 core\n";

    uint32_t features = featureFlags;
    uint32_t pos = 0;
    while (features != 0) {
        uint32_t mask = 1 << pos;
        if (features & mask) {
            header += "#define " + m_Features[pos] + "\n";
            features &= ~mask;
        }
        ++pos;
    }

    auto replaceIncludes = [&](std::string src) -> std::string {
        std::string output;
        size_t i = src.npos;
        constexpr size_t BASE_OFFSET = std::string("#include").length();
        while (i = src.find("#include"), i != src.npos) {
            output += src.substr(0, i);
            uint32_t j = BASE_OFFSET;

            std::string includeName;
            bool startInclude = false;
            while (i + j < src.length()) {
                if (src.at(i + j) == '\"') {
                    if (startInclude)
                        break;
                    startInclude = true;
                    ++j;
                    continue;
                }
                if (startInclude) {
                    includeName += src.at(i + j);
                }
                ++j;
            }
            if (!startInclude ||
                (includeName.find(".glsl") == includeName.npos) ||
                includeName.empty()) {
                spdlog::error("Invalid include path. Should be a glsl file.");
                return "";
            }

            for (char c : includeName) {
                if (std::isspace(c)) {
                    spdlog::error(
                        "Invalid include path. No whitespaces allowed.");
                    return "";
                }
            }

            std::string includeSrc = "";

            bool shaderIncludeRegistered =
                m_ShaderIncludes.find(includeName) != m_ShaderIncludes.cend();

            if (!shaderIncludeRegistered) {
                registerIncludes(includeName);
            }

            shaderIncludeRegistered =
                m_ShaderIncludes.find(includeName) != m_ShaderIncludes.cend();

            if (shaderIncludeRegistered) {
                includeSrc = "\n" + m_ShaderIncludes.at(includeName) + "\n";
            } else {
                spdlog::error("Shader include [{}] doesn't exist.",
                              includeName);
                return "";
            }
            src = includeSrc + src.substr(i + j + 1);
        }
        output += src;

        return output;
    };

    std::string source = replaceIncludes(m_ShaderSources.at(shaderName));

    std::string fullVertexSrc = header + "#define VERTEX_SRC\n" + source;
    std::string fullFragmentSrc = header + "#define FRAGMENT_SRC\n" + source;

    std::optional<Handle<Shader>> shader =
        context.createShader(fullVertexSrc, fullFragmentSrc);

    if (!shader.has_value()) {
        spdlog::error("Failed to compile shader {}", name);
        return m_DefaultShader.value();
    }

    m_ShaderCache[key] = shader.value();

    return m_ShaderCache.at(key);
}

void SYN::gfx::gl::ShaderCache::reset(Context &context) {
    for (auto &[_, shader] : m_ShaderCache) {
        context.deleteShader(shader);
    }
    m_ShaderCache.clear();
    auto getFileContents = [](const std::string &filepath) -> std::string {
        std::fstream file(filepath);
        if (!file.is_open()) {
            spdlog::error("Could not open [{}] while resetting shader cache.",
                          filepath);
            return "";
        }
        return std::string(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
    };
    for (auto &[includePath, includeSource] : m_ShaderIncludes) {
        includeSource = getFileContents(SHADER_PATH + includePath);
    }
    for (auto &[shaderName, shaderSource] : m_ShaderSources) {
        shaderSource = getFileContents(SHADER_PATH + shaderName + ".glsl");
    }
}

// RenderTechnique
// Higher level passes for the renderer. Describe which groups a pass cares
// about, and how a pass should render each group.

void SYN::gfx::gl::RenderTechnique::setPassDesc(const PassDesc &desc) {
    m_PassDesc = desc;
}

SYN::gfx::gl::RenderTechnique &
SYN::gfx::gl::RenderTechnique::addGroup(const GroupDesc &desc) {
    m_Groups.emplace_back(desc);
    return *this;
}

SYN::gfx::gl::PassDesc SYN::gfx::gl::RenderTechnique::getPassDesc() const {
    return m_PassDesc;
}

const std::vector<SYN::gfx::gl::RenderTechnique::GroupDesc> &
SYN::gfx::gl::RenderTechnique::getGroups() const {
    return m_Groups;
}

SYN::gfx::gl::RenderTechnique &
SYN::gfx::gl::RenderTechnique::setShader(const std::string &name) {
    m_ShaderName = name;
    return *this;
}

SYN::gfx::gl::RenderTechnique &
SYN::gfx::gl::RenderTechnique::setBindUniformBase(const BindUniformFunc &func) {
    m_BindUniformBase = func;
    return *this;
}

SYN::gfx::gl::RenderTechnique &
SYN::gfx::gl::RenderTechnique::addFeatureUniform(ShaderFeature feature,
                                                 const BindUniformFunc &func) {
    m_BindFeature[(uint32_t)feature] = func;
    return *this;
}

void SYN::gfx::gl::RenderTechnique::bindUniforms(Pass &pass,
                                                 const GroupDesc &group,
                                                 const RenderItem &item) const {
    m_BindUniformBase(pass, item);

    uint32_t query = group.queryMask;
    uint32_t counter = 0;
    while ((query >> counter) > 0) {
        uint32_t mask = 1 << counter;
        uint32_t bit = query & mask;
        if (bit) {
            if (m_BindFeature.find(bit) != m_BindFeature.cend())
                m_BindFeature.at(bit)(pass, item);
        }
        ++counter;
    }
}

uint32_t SYN::gfx::gl::RenderTechnique::getDefaultShaderMask() const {
    return m_DefaultShaderFeature;
}

const std::string &SYN::gfx::gl::RenderTechnique::getShaderName() const {
    return m_ShaderName;
}

SYN::gfx::gl::RenderTechnique &
SYN::gfx::gl::RenderTechnique::setShaderFeature(uint32_t defaultFeature) {
    m_DefaultShaderFeature = defaultFeature;
    return *this;
}

// Renderer
// High level rendering API for users who want more out of the box with a simple
// interface.
//
// Goals:
//
// Provide an opinionated renderer that provides physically based shading,
// shadows, animations, post processing, and more high level features.
//
// Code first approach to defining game visuals.
//
// Basics:
//
// Create model handles using createModel(Context&, const ModelData&)
//
// At the start of each frame:
// beginFrame(const Camera&) -> Just sets main camera. Important to update
//                              every frame if camera moves/changes settings.
//
// At the end of each frame:
// endFrame() -> Processes all draw commands, and runs it through multiple
// passes to present final render.
//
// Call resize whenever main window is resized.
//
// In between beginFrame and endFrame:
// Submit draw commands per frame with submit(Handle<Model>, const glm::mat4&,
// std::span<const MaterialOverride>)
//
// More functions to tweak render settings, create environment maps for IBL,
// etc.

// TODO: Submit proper render commands based on scene description
void SYN::gfx::gl::Renderer::submitFrame(const RenderView3D &sceneDescription) {
    uint32_t modelCount = sceneDescription.models.size();

    Camera sceneCamera;
    auto cameraIt = std::find_if(
        sceneDescription.cameras.cbegin(), sceneDescription.cameras.cend(),
        [](const CameraView &camera) { return camera.isPrimary; });
    if (cameraIt != sceneDescription.cameras.cend()) {
        sceneCamera.fovYDegrees = cameraIt->fov;
        sceneCamera.nearPlane = cameraIt->near;
        sceneCamera.farPlane = cameraIt->far;
        sceneCamera.aspect = cameraIt->aspect;

        glm::vec3 pos, scale, skew;
        glm::quat orientation;
        glm::vec4 perspective;

        glm::decompose(cameraIt->worldTransform, scale, orientation, pos, skew,
                       perspective);

        sceneCamera.position = pos;
        sceneCamera.target = pos + (orientation * glm::vec3(0.0f, 0.0f, -1.0f));
        sceneCamera.up = orientation * sceneCamera.up;
    }
    beginFrame(sceneCamera);

    std::unordered_map<uint32_t, std::vector<MaterialView>> materialMap;
    std::unordered_map<uint32_t, std::span<const glm::mat4>> animationMap;
    for (uint32_t i = 0; i < modelCount; ++i) {
        for (const MaterialView &material : sceneDescription.materials) {
            if (material.modelIndex != i)
                continue;
            materialMap[i].emplace_back(material);
        }
        for (const AnimationView &animation : sceneDescription.animations) {
            if (animation.modelIndex != i)
                continue;
            animationMap[i] = animation.boneMatrices;
        }
    }

    for (uint32_t i = 0; i < modelCount; ++i) {
        UUID model = sceneDescription.models.at(i);
        glm::mat4 transform = sceneDescription.transforms.at(i);
        const std::vector<AABB> &meshBounds =
            sceneDescription.bounds.at(i).meshBounds;

        std::vector<MaterialOverride> materialOverride;
        if (auto it = materialMap.find(i); it != materialMap.cend()) {
            for (const MaterialView &view : it->second) {
                materialOverride.emplace_back(view.meshIndex, view.material);
            }
        }

        std::span<const glm::mat4> boneMatrices;
        if (auto it = animationMap.find(i); it != animationMap.cend()) {
            boneMatrices = it->second;
        }

        submit(*m_Context, model, transform, meshBounds, materialOverride,
               boneMatrices);
    }
}

void SYN::gfx::gl::Renderer::submitLineList(
    const std::vector<DebugDraw::Line> &lines, bool depthTest) {
    if (depthTest) {
        m_DebugDrawData.depthLines.insert(m_DebugDrawData.depthLines.end(),
                                          lines.cbegin(), lines.cend());
    } else {
        m_DebugDrawData.overlayLines.insert(m_DebugDrawData.overlayLines.end(),
                                            lines.cbegin(), lines.cend());
    }
}

void SYN::gfx::gl::Renderer::drawScene() {
    auto [width, height] = m_Window->getScreenSize();
    resize(width, height);
    endFrame(*m_Context);
}

SYN::gfx::gl::Renderer::Renderer(const RendererConfig &config) {
    m_RenderConfig = config;
    setExposure(glm::max(0.0f, config.exposure));
}

SYN::gfx::gl::Renderer::~Renderer() {}

void SYN::gfx::gl::Renderer::createModel(Context &context, UUID model) {
    const ModelData *modelData = m_AssetManager->get<ModelData>(model);

    Model loadedModel;
    uint32_t sourceIndex = 0;
    for (const MeshData &meshData : modelData->meshes) {
        Mesh mesh = createMesh(context, meshData);

        if (!mesh.material.albedo) {
            mesh.material.albedo = m_DefaultWhite;
        }

        mesh.sourceIndex = sourceIndex;

        if (mesh.material.alphaCutoff < 1.0f) {
            loadedModel.meshesMasked.push_back(mesh);
        } else {
            loadedModel.meshesOpaque.push_back(mesh);
        }
        ++sourceIndex;
    }
    loadedModel.skeleton = &modelData->skeleton;
    auto handle = m_ModelRegistry.createHandle(loadedModel);
    if (!handle.has_value()) {
        spdlog::error("Unable to create OpenGL model handle");
        handle.value();
    }
    m_UUIDToHandle[model] = {handle.value()};
}

// Material data is destroyed independently of mesh data.
void SYN::gfx::gl::Renderer::destroyModel(Context &context, UUID model) {
    auto it = m_UUIDToHandle.find(model);
    if (it == m_UUIDToHandle.cend()) {
        spdlog::warn("Unable to destroy model for unmapped UUID");
        return;
    }

    Handle<Model> modelHandle = std::get<Handle<Model>>(it->second.handle);

    if (!m_ModelRegistry.isValidHandle(modelHandle)) {
        return;
    }

    Model modelResource = m_ModelRegistry.getResource(modelHandle).value();
    m_ModelRegistry.releaseHandle(modelHandle);

    auto deleteMesh = [&](const Mesh &mesh) {
        context.deleteVertexArray(mesh.vao);
        context.deleteBuffer(mesh.vbo);
        context.deleteBuffer(mesh.ebo);

        if (mesh.material.sampler.has_value()) {
            context.deleteSampler(mesh.material.sampler.value());
        }
    };

    for (const Mesh &mesh : modelResource.meshesOpaque) {
        deleteMesh(mesh);
    }

    for (const Mesh &mesh : modelResource.meshesMasked) {
        deleteMesh(mesh);
    }

    m_UUIDToHandle.erase(it);
}

void SYN::gfx::gl::Renderer::createDefaultIrradianceMap(Context &context) {
    float irradianceData[] = {0.15f, 0.15f, 0.15f, 1.0f};
    m_DefaultIrradianceMap = context
                                 .createTexture({1, 1, TextureFormat::RGBA16F,
                                                 1, 1, TextureType::Cubemap})
                                 .value();
    for (uint32_t i = 0; i < 6; ++i) {
        context.updateTexture(m_DefaultIrradianceMap, 0, 0, 0, 1, 1, i,
                              TextureFormat::RGBA16F, &irradianceData[0]);
    }
}

void SYN::gfx::gl::Renderer::createDefaultPrefilterMap(Context &context) {
    float irradianceData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_DefaultPrefilterMap = context
                                .createTexture({1, 1, TextureFormat::RGBA16F, 1,
                                                1, TextureType::Cubemap})
                                .value();
}

void SYN::gfx::gl::Renderer::createCSM(Context &context) {
    m_CascadedShadowmap.isInstanced =
        hasGLExtension("GL_ARB_shader_viewport_layer_array");

    m_CascadedShadowmap.depthTextureNear =
        context
            .createTexture({m_RenderConfig.shadowMapNearResolution,
                            m_RenderConfig.shadowMapNearResolution,
                            TextureFormat::Depth16, 1, 1,
                            TextureType::Tex2DArray, 2})
            .value();
    m_CascadedShadowmap.depthTextureFar =
        context
            .createTexture({m_RenderConfig.shadowMapFarResolution,
                            m_RenderConfig.shadowMapFarResolution,
                            TextureFormat::Depth16, 1, 1,
                            TextureType::Tex2DArray, 2})
            .value();

    m_CascadedShadowmap.fboNear =
        context
            .createFramebuffer(
                {{},
                 AttachmentDesc{m_CascadedShadowmap.depthTextureNear},
                 true})
            .value();

    m_CascadedShadowmap.fboFar =
        context
            .createFramebuffer(
                {{}, AttachmentDesc{m_CascadedShadowmap.depthTextureFar}, true})
            .value();

    m_CascadedShadowmap.shadowSampler =
        context
            .createSampler({SampleFilter::Linear, SampleFilter::Linear,
                            WrapMode::ClampToBorder, WrapMode::ClampToBorder,
                            WrapMode::ClampToBorder, glm::vec4(1.0f), true})
            .value();

    m_CascadedShadowmap.planeDistances.resize(4);
    m_CascadedShadowmap.lightSpaceMatrices.resize(4);
    m_CascadedShadowmap.cascadeTexelWorldSize.resize(4);
}

void SYN::gfx::gl::Renderer::initShaderCache() {
    m_ShaderCache.registerIncludes("common.glsl");
    m_ShaderCache.registerIncludes("pbr_brdf.glsl");
    m_ShaderCache.registerIncludes("importance_sample.glsl");

    m_ShaderCache.registerFeature("FEATURE_NORMAL", ShaderFeature::Normal);
    m_ShaderCache.registerFeature("FEATURE_METALLIC_ROUGHNESS",
                                  ShaderFeature::MetallicRoughness);
    m_ShaderCache.registerFeature("FEATURE_SKINNED", ShaderFeature::Skinned);
    m_ShaderCache.registerFeature("FEATURE_ALPHA_TEST",
                                  ShaderFeature::AlphaTest);
    m_ShaderCache.registerFeature("FEATURE_DEPTH_MAP_INSTANCED",
                                  ShaderFeature::DepthMapInstanced);

    m_ShaderCache.registerShader("forward", "forward.glsl");
    m_ShaderCache.registerShader("skybox", "skybox.glsl");
    m_ShaderCache.registerShader("brdf_lut", "brdf_lut.glsl");
    m_ShaderCache.registerShader("depth_map", "depth_map.glsl");
    m_ShaderCache.registerShader("equirectangularToCubemap",
                                 "equirectangularToCubemap.glsl");

    m_ShaderCache.registerShader("hdr", "hdr.glsl");
    m_ShaderCache.registerShader("irradiance_map", "irradiance_map.glsl");
    m_ShaderCache.registerShader("prefiltered_env", "prefiltered_env.glsl");
    m_ShaderCache.registerShader("z_prepass", "z_prepass.glsl");

    m_ShaderCache.registerShader("lines", "lines.glsl");
}

void SYN::gfx::gl::Renderer::initDefaultUBOs(Context &context) {
    m_ShadowConstants =
        context
            .createBuffer({BufferType::Uniform, MemoryUsage::CpuToGPU,
                           sizeof(ShadowConstants)})
            .value();

    m_CameraConstants =
        context
            .createBuffer({BufferType::Uniform, MemoryUsage::CpuToGPU,
                           sizeof(CameraConstants)})
            .value();

    m_LightConstants =
        context
            .createBuffer({BufferType::Uniform, MemoryUsage::CpuToGPU,
                           sizeof(LightConstants)})
            .value();

    Pass pass = context.beginPass({});
    pass.bindUniformBuffer(0, m_CameraConstants);
    pass.bindUniformBuffer(1, m_ShadowConstants);
    pass.bindUniformBuffer(2, m_LightConstants);
}

void SYN::gfx::gl::Renderer::createTextureDefaults(Context &context) {
    m_DefaultWhite = createDefaultColoredTexture(context, {255, 255, 255, 255});

    m_DefaultModelSamplerDesc = {SampleFilter::Linear_Mipmap_Linear,
                                 SampleFilter::Linear, WrapMode::Repeat,
                                 WrapMode::Repeat};
    m_AnisotropicFilter = 8.0f;
    m_DefaultModelSamplerDesc.anisotropicLevel = m_AnisotropicFilter;
    m_DefaultModelSampler =
        context.createSampler(m_DefaultModelSamplerDesc).value();

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void SYN::gfx::gl::Renderer::createZPrepassTechnique() {
    PipelineState zPrepassPipeline;
    zPrepassPipeline.depth.writeEnabled = true;
    zPrepassPipeline.color = {false, false, false, false};
    zPrepassPipeline.cullMode = CullMode::Back;

    m_ZPrepass.setShader("z_prepass")
        .setBindUniformBase([&](Pass &pass, const RenderItem &item) {
            const Material &material = item.material;
            Handle<Sampler> sampler =
                material.sampler.value_or(m_DefaultModelSampler);

            pass.bindUniform("u_Model", item.transform);
        })
        .addFeatureUniform(ShaderFeature::Skinned,
                           [&](Pass &pass, const RenderItem &item) {
                               if (!item.boneOffset.has_value())
                                   return;
                               bindBoneMatrices(pass, item.boneOffset.value());
                           })
        .addFeatureUniform(
            ShaderFeature::AlphaTest,
            [&](Pass &pass, const RenderItem &item) {
                const Material &material = item.material;
                Handle<Sampler> sampler =
                    material.sampler.value_or(m_DefaultModelSampler);

                if (material.albedo) {
                    pass.bindTexture(0, material.albedo.value(), sampler);
                }

                pass.bindUniform("u_alphaCutoff", item.material.alphaCutoff);
            })
        .addGroup(
            {0, (uint32_t)ShaderFeature::Skinned,
             [zPrepassPipeline]() -> PipelineState { return zPrepassPipeline; },
             true})
        .addGroup(
            {(uint32_t)ShaderFeature::Skinned, 0,
             [zPrepassPipeline]() -> PipelineState { return zPrepassPipeline; },
             true})
        .addGroup({(uint32_t)ShaderFeature::AlphaTest,
                   (uint32_t)ShaderFeature::Skinned,
                   [zPrepassPipeline]() mutable -> PipelineState {
                       zPrepassPipeline.cullMode = CullMode::None;
                       return zPrepassPipeline;
                   },
                   true})
        .addGroup({(uint32_t)ShaderFeature::AlphaTest |
                       (uint32_t)ShaderFeature::Skinned,
                   0,
                   [zPrepassPipeline]() mutable -> PipelineState {
                       zPrepassPipeline.cullMode = CullMode::None;
                       return zPrepassPipeline;
                   },
                   true});
}

void SYN::gfx::gl::Renderer::createForwardPassTechnique() {
    PipelineState pipeline;
    pipeline.depth.test = DepthFunc::Equal;
    pipeline.depth.writeEnabled = false;
    pipeline.color = {true, true, true, true};
    pipeline.cullMode = CullMode::Back;

    m_ForwardPass.setShader("forward")
        .setBindUniformBase([&](Pass &pass, const RenderItem &item) {
            auto it = m_NameToEnvironment.find(m_CurrentEnvironment);

            pass.bindTexture(3, it->second.irradianceMap,
                             m_CubemapSampler.value());

            pass.bindTexture(4, it->second.prefilterMap,
                             m_MipmapCubeSampler.value());

            pass.bindTexture(5, m_BRDFLut, m_CubemapSampler.value());

            pass.bindTexture(6, m_CascadedShadowmap.depthTextureNear,
                             m_CascadedShadowmap.shadowSampler);

            pass.bindTexture(7, m_CascadedShadowmap.depthTextureFar,
                             m_CascadedShadowmap.shadowSampler);

            const Material &material = item.material;
            pass.bindUniform("u_tint", material.tint.r, material.tint.g,
                             material.tint.b);
            pass.bindUniform("u_metallic", material.metallic);
            pass.bindUniform("u_roughness", material.roughness);

            Handle<Sampler> sampler =
                material.sampler.value_or(m_DefaultModelSampler);

            if (material.albedo) {
                pass.bindTexture(0, material.albedo.value(), sampler);
            }

            if (material.normalMap) {
                pass.bindTexture(1, material.normalMap.value(), sampler);
            }

            if (material.metallicRoughnessMap) {
                pass.bindTexture(2, material.metallicRoughnessMap.value(),
                                 sampler);
            }

            pass.bindUniform("u_Model", item.transform);
        })
        .addFeatureUniform(ShaderFeature::Skinned,
                           [&](Pass &pass, const RenderItem &item) {
                               if (!item.boneOffset.has_value())
                                   return;
                               bindBoneMatrices(pass, item.boneOffset.value());
                           })
        .addGroup({0, (uint32_t)ShaderFeature::Skinned,
                   [pipeline]() -> PipelineState { return pipeline; }, true,
                   true})
        .addGroup({(uint32_t)ShaderFeature::Skinned, 0,
                   [pipeline]() -> PipelineState { return pipeline; }, true,
                   true})
        .addGroup({(uint32_t)ShaderFeature::AlphaTest,
                   (uint32_t)ShaderFeature::Skinned,
                   [pipeline]() mutable -> PipelineState {
                       pipeline.cullMode = CullMode::None;
                       return pipeline;
                   },
                   true, true})
        .addGroup({(uint32_t)ShaderFeature::AlphaTest |
                       (uint32_t)ShaderFeature::Skinned,
                   0,
                   [pipeline]() mutable -> PipelineState {
                       pipeline.cullMode = CullMode::None;
                       return pipeline;
                   },
                   true, true});
}

void SYN::gfx::gl::Renderer::createShadowPassTechnique() {
    m_ShadowPass.setShader("depth_map")
        .setShaderFeature(m_CascadedShadowmap.isInstanced
                              ? (uint32_t)ShaderFeature::DepthMapInstanced
                              : 0)
        .addFeatureUniform(ShaderFeature::Skinned,
                           [&](Pass &pass, const RenderItem &item) {
                               if (!item.boneOffset.has_value())
                                   return;
                               bindBoneMatrices(pass, item.boneOffset.value());
                           })
        .addFeatureUniform(
            ShaderFeature::AlphaTest,
            [&](Pass &pass, const RenderItem &item) {
                Handle<Sampler> sampler =
                    item.material.sampler.value_or(m_DefaultModelSampler);

                Handle<Texture> albedo =
                    item.material.albedo.value_or(m_DefaultWhite);

                pass.bindTexture(0, albedo, sampler);
                pass.bindUniform("u_alphaCutoff", item.material.alphaCutoff);
            })
        .addGroup({
            0,
            (uint32_t)ShaderFeature::Skinned,
            [&]() -> PipelineState {
                PipelineState pipeline;
                pipeline.cullMode = CullMode::Back;
                return pipeline;
            },
            false,
            false,
            m_CascadedShadowmap.isInstanced
                ? std::optional{[](Pass &pass, const RenderItem &item) {
                      pass.bindVertexArray(item.vao);
                      pass.drawInstancedIndexed(item.indexCount, 2);
                  }}
                : std::nullopt,
        })
        .addGroup({
            (uint32_t)ShaderFeature::Skinned,
            0,
            [&]() -> PipelineState {
                PipelineState pipeline;
                pipeline.cullMode = CullMode::Back;
                return pipeline;
            },
            false,
            false,
            m_CascadedShadowmap.isInstanced
                ? std::optional{[](Pass &pass, const RenderItem &item) {
                      pass.bindVertexArray(item.vao);
                      pass.drawInstancedIndexed(item.indexCount, 2);
                  }}
                : std::nullopt,
        })
        .addGroup({
            (uint32_t)ShaderFeature::AlphaTest,
            (uint32_t)ShaderFeature::Skinned,
            [&]() -> PipelineState {
                PipelineState pipeline;
                pipeline.cullMode = CullMode::None;
                return pipeline;
            },
            false,
            false,
            m_CascadedShadowmap.isInstanced
                ? std::optional{[](Pass &pass, const RenderItem &item) {
                      pass.bindVertexArray(item.vao);
                      pass.drawInstancedIndexed(item.indexCount, 2);
                  }}
                : std::nullopt,
        })
        .addGroup({
            (uint32_t)ShaderFeature::AlphaTest |
                (uint32_t)ShaderFeature::Skinned,
            0,
            [&]() -> PipelineState {
                PipelineState pipeline;
                pipeline.cullMode = CullMode::None;
                return pipeline;
            },
            false,
            false,
            m_CascadedShadowmap.isInstanced
                ? std::optional{[](Pass &pass, const RenderItem &item) {
                      pass.bindVertexArray(item.vao);
                      pass.drawInstancedIndexed(item.indexCount, 2);
                  }}
                : std::nullopt,
        });
}

void SYN::gfx::gl::Renderer::createLineData(Context &context) {
    m_DebugDrawData.lineDepthBuffer =
        context.createBuffer({BufferType::Vertex, MemoryUsage::CpuToGPU, 0})
            .value();

    m_DebugDrawData.lineOverlayBuffer =
        context.createBuffer({BufferType::Vertex, MemoryUsage::CpuToGPU, 0})
            .value();

    float vertices[] = {0.0f, -0.5f, 0.0f, 0.0f, 0.5f,  0.0f,
                        0.0f, 0.5f,  1.0f, 0.0f, -0.5f, 1.0f};

    m_DebugDrawData.linePrimitiveBuffer =
        context
            .createBuffer(
                {BufferType::Vertex, MemoryUsage::CpuToGPU, sizeof(vertices)},
                &vertices[0])
            .value();

    VertexArrayDesc vaoDesc;
    vaoDesc.vertexBufferHandle = m_DebugDrawData.linePrimitiveBuffer;
    vaoDesc.vertexBufferStride = sizeof(float) * 3;

    // Primitive data
    vaoDesc.attributes[0] = {0, VertexFormat::Float3, 0, false, 0};

    // Instance Data
    vaoDesc.attributes[1] = {1, VertexFormat::Float3, 0, false, 1};
    vaoDesc.attributes[2] = {2, VertexFormat::Float3,
                             offsetof(DebugDraw::Line, pointB), false, 1};
    vaoDesc.attributes[3] = {3, VertexFormat::Float4,
                             offsetof(DebugDraw::Line, color), false, 1};

    vaoDesc.attributeCount = 4;

    std::array<VertexAttribDivisor, 2> divisors = {
        VertexAttribDivisor{0, 0},
        VertexAttribDivisor{1, 1},
    };

    vaoDesc.divisors = divisors;

    m_DebugDrawData.lineVAO = context.createVertexArray(vaoDesc).value();
}

void SYN::gfx::gl::Renderer::init(EngineContext *engineContext) {
    if (engineContext == nullptr) {
        spdlog::critical(
            "Cannot initialize renderer as engine context is NULL.");
        return;
    }
    if (engineContext->projectConfig.assetManager == nullptr) {
        spdlog::critical("Cannot pass NULL asset manager to renderer.");
        return;
    }

    m_Context = engineContext->glContext.get();
    m_Window = engineContext->window.get();
    m_AssetManager = engineContext->projectConfig.assetManager.get();

    initShaderCache();
    initDefaultUBOs(*m_Context);
    createScreenQuad(*m_Context);
    createHdrShader(*m_Context);
    createDefaultPrefilterMap(*m_Context);
    createDefaultIrradianceMap(*m_Context);
    createSkybox(*m_Context);
    createCSM(*m_Context);
    createTextureDefaults(*m_Context);
    createLineData(*m_Context);

    m_BRDFLut = createBRDFLut(*m_Context);

    createZPrepassTechnique();
    createForwardPassTechnique();
    createShadowPassTechnique();

    EnvironmentResource defaultEnvironment{};
    defaultEnvironment.prefilterMap = m_DefaultPrefilterMap;
    defaultEnvironment.irradianceMap = m_DefaultIrradianceMap;
    m_NameToEnvironment[DEFAULT_ENVIRONMENT_NAME] = defaultEnvironment;

    m_AssetManager->registerCleanup([&](UUID id) {
        auto handleIt = m_UUIDToHandle.find(id);
        if (handleIt == m_UUIDToHandle.cend())
            return;
        ResourceHandle resource = handleIt->second;
        std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Handle<Texture>>) {
                    m_Context->deleteTexture(arg);
                    m_UUIDToHandle.erase(handleIt);
                } else if constexpr (std::is_same_v<T, Handle<Model>>) {
                    destroyModel(*m_Context, id);
                }
            },
            resource.handle);
    });
}

void SYN::gfx::gl::Renderer::createEnvironment(Context &context,
                                               std::string_view name,
                                               const Environment &environment) {
    std::string key(name);
    if (m_NameToEnvironment.contains(key)) {
        spdlog::error("Cannot create new environment under name [{}] as it is "
                      "already taken.",
                      key);
        return;
    }

    if (environment.cubemap.size() != 1 && environment.cubemap.size() != 6) {
        spdlog::error("You must add either one image, or 6 images to the "
                      "environment cubemap. One image is for HDR maps, 6 "
                      "images is for a normal skybox.");
        return;
    }

    auto createCubemap = [&](const std::vector<UUID> &textures) {
        const TextureData *face1 =
            m_AssetManager->get<TextureData>(textures[0]);

        uint32_t width = face1->width, height = face1->height;

        TextureFormat cubeFormat = TextureFormat::SRGBA;
        if (face1->channelCount == 3) {
            cubeFormat = TextureFormat::SRGB;
        }

        Handle<Texture> cubemap =
            context
                .createTexture(
                    {width, height, cubeFormat, 1, 1, TextureType::Cubemap})
                .value();

        for (uint32_t i = 0; i < textures.size(); ++i) {
            const TextureData *face =
                m_AssetManager->get<TextureData>(textures[i]);
            context.updateTexture(cubemap, 0, 0, 0, width, height, i,
                                  cubeFormat, &face->data[0]);
        }

        return cubemap;
    };

    // https://learnopengl.com/code_viewer_gh.php?code=src/6.pbr/2.1.1.ibl_irradiance_conversion/ibl_irradiance_conversion.cpp
    glm::mat4 captureProjection =
        glm::perspectiveRH_NO(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = {
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                      glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, -1.0f, 0.0f))};

    auto createHdrmap = [&](UUID texture) {
        const TextureData *face = m_AssetManager->get<TextureData>(texture);

        uint32_t width = face->width, height = face->height;

        Handle<Texture> envCubemap =
            context
                .createTexture({512, 512, TextureFormat::RGBA16F, 10, 1,
                                TextureType::Cubemap})
                .value();

        Handle<Texture> hdrTexture =
            context
                .createTexture({width, height, TextureFormat::RGBA16F},
                               &face->dataFloat[0])
                .value();

        Handle<Framebuffer> equirectangularProjection =
            context.createFramebuffer({std::array{AttachmentDesc{envCubemap}}})
                .value();

        Handle<Sampler> mipSampler =
            context
                .createSampler({SampleFilter::Linear_Mipmap_Linear,
                                SampleFilter::Linear, WrapMode::ClampToEdge,
                                WrapMode::ClampToEdge, WrapMode::ClampToEdge})
                .value();

        PipelineState pipeline;
        pipeline.shader = m_ShaderCache.getShaderHandle(
            context, "equirectangularToCubemap", 0);
        for (uint32_t i = 0; i < 6; ++i) {
            context.setColorAttachment(equirectangularProjection, 0, envCubemap,
                                       0, i);
            Pass pass = context.beginPass({equirectangularProjection,
                                           glm::vec4(1.0f), false, false, false,
                                           Viewport{0, 0, 512, 512}});

            pass.usePipeline(pipeline);
            pass.bindTexture(0, hdrTexture, mipSampler);
            pass.bindUniform("u_ViewProjection",
                             captureProjection * captureViews[i]);
            pass.bindUniform("u_equirectangularMap", 0);
            pass.bindVertexArray(m_SkyboxCube.value());
            pass.draw(36);
        }

        context.generateMipmap(envCubemap);

        context.deleteTexture(hdrTexture);
        context.deleteFramebuffer(equirectangularProjection);
        context.deleteSampler(mipSampler);

        return envCubemap;
    };

    auto createIrradianceMap = [&](Handle<Texture> hdrMap) {
        constexpr uint32_t mapResolution = 32;
        Handle<Texture> irradianceMap =
            context
                .createTexture({mapResolution, mapResolution,
                                TextureFormat::RGBA16F, 1, 1,
                                TextureType::Cubemap})
                .value();
        Handle<Framebuffer> captureFramebuffer =
            context
                .createFramebuffer({std::array{AttachmentDesc{irradianceMap}}})
                .value();

        Handle<Sampler> mipSampler =
            context
                .createSampler({SampleFilter::Linear_Mipmap_Linear,
                                SampleFilter::Linear, WrapMode::ClampToEdge,
                                WrapMode::ClampToEdge, WrapMode::ClampToEdge})
                .value();

        PipelineState pipeline;
        pipeline.shader =
            m_ShaderCache.getShaderHandle(context, "irradiance_map", 0);
        for (uint32_t i = 0; i < 6; ++i) {
            context.setColorAttachment(captureFramebuffer, 0, irradianceMap, 0,
                                       i);
            Pass pass = context.beginPass(
                {captureFramebuffer, glm::vec4(1.0), false, false, false,
                 Viewport{0, 0, mapResolution, mapResolution}});
            pass.usePipeline(pipeline);
            pass.bindTexture(0, hdrMap, mipSampler);
            pass.bindUniform("u_hdrMap", 0);
            pass.bindUniform("u_ViewProjection",
                             captureProjection * captureViews[i]);
            pass.bindVertexArray(m_SkyboxCube.value());
            pass.draw(36);
        }

        context.deleteFramebuffer(captureFramebuffer);
        context.deleteSampler(mipSampler);

        return irradianceMap;
    };

    auto createPrefilteredEnvironmentMap = [&](Handle<Texture> hdrMap) {
        uint32_t maxMipCount = 8;

        constexpr uint32_t mapResolution = 128;
        Handle<Texture> prefilteredMap =
            context
                .createTexture({mapResolution, mapResolution,
                                TextureFormat::RGBA16F, maxMipCount, 1,
                                TextureType::Cubemap})
                .value();

        Handle<Framebuffer> captureFramebuffer =
            context
                .createFramebuffer({std::array{AttachmentDesc{prefilteredMap}}})
                .value();

        Handle<Sampler> mipSampler =
            context
                .createSampler({SampleFilter::Linear_Mipmap_Linear,
                                SampleFilter::Linear, WrapMode::ClampToEdge,
                                WrapMode::ClampToEdge, WrapMode::ClampToEdge})
                .value();

        PipelineState pipeline;
        pipeline.shader =
            m_ShaderCache.getShaderHandle(context, "prefiltered_env", 0);

        for (uint32_t i = 0; i < maxMipCount; ++i) {
            uint32_t mipSize =
                (uint32_t)((float)mapResolution * std::pow(0.5f, (float)i));
            for (int j = 0; j < 6; ++j) {
                context.setColorAttachment(captureFramebuffer, 0,
                                           prefilteredMap, i, j);
                Pass pass = context.beginPass(
                    {captureFramebuffer, glm::vec4(1.0), false, false, false,
                     Viewport{0, 0, mipSize, mipSize}});

                pass.usePipeline(pipeline);
                pass.bindTexture(0, hdrMap, mipSampler);
                pass.bindUniform("u_hdrMap", 0);
                pass.bindUniform("u_ViewProjection",
                                 captureProjection * captureViews[j]);
                float roughness = (float)i / (float)(maxMipCount - 1);
                pass.bindUniform("u_roughness", roughness);
                pass.bindVertexArray(m_SkyboxCube.value());
                pass.draw(36);
            }
        }

        context.deleteFramebuffer(captureFramebuffer);
        context.deleteSampler(mipSampler);

        return prefilteredMap;
    };

    EnvironmentResource resource;
    resource.type = environment.type;
    resource.clearColor = environment.clearColor;
    resource.exposure = environment.exposure;
    resource.gamma = environment.gamma;
    resource.bloomEnabled = environment.bloomEnabled;

    if (resource.type == Environment::Type::ClearColor) {
        m_NameToEnvironment[key] = resource;
        return;
    }

    for (UUID uuid : environment.cubemap) {
        resource.textures.emplace_back(m_AssetManager->acquire(uuid));
    }
    if (environment.cubemap.size() == 6) {
        resource.cubemap = createCubemap(environment.cubemap);
        resource.irradianceMap = m_DefaultIrradianceMap;
        resource.prefilterMap = m_DefaultPrefilterMap;
    } else {
        resource.cubemap = createHdrmap(environment.cubemap[0]);
        resource.irradianceMap = createIrradianceMap(resource.cubemap);
        resource.prefilterMap =
            createPrefilteredEnvironmentMap(resource.cubemap);
    }

    m_NameToEnvironment[key] = resource;
}

void SYN::gfx::gl::Renderer::setEnvironment(
    std::optional<std::string_view> name) {
    if (!name.has_value()) {
        m_CurrentEnvironment = DEFAULT_ENVIRONMENT_NAME;
        return;
    }
    std::string nameValue = std::string(name.value());
    if (!m_NameToEnvironment.contains(nameValue)) {
        spdlog::error("[{}] not found in renderer's environment cache. Please "
                      "add it first.",
                      nameValue);
        m_CurrentEnvironment = DEFAULT_ENVIRONMENT_NAME;
        return;
    }
    m_CurrentEnvironment = nameValue;
}

void SYN::gfx::gl::Renderer::destroyEnvironment(Context &context,
                                                std::string_view name) {
    auto it = m_NameToEnvironment.find(std::string(name));
    if (it == m_NameToEnvironment.cend())
        return;
    EnvironmentResource *resource = &it->second;
    context.deleteTexture(resource->irradianceMap);
    context.deleteTexture(resource->prefilterMap);
    context.deleteTexture(resource->cubemap);
    m_NameToEnvironment.erase(it);
}

void SYN::gfx::gl::Renderer::beginFrame(const Camera &camera) {
    m_MainCamera = camera;
}

void SYN::gfx::gl::Renderer::setDirectionalLight(
    const DirectionalLight &light) {
    m_DirectionalLight = light;
    m_DirectionalLight.direction = glm::normalize(m_DirectionalLight.direction);
}

void SYN::gfx::gl::Renderer::submit(
    Context &context, UUID model, const glm::mat4 &transform,
    std::span<const AABB> meshBounds,
    std::span<const MaterialOverride> materialOverride,
    std::span<const glm::mat4> boneMatrices) {

    std::optional<uint32_t> boneIndex = std::nullopt;

    if (!boneMatrices.empty()) {
        boneIndex = m_FrameBoneMatrices.size();
        m_FrameBoneMatrices.resize(m_FrameBoneMatrices.size() + MAX_BONES,
                                   glm::mat4(1.0f));
        for (uint32_t i = 0; i < boneMatrices.size() && i < MAX_BONES; ++i) {
            m_FrameBoneMatrices[boneIndex.value() + i] = boneMatrices[i];
        }
    }

    auto it = m_UUIDToHandle.find(model);
    if (it == m_UUIDToHandle.cend()) {
        createModel(context, model);
    }
    it = m_UUIDToHandle.find(model);

    m_DrawCommandList.emplace_back(
        std::get<Handle<Model>>(it->second.handle), transform,
        std::vector<MaterialOverride>(materialOverride.begin(),
                                      materialOverride.end()),
        boneIndex, std::vector<AABB>(meshBounds.cbegin(), meshBounds.cend()));
}

std::tuple<uint32_t, uint32_t> SYN::gfx::gl::Renderer::getRenderResolution() {
    uint32_t w =
        glm::round((float)m_ScreenViewport.width * m_RenderConfig.renderScale);
    uint32_t h =
        glm::round((float)m_ScreenViewport.height * m_RenderConfig.renderScale);
    return std::make_tuple(w, h);
}

void SYN::gfx::gl::Renderer::updateMsaaFramebuffer(Context &context) {
    if (!m_MsaaFramebuffer.update)
        return;

    m_MsaaFramebuffer.update = false;

    if (m_MsaaFramebuffer.rbAttachment) {
        context.deleteRenderbuffer(m_MsaaFramebuffer.rbAttachment.value());
    }
    if (m_MsaaFramebuffer.colorAttachment) {
        context.deleteTexture(m_MsaaFramebuffer.colorAttachment.value());
    }
    if (m_MsaaFramebuffer.handle) {
        context.deleteFramebuffer(m_MsaaFramebuffer.handle.value());
    }

    uint32_t samples = 1;
    switch (m_RenderConfig.aaMode) {
    case AntiAliasMode::None:
        break;
    case AntiAliasMode::FXAA:
        break;
    case AntiAliasMode::MSAA_2x:
        samples = 2;
        break;
    case AntiAliasMode::MSAA_4x:
        samples = 4;
        break;
    case AntiAliasMode::MSAA_8x:
        samples = 8;
        break;
    }

    auto [width, height] = getRenderResolution();

    m_MsaaFramebuffer.rbAttachment =
        context
            .createRenderbuffer(
                {TextureFormat::Depth24Stencil8, width, height, samples})
            .value();

    m_MsaaFramebuffer.colorAttachment =
        context
            .createTexture({width, height, TextureFormat::RGBA16F, 1, samples})
            .value();

    m_MsaaFramebuffer.handle =
        context
            .createFramebuffer(
                {std::array{
                     AttachmentDesc{m_MsaaFramebuffer.colorAttachment.value()}},
                 AttachmentDesc{m_MsaaFramebuffer.rbAttachment.value()}})
            .value();
}

void SYN::gfx::gl::Renderer::updateHdrFramebuffer(Context &context) {
    if (!m_HdrFramebuffer.update)
        return;

    m_HdrFramebuffer.update = false;

    if (m_HdrFramebuffer.colorAttachment) {
        context.deleteTexture(m_HdrFramebuffer.colorAttachment.value());
    }
    if (m_HdrFramebuffer.handle) {
        context.deleteFramebuffer(m_HdrFramebuffer.handle.value());
    }

    auto [width, height] = getRenderResolution();

    m_HdrFramebuffer.colorAttachment =
        context.createTexture({width, height, TextureFormat::RGBA16F}).value();

    m_HdrFramebuffer.handle =
        context
            .createFramebuffer({
                std::array{
                    AttachmentDesc{m_HdrFramebuffer.colorAttachment.value()}},
            })
            .value();
}

void SYN::gfx::gl::Renderer::createScreenQuad(Context &context) {
    if (m_ScreenQuad.has_value()) {
        return;
    }

    float vertices[] = {-1.0, 1.0,  0.0, 1.0, 1.0, 1.0,  1.0, 1.0,
                        -1.0, -1.0, 0.0, 0.0, 1.0, -1.0, 1.0, 0.0};

    uint16_t indices[] = {0, 1, 2, 2, 3, 1};

    Handle<Buffer> vbo =
        context
            .createBuffer(
                {BufferType::Vertex, MemoryUsage::CpuToGPU, sizeof(vertices)},
                vertices)
            .value();

    Handle<Buffer> ebo =
        context
            .createBuffer(
                {BufferType::Index, MemoryUsage::CpuToGPU, sizeof(indices)},
                indices)
            .value();

    m_ScreenQuad =
        context
            .createVertexArray(
                {vbo,
                 sizeof(float) * 4,
                 ebo,
                 {VertexAttribDesc{0, VertexFormat::Float2, 0},
                  VertexAttribDesc{1, VertexFormat::Float2, sizeof(float) * 2}},
                 2,
                 IndexType::Unsigned16})
            .value();

    m_HdrFramebuffer.colorSampler = context.createSampler({}).value();
}

void SYN::gfx::gl::Renderer::createHdrShader(Context &context) {}

void SYN::gfx::gl::Renderer::createSkybox(Context &context) {
    if (m_SkyboxCube.has_value())
        return;

    // https://learnopengl.com/code_viewer.php?code=advanced/cubemaps_skybox_data
    float skyboxVertices[] = {
        -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
        1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f,

        -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f,
        -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,

        1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f,

        -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,

        -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,

        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f,
        1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f};

    Handle<Buffer> m_VertexBuffer =
        context
            .createBuffer({BufferType::Vertex, MemoryUsage::CpuToGPU,
                           sizeof(skyboxVertices)},
                          skyboxVertices)
            .value();

    m_SkyboxCube =
        context
            .createVertexArray({m_VertexBuffer,
                                sizeof(float) * 3,
                                {},
                                {VertexAttribDesc{0, VertexFormat::Float3, 0}},
                                1})
            .value();

    if (m_CubemapSampler.has_value())
        return;

    m_CubemapSampler = context
                           .createSampler({
                               SampleFilter::Linear,
                               SampleFilter::Linear,
                               WrapMode::ClampToEdge,
                               WrapMode::ClampToEdge,
                               WrapMode::ClampToEdge,
                           })
                           .value();

    m_MipmapCubeSampler = context
                              .createSampler({
                                  SampleFilter::Linear_Mipmap_Linear,
                                  SampleFilter::Linear,
                                  WrapMode::ClampToEdge,
                                  WrapMode::ClampToEdge,
                                  WrapMode::ClampToEdge,
                              })
                              .value();
}

SYN::gfx::gl::Handle<SYN::gfx::gl::Texture>
SYN::gfx::gl::Renderer::createBRDFLut(Context &context) {
    Handle<Texture> brdfLUT =
        context.createTexture({512, 512, TextureFormat::RG16F}).value();

    Handle<Framebuffer> captureFb =
        context.createFramebuffer({std::array{AttachmentDesc{brdfLUT}}})
            .value();

    PipelineState pipeline;
    pipeline.shader = m_ShaderCache.getShaderHandle(context, "brdf_lut", 0);

    Pass pass = context.beginPass({captureFb, glm::vec4(1.0f), false, false,
                                   false, Viewport{0, 0, 512, 512}});
    pass.usePipeline(pipeline);
    pass.bindVertexArray(m_ScreenQuad.value());
    pass.drawIndexed(6);

    context.deleteFramebuffer(captureFb);

    return brdfLUT;
}

void SYN::gfx::gl::Renderer::drawRenderItems(Context &context,
                                             const RenderTechnique &technique) {
    TracyGpuZone("DrawRenderItems");
    ZoneScopedN("DrawRenderItems");
    Pass pass = context.beginPass(technique.getPassDesc());
    const std::vector<RenderTechnique::GroupDesc> &groups =
        technique.getGroups();

    for (const RenderTechnique::GroupDesc &groupDesc : groups) {
        uint64_t maskKey =
            (uint64_t)(groupDesc.queryMask) << 32 | groupDesc.exclusionMask;

        auto groupIt = m_GroupCache.find(maskKey);
        if (groupIt == m_GroupCache.cend()) {
            auto [it, _] = m_GroupCache.emplace(
                maskKey, getRenderItemsByShader(context, groupDesc.queryMask,
                                                groupDesc.exclusionMask));
            groupIt = it;
        }

        const std::vector<RenderItem> *items = &groupIt->second;
        if (items->empty())
            continue;

        if (groupDesc.cull || groupDesc.sort) {
            auto groupStateIt = m_GroupStateCache.find(groupDesc);
            if (groupStateIt == m_GroupStateCache.cend()) {
                std::vector<RenderItem> itemsCopy = *items;
                if (groupDesc.cull)
                    frustumCullRenderItems(itemsCopy);
                if (groupDesc.sort)
                    sortRenderItems(itemsCopy);

                auto [it, _] = m_GroupStateCache.emplace(groupDesc, itemsCopy);
                groupStateIt = it;
            }
            items = &groupStateIt->second;
        }
        if (items->empty())
            continue;

        PipelineState pipeline = groupDesc.setupGroup();

        std::optional<uint32_t> lastShaderMask = std::nullopt;
        const std::string &shaderName = technique.getShaderName();
        uint32_t defaultMask = technique.getDefaultShaderMask();

        if (!groupDesc.sort) {
            pipeline.shader = m_ShaderCache.getShaderHandle(
                context, shaderName, groupDesc.queryMask | defaultMask);
            pass.usePipeline(pipeline);
            for (const RenderItem &item : *items) {
                technique.bindUniforms(pass, groupDesc, item);
                if (groupDesc.draw.has_value()) {
                    const auto &drawFunc = groupDesc.draw.value();
                    drawFunc(pass, item);
                    continue;
                }
                pass.bindVertexArray(item.vao);
                pass.drawIndexed(item.indexCount);
            }
        } else {
            for (const RenderItem &item : *items) {
                if (!lastShaderMask.has_value() ||
                    item.shaderIndex != lastShaderMask.value()) {
                    pipeline.shader = m_ShaderCache.getShaderHandle(
                        context, shaderName, item.shaderIndex | defaultMask);
                    pass.usePipeline(pipeline);
                    lastShaderMask = item.shaderIndex;
                }
                technique.bindUniforms(pass, groupDesc, item);
                if (groupDesc.draw.has_value()) {
                    const auto &drawFunc = groupDesc.draw.value();
                    drawFunc(pass, item);
                    continue;
                }
                pass.bindVertexArray(item.vao);
                pass.drawIndexed(item.indexCount);
            }
        }
    }
}

void SYN::gfx::gl::Renderer::setRenderScale(float renderScale) {
    m_RenderConfig.renderScale = glm::max(0.01f, renderScale);
    m_MsaaFramebuffer.update = true;
    m_HdrFramebuffer.update = true;
}

void SYN::gfx::gl::Renderer::endFrame(Context &context) {
    Viewport renderViewport = m_ScreenViewport;
    auto [renderWidth, renderHeight] = getRenderResolution();
    renderViewport.width = renderWidth;
    renderViewport.height = renderHeight;

    glm::mat4 viewMatrix = glm::lookAtRH(m_MainCamera.position,
                                         m_MainCamera.target, m_MainCamera.up);

    glm::mat4 projMatrix = glm::perspectiveRH_NO(
        glm::radians(m_MainCamera.fovYDegrees), m_MainCamera.aspect,
        m_MainCamera.nearPlane, m_MainCamera.farPlane);

    updateMsaaFramebuffer(context);
    updateHdrFramebuffer(context);

    // Update anisotropy of all objects submitted to draw command
    // TODO: Only update the anisotropy and not irrelevant sampler parameters.
    // Only have to do it once per unique model (set of meshes) as multiple draw
    // commands may refer to the same model.
    {
        if (m_AnisotropicUpdate) {
            context.updateSampler(m_DefaultModelSampler,
                                  m_DefaultModelSamplerDesc);
        }

        for (const DrawCommand &cmd : m_DrawCommandList) {
            const Model *model =
                m_ModelRegistry.getResourceImmutableRef(cmd.modelHandle)
                    .value();
            for (const Mesh &mesh : model->meshesOpaque) {
                if (mesh.material.sampler.has_value() &&
                    mesh.material.samplerDesc.has_value() &&
                    m_AnisotropicUpdate) {
                    SamplerDesc samplerDesc = mesh.material.samplerDesc.value();
                    samplerDesc.anisotropicLevel = glm::min(
                        samplerDesc.anisotropicLevel, m_AnisotropicFilter);
                    context.updateSampler(mesh.material.sampler.value(),
                                          samplerDesc);
                }
            }
            for (const Mesh &mesh : model->meshesMasked) {
                if (mesh.material.sampler.has_value() &&
                    mesh.material.samplerDesc.has_value() &&
                    m_AnisotropicUpdate) {
                    SamplerDesc samplerDesc = mesh.material.samplerDesc.value();
                    samplerDesc.anisotropicLevel = glm::min(
                        samplerDesc.anisotropicLevel, m_AnisotropicFilter);
                    context.updateSampler(mesh.material.sampler.value(),
                                          samplerDesc);
                }
            }
        }
        m_AnisotropicUpdate = false;
    }

    // Set per frame constant UBOs
    {
        CameraConstants cameraConstants{};
        cameraConstants.u_viewProjection = projMatrix * viewMatrix;
        cameraConstants.u_view = viewMatrix;
        cameraConstants.u_cameraPos = m_MainCamera.position;

        LightConstants lightConstants{};
        lightConstants.direction = m_DirectionalLight.direction;
        lightConstants.color = m_DirectionalLight.color;
        lightConstants.intensity = m_DirectionalLight.intensity;
        lightConstants.castShadow = m_DirectionalLight.castsShadows;

        context.updateBuffer(m_CameraConstants, 0, sizeof(CameraConstants),
                             &cameraConstants);

        context.updateBuffer(m_LightConstants, 0, sizeof(LightConstants),
                             &lightConstants);
    }

    {
        glm::mat4 view = glm::lookAtRH(m_MainCamera.position,
                                       m_MainCamera.target, m_MainCamera.up);

        glm::mat4 projection = glm::perspectiveRH(
            glm::radians(m_MainCamera.fovYDegrees), m_MainCamera.aspect,
            m_MainCamera.nearPlane, m_MainCamera.farPlane);

        m_CurrentFrustum = Frustum::fromViewProjectionMatrix(view, projection);
    }
    auto environmentIt = m_NameToEnvironment.find(m_CurrentEnvironment);

    if (m_DirectionalLight.castsShadows) {
        drawDirectionalCSM(context, m_DirectionalLight);
    }

    {
        TracyGpuZone("Forward");
        ZoneScopedN("Forward");

        // Main render pass
        {
            m_ZPrepass.setPassDesc({m_MsaaFramebuffer.handle,
                                    environmentIt->second.clearColor, true,
                                    true, false, renderViewport});
            drawRenderItems(context, m_ZPrepass);
            m_ForwardPass.setPassDesc({m_MsaaFramebuffer.handle, std::nullopt,
                                       false, true, false, renderViewport});
            drawRenderItems(context, m_ForwardPass);

            if (environmentIt->second.type != Environment::Type::ClearColor) {
                Pass pass =
                    context.beginPass({m_MsaaFramebuffer.handle, std::nullopt,
                                       false, true, false, renderViewport});
                PipelineState pipeline;
                pipeline.depth.writeEnabled = false;
                pipeline.shader =
                    m_ShaderCache.getShaderHandle(context, "skybox", 0);
                pipeline.depth.test = DepthFunc::LessEqual;
                pass.usePipeline(pipeline);

                pass.bindTexture(0, environmentIt->second.cubemap,
                                 m_CubemapSampler.value());
                pass.bindUniform("u_skybox", 0);
                pass.bindUniform("u_ViewProjection",
                                 projMatrix * glm::mat4(glm::mat3(viewMatrix)));
                pass.bindVertexArray(m_SkyboxCube.value());
                pass.draw(36);
            }

            drawDebugPass(context);
        }

        context.blitFramebuffer(m_MsaaFramebuffer.handle,
                                m_HdrFramebuffer.handle, renderViewport,
                                renderViewport);

        {
            Pass hdrPass = context.beginPass(
                {std::nullopt, environmentIt->second.clearColor, false, false,
                 false, m_ScreenViewport});
            PipelineState hdrPipeline;
            hdrPipeline.shader =
                m_ShaderCache.getShaderHandle(context, "hdr", 0);

            hdrPass.usePipeline(hdrPipeline);

            hdrPass.bindTexture(0, m_HdrFramebuffer.colorAttachment.value(),
                                m_HdrFramebuffer.colorSampler);
            hdrPass.bindUniform("u_hdrBuffer", 0);
            hdrPass.bindUniform("u_gamma", m_Gamma);
            hdrPass.bindUniform("u_exposure", m_Exposure);

            hdrPass.bindVertexArray(m_ScreenQuad.value());
            hdrPass.drawIndexed(6);
        }
    }

    m_DrawCommandList.clear();
    m_GroupCache.clear();
    m_GroupStateCache.clear();
    m_FrameBoneMatrices.clear();

    TracyGpuCollect;
}

void SYN::gfx::gl::Renderer::setGamma(float gamma) {
    m_Gamma = glm::clamp(gamma, MIN_GAMMA, MAX_GAMMA);
}
void SYN::gfx::gl::Renderer::setExposure(float exposure) {
    if (exposure < 0.0f)
        exposure = 0.0f;
    m_Exposure = exposure;
}

void SYN::gfx::gl::Renderer::setBloomEnabled(bool enabled) {}

void SYN::gfx::gl::Renderer::setAntiAliasMode(AntiAliasMode mode) {
    if (m_RenderConfig.aaMode == mode)
        return;

    // MSAA to non-MSAA needs update
    if (m_RenderConfig.aaMode >= AntiAliasMode::MSAA_2x &&
        m_RenderConfig.aaMode <= AntiAliasMode::MSAA_8x) {
        m_MsaaFramebuffer.update = true;
    }

    m_RenderConfig.aaMode = mode;

    // non-MSAA to MSAA needs update
    if (m_RenderConfig.aaMode >= AntiAliasMode::MSAA_2x &&
        m_RenderConfig.aaMode <= AntiAliasMode::MSAA_8x) {
        m_MsaaFramebuffer.update = true;
    }
}

void SYN::gfx::gl::Renderer::resize(int width, int height) {
    // An AND is probably safe but there could be a case
    // where one is 0 while the other is not?
    if (width == 0 || height == 0)
        return;
    if (width == m_ScreenViewport.width && height == m_ScreenViewport.height)
        return;
    m_ScreenViewport.width = width;
    m_ScreenViewport.height = height;
    m_MsaaFramebuffer.update = true;
    m_HdrFramebuffer.update = true;
}

glm::mat4 SYN::gfx::gl::Renderer::calculateTightLightFrustum(
    const DirectionalLight &light, uint32_t resolution, const Camera &camera,
    float &texelWorld) {
    ZoneScopedN("Calculate tight light frustum");

    glm::mat4 viewMatrix =
        glm::lookAtRH(camera.position, camera.target, camera.up);

    glm::mat4 projMatrix =
        glm::perspectiveRH_NO(glm::radians(camera.fovYDegrees), camera.aspect,
                              camera.nearPlane, camera.farPlane);

    std::vector<glm::vec4> corners =
        Frustum::getCornersWorldSpace(viewMatrix, projMatrix);

    glm::vec3 center(0.0f);

    for (const glm::vec4 &p : corners) {
        center += glm::vec3(p);
    }
    center /= corners.size();

    float bound = glm::max(glm::length(corners[0] - corners[7]),
                           glm::length(corners[1] - corners[7]));
    bound = std::ceil(bound * 16.0f) / 16.0f;

    texelWorld = bound / resolution;

    float halfBound = bound * 0.5f;

    glm::vec3 up = glm::abs(glm::dot(light.direction,
                                     glm::vec3(0.0f, 1.0f, 0.0f))) < 0.999f
                       ? glm::vec3(0.0f, 1.0f, 0.0f)
                       : glm::vec3(1.0f, 0.0f, 0.0f);

    glm::mat4 lightViewMatrix =
        glm::lookAtRH(center - light.direction, center, up);

    float minZ = std::numeric_limits<float>().max();
    float maxZ = std::numeric_limits<float>().lowest();

    for (const glm::vec4 &p : corners) {
        glm::vec3 pLight = lightViewMatrix * p;
        minZ = glm::min(minZ, pLight.z);
        maxZ = glm::max(maxZ, pLight.z);
    }

    maxZ += halfBound * 1.5f;
    minZ -= halfBound * 0.5f;

    glm::mat4 lightProjection = glm::orthoRH_NO(
        -halfBound, halfBound, -halfBound, halfBound, -maxZ, -minZ);

    glm::mat4 shadowMatrix = lightProjection * lightViewMatrix;
    glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin *= resolution * 0.5f;
    glm::vec4 rounded = glm::round(shadowOrigin);
    glm::vec4 offset = (rounded - shadowOrigin) * (2.0f / resolution);
    offset.z = 0.0f;
    offset.w = 0.0f;
    lightProjection[3] += offset;

    return lightProjection * lightViewMatrix;
}

void SYN::gfx::gl::Renderer::setCSMDistance(float distance) {
    m_RenderConfig.shadowDistance = glm::max(0.001f, distance);
}

void SYN::gfx::gl::Renderer::drawInstancedCSMDepth(Context &context,
                                                   Handle<Framebuffer> fbo,
                                                   Handle<Texture> depth,
                                                   bool isNear,
                                                   uint32_t resolution) {
    context.setDepthAttachment(fbo, depth);

    m_ShadowPass.setPassDesc({fbo, std::nullopt, true, true, false,
                              Viewport{0, 0, resolution, resolution}});

    m_ShadowPass.setBindUniformBase([&](Pass &pass, const RenderItem &item) {
        pass.bindUniform("u_layerOffset", (1 - (int32_t)isNear) * 2);
        pass.bindUniform("u_modelMatrix", item.transform);
    });

    drawRenderItems(context, m_ShadowPass);
}

void SYN::gfx::gl::Renderer::drawCSMDepth(Context &context,
                                          Handle<Framebuffer> fbo,
                                          Handle<Texture> depth, bool isNear,
                                          uint32_t resolution) {
    for (uint32_t i = 0; i < 2; ++i) {
        context.setDepthAttachment(fbo, depth, 0, i);
        m_ShadowPass.setPassDesc({fbo, std::nullopt, true, true, false,
                                  Viewport{0, 0, resolution, resolution}});

        m_ShadowPass.setBindUniformBase([&](Pass &pass,
                                            const RenderItem &item) {
            pass.bindUniform("u_lightSpaceMatrix",
                             m_CascadedShadowmap
                                 .lightSpaceMatrices[i + ((1.0 - isNear) * 2)]);
            pass.bindUniform("u_modelMatrix", item.transform);
        });

        drawRenderItems(context, m_ShadowPass);
    }
}

void SYN::gfx::gl::Renderer::drawDirectionalCSM(Context &context,
                                                const DirectionalLight &light) {
    TracyGpuZone("CSM Pass");
    ZoneScopedN("CSM Pass");

    Camera camera = m_MainCamera;

    std::vector<float> splits(5);
    splits[0] = camera.nearPlane;
    float shadowDist = m_RenderConfig.shadowDistance;
    splits[4] = shadowDist;
    uint32_t numMaps = 4;
    for (uint32_t i = 1; i < numMaps; ++i) {
        float t = i / (float)numMaps;
        float logC =
            camera.nearPlane * glm::pow(shadowDist / camera.nearPlane, t);
        float uniformC = camera.nearPlane + (shadowDist - camera.nearPlane) * t;
        splits[i] = glm::mix(uniformC, logC, 0.5f);
    }

    for (uint32_t i = 0; i < numMaps; ++i) {
        camera.nearPlane = splits[i];
        camera.farPlane = splits[i + 1];
        uint32_t resolution = i < 2 ? m_RenderConfig.shadowMapNearResolution
                                    : m_RenderConfig.shadowMapFarResolution;
        m_CascadedShadowmap.lightSpaceMatrices[i] = calculateTightLightFrustum(
            light, resolution, camera,
            m_CascadedShadowmap.cascadeTexelWorldSize[i]);
        m_CascadedShadowmap.planeDistances[i] = splits[i + 1];
    }

    ShadowConstants shadowConstants{};

    std::copy(m_CascadedShadowmap.lightSpaceMatrices.cbegin(),
              m_CascadedShadowmap.lightSpaceMatrices.cend(),
              &shadowConstants.u_lightSpaceMatrices[0]);
    std::copy(m_CascadedShadowmap.planeDistances.cbegin(),
              m_CascadedShadowmap.planeDistances.cend(),
              &shadowConstants.u_cascadePlaneDistances[0]);
    std::copy(m_CascadedShadowmap.cascadeTexelWorldSize.cbegin(),
              m_CascadedShadowmap.cascadeTexelWorldSize.cend(),
              &shadowConstants.u_cascadeTexelWorldSize[0]);
    context.updateBuffer(m_ShadowConstants, 0, sizeof(ShadowConstants),
                         &shadowConstants);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_CLAMP);
    glPolygonOffset(2.0f, 2.0f);

    if (m_CascadedShadowmap.isInstanced) {
        drawInstancedCSMDepth(context, m_CascadedShadowmap.fboNear,
                              m_CascadedShadowmap.depthTextureNear, true,
                              m_RenderConfig.shadowMapNearResolution);
        drawInstancedCSMDepth(context, m_CascadedShadowmap.fboFar,
                              m_CascadedShadowmap.depthTextureFar, false,
                              m_RenderConfig.shadowMapFarResolution);
    } else {
        drawCSMDepth(context, m_CascadedShadowmap.fboNear,
                     m_CascadedShadowmap.depthTextureNear, true,
                     m_RenderConfig.shadowMapNearResolution);
        drawCSMDepth(context, m_CascadedShadowmap.fboFar,
                     m_CascadedShadowmap.depthTextureFar, false,
                     m_RenderConfig.shadowMapFarResolution);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_CLAMP);
}

std::vector<uint32_t> SYN::gfx::gl::Renderer::getCSMTextures(Context &context) {
    uint32_t id =
        context.getTextureId(m_CascadedShadowmap.depthTextureNear).value();

    std::vector<uint32_t> csmLayers;
    float colors[] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t newId = 0;
        glGenTextures(1, &newId);
        glTextureView(newId, GL_TEXTURE_2D, id, GL_DEPTH_COMPONENT16, 0, 1, i,
                      1);
        glTextureParameteri(newId, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTextureParameteri(newId, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTextureParameteri(newId, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTextureParameteri(newId, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(newId, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(newId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(newId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTextureParameterfv(newId, GL_TEXTURE_BORDER_COLOR, &colors[0]);
        csmLayers.push_back(newId);
    }

    id = context.getTextureId(m_CascadedShadowmap.depthTextureFar).value();
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t newId = 0;
        glGenTextures(1, &newId);
        glTextureView(newId, GL_TEXTURE_2D, id, GL_DEPTH_COMPONENT16, 0, 1, i,
                      1);
        glTextureParameteri(newId, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTextureParameteri(newId, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTextureParameteri(newId, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTextureParameteri(newId, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(newId, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(newId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(newId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTextureParameterfv(newId, GL_TEXTURE_BORDER_COLOR, &colors[0]);
        csmLayers.push_back(newId);
    }

    return csmLayers;
}

void SYN::gfx::gl::Renderer::setAnisotropicFiltering(float filter) {
    m_AnisotropicFilter = glm::clamp(filter, 1.0f, 16.0f);
    m_DefaultModelSamplerDesc.anisotropicLevel = m_AnisotropicFilter;
    m_AnisotropicUpdate = true;
}

void SYN::gfx::gl::Renderer::reloadInternalShaders(Context &context) {
    m_ShaderCache.reset(context);
}

std::vector<SYN::gfx::gl::RenderItem>
SYN::gfx::gl::Renderer::getRenderItemsByShader(Context &context,
                                               uint32_t shaderIndex,
                                               uint32_t exclusionMask) {
    ZoneScopedN("GetRenderItemsByShader");
    std::vector<RenderItem> renderItems;

    auto getMaterialOverride =
        [&](const DrawCommand &cmd,
            uint32_t sourceIndex) -> std::optional<Material> {
        for (const MaterialOverride &override : cmd.materialOverride) {
            if (sourceIndex != override.meshIndex)
                continue;

            const MaterialData *materialData =
                m_AssetManager->get<MaterialData>(override.material);

            return loadMaterial(*m_Context, *materialData);
        }
        return std::nullopt;
    };

    auto processMeshes = [&](const DrawCommand &cmd, const Model *model,
                             const std::vector<Mesh> &meshes) {
        for (const Mesh &mesh : meshes) {
            Material material = getMaterialOverride(cmd, mesh.sourceIndex)
                                    .value_or(mesh.material);

            uint32_t shaderFeatures = getShaderFeatures(mesh, material);

            if (!cmd.boneOffset.has_value()) {
                shaderFeatures &= ~(uint32_t)(ShaderFeature::Skinned);
            }

            if ((shaderFeatures & shaderIndex) != shaderIndex)
                continue;

            if ((shaderFeatures & exclusionMask) == exclusionMask &&
                (exclusionMask != 0))
                continue;

            glm::mat4 worldTransform = cmd.transform * mesh.localTransform;

            renderItems.push_back({worldTransform, material, mesh.vao,
                                   mesh.indexCount, shaderFeatures,
                                   cmd.meshBounds.at(mesh.sourceIndex),
                                   cmd.boneOffset});
        }
    };

    for (const DrawCommand &cmd : m_DrawCommandList) {
        const Model *model =
            m_ModelRegistry.getResourceImmutableRef(cmd.modelHandle).value();
        const std::vector<Mesh> &meshes =
            shaderIndex & (uint32_t)ShaderFeature::AlphaTest
                ? model->meshesMasked
                : model->meshesOpaque;
        processMeshes(cmd, model, meshes);
    }

    return renderItems;
}

void SYN::gfx::gl::Renderer::frustumCullRenderItems(
    std::vector<RenderItem> &items) {
    ZoneScopedN("Frustum Cull");
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const RenderItem &item) {
                                   return !m_CurrentFrustum.collidesWithAABB(
                                       item.aabb);
                               }),
                items.end());
}

void SYN::gfx::gl::Renderer::sortRenderItems(std::vector<RenderItem> &items) {
    ZoneScopedN("Sort items");
    std::sort(items.begin(), items.end(),
              [&](const RenderItem &a, const RenderItem &b) {
                  return a.shaderIndex < b.shaderIndex;
              });
}

void SYN::gfx::gl::Renderer::bindBoneMatrices(Pass &pass, uint32_t offset) {
    TracyGpuZone("BindBoneMatrices");
    ZoneScopedN("BindBoneMatrices");
    pass.bindUniform("boneTransforms[0]", m_FrameBoneMatrices, offset,
                     MAX_BONES);
}

std::optional<SYN::gfx::gl::Handle<SYN::gfx::gl::Texture>>
SYN::gfx::gl::Renderer::loadTexture(Context &context, const AssetRef &texture,
                                    bool srgb) {
    if (!texture.valid())
        return std::nullopt;

    if (auto it = m_UUIDToHandle.find(texture.uuid());
        it != m_UUIDToHandle.cend()) {
        return std::get<Handle<Texture>>(m_UUIDToHandle.at(it->first).handle);
    }

    const TextureData *textureData =
        m_AssetManager->get<TextureData>(texture.uuid());

    auto getMipLevel = [](int width, int height) {
        return 1 +
               static_cast<int>(std::floor(std::log2(std::max(width, height))));
    };

    auto textureFormatFromChannelCount = [](int channels, bool srgb = false) {
        switch (channels) {
        case 1:
            return SYN::gfx::gl::TextureFormat::R8;
        case 2:
            return SYN::gfx::gl::TextureFormat::RG8;
        case 3:
            return srgb ? SYN::gfx::gl::TextureFormat::SRGB
                        : SYN::gfx::gl::TextureFormat::RGB8;
        case 4:
        default:
            return srgb ? SYN::gfx::gl::TextureFormat::SRGBA
                        : SYN::gfx::gl::TextureFormat::RGBA8;
        }
    };

    auto dataToDesc = [&](const TextureData *data, bool srgb) -> TextureDesc {
        TextureDesc desc{};
        desc.width = data->width;
        desc.height = data->height;
        desc.format = textureFormatFromChannelCount(data->channelCount, srgb);
        desc.mipLevel = getMipLevel(data->width, data->height);

        return desc;
    };

    TextureDesc desc = dataToDesc(textureData, srgb);

    Handle<Texture> textureHandle =
        context.createTexture(desc, &textureData->data[0]).value();
    m_UUIDToHandle[texture.uuid()] = {textureHandle};

    return textureHandle;
}

SYN::gfx::gl::Material
SYN::gfx::gl::Renderer::loadMaterial(Context &context,
                                     const MaterialData &materialData) {

    SYN::gfx::gl::Material material;

    material.albedo = loadTexture(context, materialData.albedoData, true)
                          .value_or(m_DefaultWhite);
    material.normalMap = loadTexture(context, materialData.normalData, false);
    material.metallicRoughnessMap =
        loadTexture(context, materialData.metallicRoughnessData, false);

    material.metallic = materialData.metallic;
    material.roughness = materialData.roughness;
    material.tint = materialData.tint;
    material.alphaCutoff = materialData.alphaCutoff;

    return material;
}

void SYN::gfx::gl::Renderer::drawDebugPass(Context &context) {
    TracyGpuZone("Debug Draw Pass");
    ZoneScopedN("Debug Draw Pass");

    Viewport renderViewport = m_ScreenViewport;
    auto [renderWidth, renderHeight] = getRenderResolution();
    renderViewport.width = renderWidth;
    renderViewport.height = renderHeight;

    const std::vector<DebugDraw::Line> depthLines = m_DebugDrawData.depthLines;
    const std::vector<DebugDraw::Line> overlayLines =
        m_DebugDrawData.overlayLines;

    PipelineState linePipeline;
    linePipeline.shader = m_ShaderCache.getShaderHandle(context, "lines", 0);
    linePipeline.topology = PrimitiveTopology::TriangleStrip;
    linePipeline.depth.writeEnabled = false;

    if (depthLines.size() > 0) {
        context.updateBufferAndResize(
            m_DebugDrawData.lineDepthBuffer,
            sizeof(DebugDraw::Line) * depthLines.size(), &depthLines[0]);

        context.updateVertexArrayVertexBuffer(m_DebugDrawData.lineVAO,
                                              m_DebugDrawData.lineDepthBuffer,
                                              1, 0, sizeof(DebugDraw::Line));

        Pass depthLinePass =
            context.beginPass({m_MsaaFramebuffer.handle, std::nullopt, false,
                               true, false, renderViewport, std::nullopt});

        depthLinePass.usePipeline(linePipeline);
        depthLinePass.bindUniform("u_resolution", (float)renderViewport.width,
                                  (float)renderViewport.height);
        depthLinePass.bindVertexArray(m_DebugDrawData.lineVAO);
        depthLinePass.drawInstanced(4, depthLines.size());
    }

    if (overlayLines.size() > 0) {
        context.updateBufferAndResize(
            m_DebugDrawData.lineOverlayBuffer,
            sizeof(DebugDraw::Line) * overlayLines.size(), &overlayLines[0]);

        context.updateVertexArrayVertexBuffer(m_DebugDrawData.lineVAO,
                                              m_DebugDrawData.lineOverlayBuffer,
                                              1, 0, sizeof(DebugDraw::Line));

        Pass overlayLinePass =
            context.beginPass({m_MsaaFramebuffer.handle, std::nullopt, false,
                               false, false, renderViewport, std::nullopt});

        overlayLinePass.usePipeline(linePipeline);
        overlayLinePass.bindUniform("u_resolution", (float)renderViewport.width,
                                    (float)renderViewport.height);
        overlayLinePass.bindVertexArray(m_DebugDrawData.lineVAO);
        overlayLinePass.drawInstanced(4, overlayLines.size());
    }

    m_DebugDrawData.depthLines.clear();
    m_DebugDrawData.overlayLines.clear();
}
