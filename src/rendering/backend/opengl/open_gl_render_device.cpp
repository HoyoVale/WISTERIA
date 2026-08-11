#include "wisteria/common/pch.hpp"

#include "open_gl_render_device.hpp"

#include "rendering/renderer_internal.hpp"
#include "wisteria/rendering/render_graph.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace wisteria
{
namespace
{
// OpenGL realization of the backend-created presentation endpoint. It owns
// the present->swap sequence; the actual blit and swap operations are the
// composition root's OpenGL present implementation (Renderer::Present and
// Window::SwapBuffers), supplied as neutral callbacks.
class OpenGlPresentationTarget final : public PresentationTarget
{
public:
    OpenGlPresentationTarget(
        const PresentSurface& surface,
        PresentBlitFunction blit,
        PresentSwapFunction swap
    )
        : surface(surface),
          blit(std::move(blit)),
          swap(std::move(swap))
    {
    }

    void Present(const RenderTarget& source) override
    {
        if (!this->blit)
        {
            throw std::logic_error(
                "OpenGL presentation target has no blit wired"
            );
        }
        this->blit(source, this->surface.Width(), this->surface.Height());
    }

    void Swap() override
    {
        if (!this->swap)
        {
            throw std::logic_error(
                "OpenGL presentation target has no swap wired"
            );
        }
        this->swap();
    }

private:
    const PresentSurface& surface;
    PresentBlitFunction blit;
    PresentSwapFunction swap;
};

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

OpenGlRenderDevice::OpenGlRenderDevice()
    : renderCache(this->graphicsDevice)
{
}

RenderBackendId OpenGlRenderDevice::BackendId() const noexcept
{
    return RenderBackendId::OpenGL;
}

std::string_view OpenGlRenderDevice::BackendName() const noexcept
{
    return "OpenGL";
}

const RenderDeviceCapabilities& OpenGlRenderDevice::Capabilities() const
{
    if (!this->capabilitiesValid)
    {
        throw std::logic_error(
            "RenderDevice capabilities are not initialized; "
            "refresh with the owning share-group context current"
        );
    }
    return this->capabilities;
}

void OpenGlRenderDevice::RefreshCapabilities()
{
    // R2.0 0B Final Fix: capabilities may only be queried while a context of
    // this device's owning share group is current. WindowManager restores
    // the previous context after creating a window, so composition roots
    // must refresh inside a real current-context transaction.
    const GraphicsShareGroupToken owning = this->graphicsDevice.ShareGroupToken();
    if (owning == nullptr ||
        GraphicsDevice::CurrentShareGroup() != owning)
    {
        throw std::logic_error(
            "RenderDevice capabilities require the owning GL share-group "
            "context to be current"
        );
    }

    // Engine semantic: maxSkinningMatrices is the matrix-palette capacity
    // actually addressable by the current pipeline, not a raw GL constant.
    // R1 skinning (render_skinning.cpp) requires:
    //   vertex texture fetch available
    //   combined texture units leave room for the skinning unit
    //   texture-buffer capacity holds at least one mat4 (4 vec4 texels)
    // Capacity = GL_MAX_TEXTURE_BUFFER_SIZE / 4 (one mat4 = 4 texels).
    GLint vertexTextureUnits = 0;
    GLint combinedTextureUnits = 0;
    GLint maximumTexels = 0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertexTextureUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combinedTextureUnits);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximumTexels);
    if (vertexTextureUnits < 1 ||
        combinedTextureUnits <= static_cast<GLint>(SkinningTextureUnit) ||
        maximumTexels < 4)
    {
        this->capabilities.maxSkinningMatrices = 0U;
    }
    else
    {
        this->capabilities.maxSkinningMatrices =
            static_cast<std::size_t>(maximumTexels) / 4U;
    }
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
    try
    {
        this->resources.emplace(
            id,
            ResourceEntry{
                ResourceKind::Buffer,
                buffer,
                desc.size,
                desc.usage
            }
        );
    }
    catch (...)
    {
        glDeleteBuffers(1, &buffer);
        throw;
    }
    return RenderDevice::MakeBufferHandle(this->DeviceUid(), id);
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
    try
    {
        this->resources.emplace(
            id,
            ResourceEntry{ResourceKind::Texture, texture}
        );
    }
    catch (...)
    {
        glDeleteTextures(1, &texture);
        throw;
    }
    return RenderDevice::MakeTextureHandle(this->DeviceUid(), id);
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
    try
    {
        this->resources.emplace(
            id,
            ResourceEntry{ResourceKind::Sampler, sampler}
        );
    }
    catch (...)
    {
        glDeleteSamplers(1, &sampler);
        throw;
    }
    return RenderDevice::MakeSamplerHandle(this->DeviceUid(), id);
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
    try
    {
        this->resources.emplace(
            id,
            ResourceEntry{ResourceKind::Pipeline, program}
        );
    }
    catch (...)
    {
        glDeleteProgram(program);
        throw;
    }
    return RenderDevice::MakePipelineHandle(this->DeviceUid(), id);
}

void OpenGlRenderDevice::UpdateBuffer(
    BufferHandle handle,
    const void* data,
    std::size_t size,
    std::size_t offset
)
{
    if (RenderDevice::HandleDevice(handle) != this->DeviceUid())
    {
        throw std::invalid_argument(
            "buffer handle does not belong to this RenderDevice"
        );
    }
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Buffer)
    {
        throw std::invalid_argument(
            "buffer handle does not belong to this RenderDevice"
        );
    }
    if (data == nullptr && size != 0U)
        throw std::invalid_argument("buffer update data must not be null");
    if (offset > entry->bufferSize ||
        size > entry->bufferSize - offset)
    {
        throw std::out_of_range(
            "buffer update exceeds the buffer capacity"
        );
    }
    glBindBuffer(GL_ARRAY_BUFFER, entry->object);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        static_cast<GLintptr>(offset),
        static_cast<GLsizeiptr>(size),
        data
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0U);
}

void OpenGlRenderDevice::DestroyBuffer(BufferHandle handle)
{
    if (RenderDevice::HandleDevice(handle) != this->DeviceUid())
    {
        throw std::invalid_argument(
            "buffer handle does not belong to this RenderDevice"
        );
    }
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Buffer)
    {
        throw std::invalid_argument(
            "buffer handle does not belong to this RenderDevice"
        );
    }
    this->graphicsDevice.DeleteResource(
        GraphicsDevice::ResourceKind::Buffer,
        entry->object
    );
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::DestroyTexture(TextureHandle handle)
{
    if (RenderDevice::HandleDevice(handle) != this->DeviceUid())
    {
        throw std::invalid_argument(
            "texture handle does not belong to this RenderDevice"
        );
    }
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Texture)
    {
        throw std::invalid_argument(
            "texture handle does not belong to this RenderDevice"
        );
    }
    this->graphicsDevice.DeleteResource(
        GraphicsDevice::ResourceKind::Texture,
        entry->object
    );
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::DestroySampler(SamplerHandle handle)
{
    if (RenderDevice::HandleDevice(handle) != this->DeviceUid())
    {
        throw std::invalid_argument(
            "sampler handle does not belong to this RenderDevice"
        );
    }
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Sampler)
    {
        throw std::invalid_argument(
            "sampler handle does not belong to this RenderDevice"
        );
    }
    // Sampler is a share-group shared object: reuse the R1.7 deletion queue
    // (immediate when the owning share group is current, pending otherwise).
    this->graphicsDevice.DeleteResource(
        GraphicsDevice::ResourceKind::Sampler,
        entry->object
    );
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::DestroyGraphicsPipeline(
    PipelineHandle handle
)
{
    if (RenderDevice::HandleDevice(handle) != this->DeviceUid())
    {
        throw std::invalid_argument(
            "pipeline handle does not belong to this RenderDevice"
        );
    }
    ResourceEntry* entry = this->Find(RenderDevice::HandleId(handle));
    if (entry == nullptr || entry->kind != ResourceKind::Pipeline)
    {
        throw std::invalid_argument(
            "pipeline handle does not belong to this RenderDevice"
        );
    }
    this->graphicsDevice.DeleteResource(
        GraphicsDevice::ResourceKind::Program,
        entry->object
    );
    this->Erase(RenderDevice::HandleId(handle));
}

void OpenGlRenderDevice::ExecuteGraph(RenderGraph& graph)
{
    // The OpenGL backend owns graph execution: pass callbacks are wired by
    // the OpenGL pass executor layer (Renderer::Execute*). A future Vulkan
    // backend interprets the graph data instead of callbacks.
    graph.Execute();
}

std::unique_ptr<PresentationTarget>
OpenGlRenderDevice::CreatePresentationTarget(
    const PresentSurface& surface,
    PresentBlitFunction blit,
    PresentSwapFunction swap
)
{
    return std::make_unique<OpenGlPresentationTarget>(
        surface,
        std::move(blit),
        std::move(swap)
    );
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

RenderResourceCache& OpenGlRenderDevice::RenderCache() noexcept
{
    return this->renderCache;
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
