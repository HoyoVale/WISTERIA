#include "wisteria/common/pch.hpp"

#include "open_gl_render_device.hpp"

#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wisteria
{
namespace
{
GLenum MapBufferTarget(BufferUsage usage) noexcept
{
    switch (usage)
    {
    case BufferUsage::Index:
        return GL_ELEMENT_ARRAY_BUFFER;
    case BufferUsage::Uniform:
        return GL_UNIFORM_BUFFER;
    case BufferUsage::Vertex:
        break;
    }
    return GL_ARRAY_BUFFER;
}

void InternalTextureFormat(
    TextureFormat format,
    GLenum& internalFormat,
    GLenum& externalFormat,
    GLenum& externalType
) noexcept
{
    switch (format)
    {
    case TextureFormat::Rgba8:
        internalFormat = GL_RGBA8;
        externalFormat = GL_RGBA;
        externalType = GL_UNSIGNED_BYTE;
        return;
    case TextureFormat::Rgba8Srgb:
        internalFormat = GL_SRGB8_ALPHA8;
        externalFormat = GL_RGBA;
        externalType = GL_UNSIGNED_BYTE;
        return;
    case TextureFormat::Depth24Stencil8:
        internalFormat = GL_DEPTH24_STENCIL8;
        externalFormat = GL_DEPTH_STENCIL;
        externalType = GL_UNSIGNED_INT_24_8;
        return;
    }
}

GLenum MapMinFilter(TextureFilter filter, bool generateMipmaps) noexcept
{
    if (filter == TextureFilter::Nearest)
        return generateMipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
    return generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
}

GLenum MapWrap(TextureWrap wrap) noexcept
{
    return wrap == TextureWrap::ClampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
}

}  // namespace

OpenGlRenderDevice::~OpenGlRenderDevice() = default;

RenderBackendId OpenGlRenderDevice::BackendId() const noexcept
{
    return RenderBackendId::OpenGL;
}

std::string_view OpenGlRenderDevice::BackendName() const noexcept
{
    return "OpenGL";
}

const RenderDeviceCapabilities& OpenGlRenderDevice::Capabilities() const noexcept
{
    return this->capabilities;
}

void OpenGlRenderDevice::RefreshCapabilities() noexcept
{
    GLint vertexTextureUnits = 0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertexTextureUnits);
    this->capabilities.maxSkinningMatrices =
        vertexTextureUnits > 0 ? static_cast<std::size_t>(vertexTextureUnits)
                               : 0U;
    this->capabilities.independentBlend =
        GLAD_GL_ARB_draw_buffers_blend != 0 &&
        glad_glBlendFunciARB != nullptr;
    this->capabilitiesValid = true;
}

BufferHandle OpenGlRenderDevice::CreateBuffer(const BufferDesc& desc)
{
    if (desc.size == 0U)
        throw std::invalid_argument("buffer size must be positive");
    GLuint buffer = 0U;
    glGenBuffers(1, &buffer);
    if (buffer == 0U)
        throw std::runtime_error("OpenGL buffer allocation failed");
    glBindBuffer(MapBufferTarget(desc.usage), buffer);
    glBufferData(
        MapBufferTarget(desc.usage),
        static_cast<GLsizeiptr>(desc.size),
        nullptr,
        GL_STATIC_DRAW
    );
    glBindBuffer(MapBufferTarget(desc.usage), 0U);
    const std::uint64_t id = this->AllocateId();
    this->resources.emplace(
        id,
        ResourceEntry{ResourceKind::Buffer, buffer}
    );
    return RenderDevice::MakeBufferHandle(id);
}

TextureHandle OpenGlRenderDevice::CreateTexture(const TextureDesc& desc)
{
    if (desc.width == 0U || desc.height == 0U)
        throw std::invalid_argument("texture dimensions must be positive");
    GLenum internalFormat = GL_RGBA8;
    GLenum externalFormat = GL_RGBA;
    GLenum externalType = GL_UNSIGNED_BYTE;
    InternalTextureFormat(desc.format, internalFormat, externalFormat, externalType);
    GLuint texture = 0U;
    glGenTextures(1, &texture);
    if (texture == 0U)
        throw std::runtime_error("OpenGL texture allocation failed");
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(internalFormat),
        static_cast<GLsizei>(desc.width),
        static_cast<GLsizei>(desc.height),
        0,
        externalFormat,
        externalType,
        nullptr
    );
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        static_cast<GLint>(MapMinFilter(TextureFilter::Linear, desc.generateMipmaps))
    );
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        static_cast<GLint>(MapMinFilter(TextureFilter::Linear, false))
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_REPEAT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_REPEAT));
    if (desc.generateMipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0U);
    const std::uint64_t id = this->AllocateId();
    this->resources.emplace(
        id,
        ResourceEntry{ResourceKind::Texture, texture}
    );
    return RenderDevice::MakeTextureHandle(id);
}

SamplerHandle OpenGlRenderDevice::CreateSampler(const SamplerDesc& desc)
{
    GLuint sampler = 0U;
    glGenSamplers(1, &sampler);
    if (sampler == 0U)
        throw std::runtime_error("OpenGL sampler allocation failed");
    glSamplerParameteri(
        sampler,
        GL_TEXTURE_MIN_FILTER,
        static_cast<GLint>(MapMinFilter(desc.minFilter, false))
    );
    glSamplerParameteri(
        sampler,
        GL_TEXTURE_MAG_FILTER,
        static_cast<GLint>(MapMinFilter(desc.magFilter, false))
    );
    glSamplerParameteri(
        sampler,
        GL_TEXTURE_WRAP_S,
        static_cast<GLint>(MapWrap(desc.wrapS))
    );
    glSamplerParameteri(
        sampler,
        GL_TEXTURE_WRAP_T,
        static_cast<GLint>(MapWrap(desc.wrapT))
    );
    const std::uint64_t id = this->AllocateId();
    this->resources.emplace(
        id,
        ResourceEntry{ResourceKind::Sampler, sampler}
    );
    return RenderDevice::MakeSamplerHandle(id);
}

PipelineHandle OpenGlRenderDevice::CreateGraphicsPipeline(
    const GraphicsPipelineDesc& desc
)
{
    const GLuint program = glCreateProgram();
    if (program == 0U)
        throw std::runtime_error("OpenGL program allocation failed");
    std::vector<GLuint> shaders;
    try
    {
        for (const ShaderStageDesc& stage : desc.stages)
        {
            const GLenum type =
                stage.stage == ShaderStage::Vertex
                    ? GL_VERTEX_SHADER
                    : GL_FRAGMENT_SHADER;
            const GLuint shader = glCreateShader(type);
            if (shader == 0U)
                throw std::runtime_error("OpenGL shader allocation failed");
            const char* sourceData = stage.source.data();
            const GLint sourceLength = static_cast<GLint>(stage.source.size());
            glShaderSource(shader, 1, &sourceData, &sourceLength);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled != GL_TRUE)
            {
                GLint logLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
                std::string log(
                    static_cast<std::size_t>(logLength > 0 ? logLength : 1),
                    '\0'
                );
                glGetShaderInfoLog(shader, logLength, nullptr, log.data());
                glDeleteShader(shader);
                throw std::runtime_error(
                    "pipeline shader compile failed: " + log
                );
            }
            glAttachShader(program, shader);
            shaders.push_back(shader);
        }
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(
                static_cast<std::size_t>(logLength > 0 ? logLength : 1),
                '\0'
            );
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            throw std::runtime_error("pipeline link failed: " + log);
        }
    }
    catch (...)
    {
        for (const GLuint shader : shaders)
            glDeleteShader(shader);
        glDeleteProgram(program);
        throw;
    }
    for (const GLuint shader : shaders)
        glDeleteShader(shader);

    const std::uint64_t id = this->AllocateId();
    this->resources.emplace(
        id,
        ResourceEntry{ResourceKind::Pipeline, program}
    );
    return RenderDevice::MakePipelineHandle(id);
}

void OpenGlRenderDevice::UpdateBuffer(
    BufferHandle handle,
    const void* data,
    std::size_t size,
    std::size_t offset
)
{
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Buffer)
    {
        throw std::invalid_argument(
            "buffer handle does not belong to this RenderDevice"
        );
    }
    if (data == nullptr && size != 0U)
        throw std::invalid_argument("buffer update data must not be null");
    glBindBuffer(GL_ARRAY_BUFFER, entry->object);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        static_cast<GLintptr>(offset),
        static_cast<GLsizeiptr>(size),
        data
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0U);
}

void OpenGlRenderDevice::DestroyBuffer(BufferHandle handle) noexcept
{
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Buffer)
    {
        assert(!"wrong-device or unknown buffer handle");
        return;
    }
    this->graphicsDevice.DeleteResource(
        GraphicsDevice::ResourceKind::Buffer,
        entry->object
    );
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::DestroyTexture(TextureHandle handle) noexcept
{
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Texture)
    {
        assert(!"wrong-device or unknown texture handle");
        return;
    }
    this->graphicsDevice.DeleteResource(
        GraphicsDevice::ResourceKind::Texture,
        entry->object
    );
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::DestroySampler(SamplerHandle handle) noexcept
{
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Sampler)
    {
        assert(!"wrong-device or unknown sampler handle");
        return;
    }
    // 0B: sampler objects are share-group shared; the composition root
    // guarantees a current context at teardown. 0C unifies deletion through
    // the device resource cache.
    const GLuint sampler = entry->object;
    glDeleteSamplers(1, &sampler);
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::DestroyGraphicsPipeline(
    PipelineHandle handle
) noexcept
{
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Pipeline)
    {
        assert(!"wrong-device or unknown pipeline handle");
        return;
    }
    const GLuint program = entry->object;
    glDeleteProgram(program);
    this->Erase(RenderDevice::HandleId(handle));
}

GraphicsDevice& OpenGlRenderDevice::LegacyGraphicsDevice() noexcept
{
    return this->graphicsDevice;
}

const GraphicsDevice& OpenGlRenderDevice::LegacyGraphicsDevice() const noexcept
{
    return this->graphicsDevice;
}

GraphicsDevice* OpenGlRenderDevice::GraphicsDeviceFrom(
    RenderDevice* renderDevice
) noexcept
{
    if (renderDevice == nullptr)
        return nullptr;
    auto* openGl = dynamic_cast<OpenGlRenderDevice*>(renderDevice);
    return openGl != nullptr ? &openGl->graphicsDevice : nullptr;
}

const OpenGlRenderDevice::ResourceEntry* OpenGlRenderDevice::Find(
    std::uint64_t id
) const
{
    const auto iterator = this->resources.find(id);
    return iterator == this->resources.end() ? nullptr : &iterator->second;
}

OpenGlRenderDevice::ResourceEntry* OpenGlRenderDevice::Find(std::uint64_t id)
{
    const auto iterator = this->resources.find(id);
    return iterator == this->resources.end() ? nullptr : &iterator->second;
}

std::uint64_t OpenGlRenderDevice::AllocateId() noexcept
{
    const std::uint64_t id = this->nextHandle;
    ++this->nextHandle;
    return id;
}

void OpenGlRenderDevice::Erase(std::uint64_t id) noexcept
{
    this->resources.erase(id);
}
}  // namespace wisteria
