#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wisteria
{
class TextureGpuResource;
class RenderResourceCache;

enum class TextureColorSpace
{
    Linear,
    Srgb
};

struct TextureData
{
    std::filesystem::path filePath;
    std::vector<std::uint8_t> data;
    int width = 0;
    int height = 0;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;

    static TextureData FromFile(
        std::filesystem::path filePath,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb
    );
    static TextureData FromEncoded(
        std::vector<std::uint8_t> encodedData,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb
    );
    static TextureData FromRgba8(
        int width,
        int height,
        std::vector<std::uint8_t> pixels,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb
    );

    bool IsFile() const noexcept;
    bool IsEncoded() const noexcept;
    bool IsRgba8() const noexcept;
};

class Texture{
public:
    explicit Texture(
        TextureData data = {},
        RenderResourceCache* cache = nullptr
    );
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void Attach();
    void Bind(unsigned int unit = 0);
    void Unbind(unsigned int unit = 0);
    void Upload(
        const std::filesystem::path& filePath,
        unsigned int unit = 0
    );
    void UploadEncoded(
        std::span<const std::uint8_t> encodedData,
        unsigned int unit = 0
    );
    void UploadRgba8(
        int width,
        int height,
        std::span<const std::uint8_t> pixels,
        unsigned int unit = 0
    );

    // R2.0 Phase 0C 6A: attach a per-device cache after CPU-only creation.
    // No-op once a realization is attached.
    void SetRenderCache(RenderResourceCache* cache);

    bool IsAttached() const noexcept;
    TextureColorSpace ColorSpace() const noexcept;
private:
    TextureData data;
    std::shared_ptr<TextureGpuResource> gpu;
    RenderResourceCache* cache = nullptr;
};
}  // namespace wisteria
