#pragma once
#include <cstdint>
#include <filesystem>
#include <glad/gl.h>
#include "wisteria/rendering/graphics_device.hpp"
#include <span>
#include <string>
#include <vector>

namespace wisteria
{
class TextureGpuResource;

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
        GraphicsDevice* device = nullptr
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

    // R2.0 Phase 0C Step 3: the GL texture object now lives in the GPU
    // realization; this accessor is retained for existing callers only.
    GLuint GetTexture() const noexcept;
    bool IsAttached() const noexcept;
    TextureColorSpace ColorSpace() const noexcept;
private:
    GraphicsDevice* device = nullptr;
    TextureData data;
    std::unique_ptr<TextureGpuResource> gpu;
};
}  // namespace wisteria
