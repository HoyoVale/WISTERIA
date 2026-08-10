#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/texture.hpp"
#include <fstream>
#include <memory>
#include <stdexcept>
#include "wisteria/vendor/stb_image.h"
#include <utility>
#include "backend/opengl/texture_gpu_resource.hpp"
#include "backend/opengl/render_resource_cache.hpp"

namespace wisteria
{
TextureData TextureData::FromFile(
    std::filesystem::path filePath,
    TextureColorSpace colorSpace
)
{
    TextureData result;
    result.filePath = std::move(filePath);
    result.colorSpace = colorSpace;
    return result;
}

TextureData TextureData::FromEncoded(
    std::vector<std::uint8_t> encodedData,
    TextureColorSpace colorSpace
)
{
    TextureData result;
    result.data = std::move(encodedData);
    result.colorSpace = colorSpace;
    return result;
}

TextureData TextureData::FromRgba8(
    int width,
    int height,
    std::vector<std::uint8_t> pixels,
    TextureColorSpace colorSpace
)
{
    TextureData result;
    result.width = width;
    result.height = height;
    result.data = std::move(pixels);
    result.colorSpace = colorSpace;
    return result;
}

bool TextureData::IsFile() const noexcept
{
    return !this->filePath.empty();
}

bool TextureData::IsEncoded() const noexcept
{
    return this->filePath.empty() && this->width == 0 &&
        this->height == 0 && !this->data.empty();
}

bool TextureData::IsRgba8() const noexcept
{
    return this->filePath.empty() && this->width > 0 &&
        this->height > 0 && !this->data.empty();
}

Texture::Texture(
    TextureData data,
    RenderResourceCache* cache
)
    : data(std::move(data)),
      cache(cache)
{
    if (this->cache != nullptr)
        this->gpu = this->cache->AcquireTexture(this->data);
    else
        this->gpu = std::make_shared<TextureGpuResource>(nullptr);
}

Texture::~Texture() = default;

void Texture::SetRenderCache(RenderResourceCache* nextCache)
{
    // Textures are assets: allow re-resolving for another device even after
    // attach. The previous device's cache keeps its realization alive.
    this->cache = nextCache;
    if (this->cache != nullptr)
        this->gpu = this->cache->AcquireTexture(this->data);
}

void Texture::Bind(unsigned int unit)
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Texture without a RenderResourceCache cannot bind"
        );
    }
    this->gpu->Bind(unit);
}

void Texture::Unbind(unsigned int unit)
{
    if (this->gpu == nullptr)
        return;
    this->gpu->Unbind(unit);
}

void Texture::Upload(
    const std::filesystem::path& filePath,
    unsigned int unit
)
{
    std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open texture file: " + filePath.string());

    const std::streampos end = stream.tellg();
    if (end <= 0)
        throw std::runtime_error("Texture file is empty: " + filePath.string());
    if (static_cast<std::uintmax_t>(end) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error("Texture file is too large: " + filePath.string());
    }

    std::vector<std::uint8_t> encodedData(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(encodedData.data()),
        static_cast<std::streamsize>(encodedData.size())
    );
    if (!stream)
        throw std::runtime_error("Cannot read texture file: " + filePath.string());

    this->UploadEncoded(encodedData, unit);
}

void Texture::UploadEncoded(
    std::span<const std::uint8_t> encodedData,
    unsigned int unit
)
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Texture without a RenderResourceCache cannot upload"
        );
    }
    if (encodedData.empty())
        throw std::invalid_argument("Encoded texture data must not be empty");
    if (encodedData.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Encoded texture data is too large for stb_image");

    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(
            encodedData.data(),
            static_cast<int>(encodedData.size()),
            &width,
            &height,
            &channels,
            4
        ),
        stbi_image_free
    );
    if (pixels == nullptr)
    {
        const char* failureReason = stbi_failure_reason();
        throw std::runtime_error(
            "Cannot decode embedded texture (" +
            std::string(failureReason != nullptr ? failureReason : "unknown stb_image error") +
            ")"
        );
    }

    this->gpu->UploadDecodedPixels(
        pixels.get(),
        width,
        height,
        this->data.colorSpace,
        unit
    );
}

void Texture::UploadRgba8(
    int width,
    int height,
    std::span<const std::uint8_t> pixels,
    unsigned int unit
)
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Texture without a RenderResourceCache cannot upload"
        );
    }
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("RGBA texture dimensions must be positive");

    const std::size_t expectedSize =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4;
    if (pixels.size() != expectedSize)
        throw std::invalid_argument("RGBA texture byte count does not match its dimensions");

    this->gpu->UploadDecodedPixels(
        pixels.data(),
        width,
        height,
        this->data.colorSpace,
        unit
    );
}

void Texture::Attach()
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Texture without a RenderResourceCache cannot attach"
        );
    }
    if (this->gpu->IsAttached())
        return;

    if (this->data.IsFile())
        this->Upload(this->data.filePath);
    else if (this->data.IsEncoded())
        this->UploadEncoded(this->data.data);
    else if (this->data.IsRgba8())
        this->UploadRgba8(this->data.width, this->data.height, this->data.data);
    else
        throw std::logic_error("Texture has no upload source");
}

bool Texture::IsAttached() const noexcept
{
    return this->gpu != nullptr && this->gpu->IsAttached();
}

TextureColorSpace Texture::ColorSpace() const noexcept
{
    return this->data.colorSpace;
}

}  // namespace wisteria
