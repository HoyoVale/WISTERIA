#pragma once

#include "wisteria/rendering/render_device.hpp"

#include "wisteria/rendering/graphics_device.hpp"

#include "render_resource_cache.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace wisteria
{
// R2.0 Phase 0B: the OpenGL realization of RenderDevice.
//
// Absorbs the existing GraphicsDevice (which is classified as OpenGL backend
// implementation, not the generic RenderDevice contract). It reuses the
// R1.7-validated context/share-group ownership semantics of GraphicsDevice
// instead of re-inventing a second ownership system.
//
// 0B is foundation-only: resource handles are real GPU objects, but the
// renderer/mesh/texture layers still consume GraphicsDevice directly (0C
// migrates them). The GL allowlist must not grow through this class.
class OpenGlRenderDevice final : public RenderDevice
{
public:
    OpenGlRenderDevice();
    ~OpenGlRenderDevice() override;

    OpenGlRenderDevice(const OpenGlRenderDevice&) = delete;
    OpenGlRenderDevice& operator=(const OpenGlRenderDevice&) = delete;
    OpenGlRenderDevice(OpenGlRenderDevice&&) = delete;
    OpenGlRenderDevice& operator=(OpenGlRenderDevice&&) = delete;

    RenderBackendId BackendId() const noexcept override;
    std::string_view BackendName() const noexcept override;
    const RenderDeviceCapabilities& Capabilities() const override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    PipelineHandle CreateGraphicsPipeline(
        const GraphicsPipelineDesc& desc
    ) override;

    void UpdateBuffer(
        BufferHandle handle,
        const void* data,
        std::size_t size,
        std::size_t offset = 0U
    ) override;

    void DestroyBuffer(BufferHandle handle) override;
    void DestroyTexture(TextureHandle handle) override;
    void DestroySampler(SamplerHandle handle) override;
    void DestroyGraphicsPipeline(PipelineHandle handle) override;

    // OpenGL-backend-internal access to the absorbed R1.7 machinery. Must
    // never be promoted into the neutral RenderDevice contract (Gate B).
    GraphicsDevice& LegacyGraphicsDevice() noexcept;
    const GraphicsDevice& LegacyGraphicsDevice() const noexcept;

    // Queries engine-semantic capabilities from the current GL context.
    // Call with a context of the owning share group current; the composition
    // roots (HeadlessRenderSession / Application) refresh after activation.
    void RefreshCapabilities();

    // 0B transition helper: extracts the absorbed legacy GraphicsDevice from
    // a RenderDevice. Returns nullptr for null/non-OpenGL devices. OpenGL
    // backend paths only; never promoted into the neutral contract.
    static GraphicsDevice* GraphicsDeviceFrom(
        RenderDevice* renderDevice
    ) noexcept;

    // R2.0 Phase 0C Step 6: per-device shared realization cache (static
    // assets only; runtime-deformed instances never consult it).
    RenderResourceCache& RenderCache() noexcept;

private:
    enum class ResourceKind
    {
        Buffer,
        Texture,
        Sampler,
        Pipeline
    };

    struct ResourceEntry
    {
        ResourceKind kind = ResourceKind::Buffer;
        std::uint32_t object = 0U;
        std::size_t bufferSize = 0U;
        BufferUsage bufferUsage = BufferUsage::Vertex;
    };

    const ResourceEntry* Find(std::uint64_t id) const;
    ResourceEntry* Find(std::uint64_t id);
    std::uint64_t AllocateId() noexcept;
    void Erase(std::uint64_t id) noexcept;

    GraphicsDevice graphicsDevice;
    RenderResourceCache renderCache;
    RenderDeviceCapabilities capabilities;
    bool capabilitiesValid = false;
    std::uint64_t nextHandle = 1U;
    std::unordered_map<std::uint64_t, ResourceEntry> resources;
};
}  // namespace wisteria
