#include "wisteria/common/pch.hpp"

#include "render_resource_cache.hpp"

#include <sstream>

namespace wisteria
{
namespace
{
std::uint64_t Fnv1a64Bytes(const std::uint8_t* bytes, std::size_t size)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index)
    {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}
}  // namespace

RenderResourceCache::RenderResourceCache(GraphicsDevice* nextDevice)
    : device(nextDevice)
{
}

std::shared_ptr<TextureGpuResource> RenderResourceCache::AcquireTexture(
    const TextureData& data
)
{
    const std::string key = TextureKey(data);
    const auto iterator = this->textures.find(key);
    if (iterator != this->textures.end())
        return iterator->second;

    auto realization = std::make_shared<TextureGpuResource>(this->device);
    this->textures.emplace(key, realization);
    return realization;
}

std::shared_ptr<MeshGpuResource> RenderResourceCache::AcquireStaticMesh(
    const DefaultModelData& data
)
{
    const std::uint64_t key = DataHash(data);
    const auto iterator = this->staticMeshes.find(key);
    if (iterator != this->staticMeshes.end())
        return iterator->second;

    auto realization = std::make_shared<MeshGpuResource>(this->device);
    this->staticMeshes.emplace(key, realization);
    return realization;
}

std::shared_ptr<MeshGpuResource> RenderResourceCache::CreateInstanceMesh(
    const DefaultModelData& data
)
{
    (void)data;
    return std::make_shared<MeshGpuResource>(this->device);
}

std::shared_ptr<TextureGpuResource> RenderResourceCache::CreateInstanceTexture(
    const TextureData& data
)
{
    (void)data;
    return std::make_shared<TextureGpuResource>(this->device);
}

GraphicsDevice* RenderResourceCache::Device() const noexcept
{
    return this->device;
}

void RenderResourceCache::Clear() noexcept
{
    this->textures.clear();
    this->staticMeshes.clear();
}

std::size_t RenderResourceCache::TextureCount() const noexcept
{
    return this->textures.size();
}

std::size_t RenderResourceCache::StaticMeshCount() const noexcept
{
    return this->staticMeshes.size();
}

std::string RenderResourceCache::TextureKey(const TextureData& data)
{
    const char* colorSpace =
        data.colorSpace == TextureColorSpace::Srgb ? ":srgb" : ":linear";
    if (data.IsFile())
        return "file:" + data.filePath.string() + colorSpace;

    const std::uint64_t hash = Fnv1a64Bytes(
        data.data.data(),
        data.data.size()
    );
    std::ostringstream stream;
    stream << "payload:" << std::hex << hash << colorSpace;
    if (data.IsRgba8())
        stream << ":rgba8:" << data.width << "x" << data.height;
    return stream.str();
}

std::uint64_t RenderResourceCache::DataHash(
    const DefaultModelData& data
)
{
    std::uint64_t hash = Fnv1a64Bytes(
        reinterpret_cast<const std::uint8_t*>(data.vertices.data()),
        data.VertexBytes()
    );
    const std::uint8_t* indices =
        reinterpret_cast<const std::uint8_t*>(data.indices.data());
    hash ^= Fnv1a64Bytes(indices, data.IndexBytes());
    for (const Layout& attribute : data.layout)
    {
        hash ^= Fnv1a64Bytes(
            reinterpret_cast<const std::uint8_t*>(attribute.name.data()),
            attribute.name.size()
        );
        hash ^= static_cast<std::uint64_t>(attribute.size);
        hash ^= static_cast<std::uint64_t>(attribute.format);
        hash ^= static_cast<std::uint64_t>(attribute.normalized ? 1U : 0U);
        hash ^= static_cast<std::uint64_t>(attribute.integer ? 1U : 0U);
        hash ^= static_cast<std::uint64_t>(attribute.location);
    }
    return hash;
}
}  // namespace wisteria
