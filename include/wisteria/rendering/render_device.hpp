#pragma once

// R2.0 Phase 0B: backend-neutral GPU execution/resource authority.
//
// Frozen by docs/architecture/R2_0_RENDER_ARCHITECTURE_CONTRACT.md.
// This header is backend-neutral: it must never include glad/gl.h or any
// Vulkan header, and no public signature may expose GLuint/GLenum/Vk*/native
// context handles/API-specific synchronization objects.
//
// 0B scope (foundation only): capabilities + resource handles/descriptors +
// OpenGL backend implementation. Mesh/Texture/Material migration is 0C;
// RenderFramePacket/RenderGraph is 0D; PresentSurface is 0E. SubmitFrameWork
// is intentionally NOT part of the v1 interface yet (defined with RenderGraph
// in 0D).

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string_view>
#include <vector>

namespace wisteria
{
// Engine-semantic backend identity. OpenGL is the only R2.0 backend;
// Vulkan enters in R2.1 as an additive id.
enum class RenderBackendId : std::uint32_t
{
    OpenGL = 1U
};

// Engine semantic capabilities. These describe what WISTERIA needs, not a
// mirror of the underlying API capability table (contract §3).
struct RenderDeviceCapabilities
{
    // Independent per-output blend (replaces GL_ARB_draw_buffers_blend
    // probing at the renderer layer).
    bool independentBlend = false;
    // Maximum number of skinning matrices addressable by one mesh draw
    // (replaces GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS probing).
    std::size_t maxSkinningMatrices = 0U;
};

// --- strongly typed backend-neutral resource handles -------------------
// Value handles, scoped to exactly one RenderDevice. They never expose
// backend-native object identity. Wrong-device use is an engine contract
// violation (detected by the owning device). 0A intentionally does not
// freeze whether these are generational integers, slot-map indices or
// internal pointers.

class RenderDevice;

class BufferHandle
{
public:
    BufferHandle() = default;
    constexpr bool IsValid() const noexcept
    {
        return this->device_ != 0U && this->id_ != 0U;
    }

private:
    friend class RenderDevice;
    explicit BufferHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
        : device_(device),
          id_(id)
    {
    }
    std::uint64_t device_ = 0U;
    std::uint64_t id_ = 0U;
};

class TextureHandle
{
public:
    TextureHandle() = default;
    constexpr bool IsValid() const noexcept
    {
        return this->device_ != 0U && this->id_ != 0U;
    }

private:
    friend class RenderDevice;
    explicit TextureHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
        : device_(device),
          id_(id)
    {
    }
    std::uint64_t device_ = 0U;
    std::uint64_t id_ = 0U;
};

class SamplerHandle
{
public:
    SamplerHandle() = default;
    constexpr bool IsValid() const noexcept
    {
        return this->device_ != 0U && this->id_ != 0U;
    }

private:
    friend class RenderDevice;
    explicit SamplerHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
        : device_(device),
          id_(id)
    {
    }
    std::uint64_t device_ = 0U;
    std::uint64_t id_ = 0U;
};

class PipelineHandle
{
public:
    PipelineHandle() = default;
    constexpr bool IsValid() const noexcept
    {
        return this->device_ != 0U && this->id_ != 0U;
    }

private:
    friend class RenderDevice;
    explicit PipelineHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
        : device_(device),
          id_(id)
    {
    }
    std::uint64_t device_ = 0U;
    std::uint64_t id_ = 0U;
};

// --- backend-neutral descriptors ---------------------------------------

enum class BufferUsage : std::uint8_t
{
    Vertex,
    Index,
    Uniform
};

struct BufferDesc
{
    std::size_t size = 0U;
    BufferUsage usage = BufferUsage::Vertex;
};

enum class TextureFormat : std::uint8_t
{
    Rgba8,
    Rgba8Srgb,
    Depth24Stencil8
};

enum class TextureFilter : std::uint8_t
{
    Linear,
    Nearest
};

enum class TextureWrap : std::uint8_t
{
    Repeat,
    ClampToEdge
};

struct TextureDesc
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    TextureFormat format = TextureFormat::Rgba8;
    bool generateMipmaps = false;
};

struct SamplerDesc
{
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureWrap wrapS = TextureWrap::Repeat;
    TextureWrap wrapT = TextureWrap::Repeat;
};

enum class ShaderStage : std::uint8_t
{
    Vertex,
    Fragment
};

struct ShaderStageDesc
{
    ShaderStage stage = ShaderStage::Vertex;
    // The source language is owned by the backend implementation:
    // OpenGL accepts GLSL, Vulkan (R2.1) accepts SPIR-V. The neutral layer
    // must NEVER branch "if (OpenGL) GLSL else SPIR-V" (0C Step 7): pipeline
    // realization selection belongs to the backend.
    std::string_view source;
    // Informational only in 0B/0C; OpenGL backend compiles the fixed main.
    std::string_view entryPoint;
};

// R2.0 Phase 0C Step 7: engine-semantic built-in pipeline variant key.
// 0D RenderGraph/pipeline realization will select backend pipelines from
// this key instead of shipping GLSL through the neutral layer. 0B/0C keep
// GraphicsPipelineDesc.stages as the working surface.
enum class PipelineVariant : std::uint8_t
{
    PbrMetallicRoughness,
    MmdToon,
    ShadowDepth,
    GroundShadow,
    Skybox,
    OitComposite,
    Present
};

struct PipelineVariantKey
{
    PipelineVariant variant = PipelineVariant::PbrMetallicRoughness;
    // 0C: reserved for future semantic flags (alpha mode, skinning, morph).
    std::uint32_t flags = 0U;
};

struct GraphicsPipelineDesc
{
    // Diagnostics only in 0B/0C; layout/pipeline state is 0C/0D (additive).
    std::string_view label;
    std::vector<ShaderStageDesc> stages;
};

// --- backend-neutral RenderDevice interface ----------------------------
//
// R2.0 lifetime rules (contract §3):
// - handles are scoped to exactly one RenderDevice;
// - wrong-device handle use is an engine contract violation;
// - Destroy/retire makes the logical handle unusable immediately;
// - the backend may defer physical GPU destruction until safe;
// - R2.0 is creator-thread / single graphics execution-domain affine;
// - no thread-safety guarantee.
class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    virtual RenderBackendId BackendId() const noexcept = 0;
    virtual std::string_view BackendName() const noexcept = 0;
    virtual const RenderDeviceCapabilities& Capabilities() const = 0;

    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual PipelineHandle CreateGraphicsPipeline(
        const GraphicsPipelineDesc& desc
    ) = 0;

    virtual void UpdateBuffer(
        BufferHandle handle,
        const void* data,
        std::size_t size,
        std::size_t offset = 0U
    ) = 0;

    virtual void DestroyBuffer(BufferHandle handle) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;
    virtual void DestroySampler(SamplerHandle handle) = 0;
    virtual void DestroyGraphicsPipeline(PipelineHandle handle) = 0;

protected:
    RenderDevice() : deviceUid_(NextDeviceUid()) {}

    static BufferHandle MakeBufferHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
    {
        return BufferHandle(device, id);
    }
    static TextureHandle MakeTextureHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
    {
        return TextureHandle(device, id);
    }
    static SamplerHandle MakeSamplerHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
    {
        return SamplerHandle(device, id);
    }
    static PipelineHandle MakePipelineHandle(
        std::uint64_t device,
        std::uint64_t id
    ) noexcept
    {
        return PipelineHandle(device, id);
    }
    static std::uint64_t HandleId(BufferHandle handle) noexcept
    {
        return handle.id_;
    }
    static std::uint64_t HandleId(TextureHandle handle) noexcept
    {
        return handle.id_;
    }
    static std::uint64_t HandleId(SamplerHandle handle) noexcept
    {
        return handle.id_;
    }
    static std::uint64_t HandleId(PipelineHandle handle) noexcept
    {
        return handle.id_;
    }
    static std::uint64_t HandleDevice(BufferHandle handle) noexcept
    {
        return handle.device_;
    }
    static std::uint64_t HandleDevice(TextureHandle handle) noexcept
    {
        return handle.device_;
    }
    static std::uint64_t HandleDevice(SamplerHandle handle) noexcept
    {
        return handle.device_;
    }
    static std::uint64_t HandleDevice(PipelineHandle handle) noexcept
    {
        return handle.device_;
    }

    std::uint64_t DeviceUid() const noexcept { return this->deviceUid_; }

private:
    static std::uint64_t NextDeviceUid() noexcept
    {
        static std::atomic<std::uint64_t> nextDeviceUid{1U};
        return nextDeviceUid.fetch_add(
            1U,
            std::memory_order_relaxed
        );
    }

    std::uint64_t deviceUid_ = 0U;
};
}  // namespace wisteria
