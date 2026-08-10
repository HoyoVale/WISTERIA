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
    for (const TextureEntry& entry : this->textures)
    {
        if (entry.key == key && TextureDataEqual(entry.data, data))
            return entry.realization;
    }

    auto realization = std::make_shared<TextureGpuResource>(this->device);
    this->textures.push_back(TextureEntry{key, data, realization});
    return realization;
}

std::shared_ptr<MeshGpuResource> RenderResourceCache::AcquireStaticMesh(
    const DefaultModelData& data
)
{
    const std::uint64_t key = DataHash(data);
    for (const StaticMeshEntry& entry : this->staticMeshes)
    {
        if (entry.hash == key && MeshDataEqual(entry.data, data))
            return entry.realization;
    }

    auto realization = std::make_shared<MeshGpuResource>(this->device);
    this->staticMeshes.push_back(
        StaticMeshEntry{key, data, realization}
    );
    return realization;
}

std::shared_ptr<EnvironmentMapGpuResource>
RenderResourceCache::AcquireEnvironment(
    const EnvironmentMapData& data
)
{
    const std::string key = EnvironmentKey(data);
    const auto iterator = this->environments.find(key);
    if (iterator != this->environments.end())
        return iterator->second;

    auto realization = std::make_shared<EnvironmentMapGpuResource>(
        data,
        this
    );
    this->environments.emplace(key, realization);
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

std::shared_ptr<EnvironmentMapGpuResource>
RenderResourceCache::CreateInstanceEnvironment(
    const EnvironmentMapData& data
)
{
    return std::make_shared<EnvironmentMapGpuResource>(data, this);
}

GraphicsDevice* RenderResourceCache::Device() const noexcept
{
    return this->device;
}

void RenderResourceCache::Clear() noexcept
{
    this->textures.clear();
    this->staticMeshes.clear();
    this->environments.clear();
}

std::size_t RenderResourceCache::TextureCount() const noexcept
{
    return this->textures.size();
}

std::size_t RenderResourceCache::StaticMeshCount() const noexcept
{
    return this->staticMeshes.size();
}

std::size_t RenderResourceCache::EnvironmentCount() const noexcept
{
    return this->environments.size();
}

std::string RenderResourceCache::TextureKey(const TextureData& data)
{
    const char* colorSpace =
        data.colorSpace == TextureColorSpace::Srgb ? ":srgb" : ":linear";
    if (data.IsFile())
        return "file:" + data.filePath.lexically_normal().string() + colorSpace;

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

std::string RenderResourceCache::EnvironmentKey(
    const EnvironmentMapData& data
)
{
    std::ostringstream stream;
    if (data.equirectangularImage != nullptr)
    {
        const EnvironmentHdrImage& image = *data.equirectangularImage;
        const std::uint64_t hash = Fnv1a64Bytes(
            reinterpret_cast<const std::uint8_t*>(image.rgb.data()),
            image.rgb.size() * sizeof(float)
        );
        stream << "eq:" << std::hex << hash
               << ":w" << image.width << ":h" << image.height;
    }
    else
    {
        stream << "proc";
    }
    stream << ":env" << data.environmentResolution
           << ":irr" << data.irradianceResolution
           << ":pre" << data.prefilterResolution
           << ":mip" << data.prefilterMipLevels
           << ":brdf" << data.brdfResolution;
    return stream.str();
}

bool RenderResourceCache::MeshDataEqual(
    const DefaultModelData& left,
    const DefaultModelData& right
)
{
    if (left.vertices != right.vertices ||
        left.indices != right.indices ||
        left.layout.size() != right.layout.size())
    {
        return false;
    }
    for (std::size_t index = 0U; index < left.layout.size(); ++index)
    {
        const Layout& a = left.layout[index];
        const Layout& b = right.layout[index];
        if (a.name != b.name ||
            a.size != b.size ||
            a.format != b.format ||
            a.normalized != b.normalized ||
            a.integer != b.integer ||
            a.location != b.location ||
            a.semantic != b.semantic)
        {
            return false;
        }
    }
    return true;
}

bool RenderResourceCache::TextureDataEqual(
    const TextureData& left,
    const TextureData& right
)
{
    return left.filePath == right.filePath &&
        left.data == right.data &&
        left.width == right.width &&
        left.height == right.height &&
        left.colorSpace == right.colorSpace;
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
