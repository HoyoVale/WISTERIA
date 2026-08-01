#include "pch.hpp"
#include "texture.hpp"
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <stb_image.h>
#include <utility>

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

Texture::Texture(TextureData data)
    : data(std::move(data))
{
}

Texture::~Texture()
{
    if (this->texture != 0)
    {
        glDeleteTextures(1, &this->texture);
        this->texture = 0;
    }
}

void Texture::Bind(unsigned int unit)
{
    if (!this->attached)
        throw std::logic_error("Texture must be attached before binding");

    ValidateUnit(unit);
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, this->texture);
}

void Texture::Unbind(unsigned int unit)
{
    ValidateUnit(unit);
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, 0);
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

    this->UploadDecodedPixels(pixels.get(), width, height, unit);
}

void Texture::UploadRgba8(
    int width,
    int height,
    std::span<const std::uint8_t> pixels,
    unsigned int unit
)
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("RGBA texture dimensions must be positive");

    const std::size_t expectedSize =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4;
    if (pixels.size() != expectedSize)
        throw std::invalid_argument("RGBA texture byte count does not match its dimensions");

    this->UploadDecodedPixels(pixels.data(), width, height, unit);
}

void Texture::Attach()
{
    if (this->attached)
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
    return this->attached;
}

TextureColorSpace Texture::ColorSpace() const noexcept
{
    return this->data.colorSpace;
}

void Texture::EnsureCreated()
{
    if (this->texture == 0)
        glGenTextures(1, &this->texture);
    if (this->texture == 0)
        throw std::runtime_error("Cannot create OpenGL texture");
}

void Texture::UploadDecodedPixels(
    const unsigned char* pixels,
    int width,
    int height,
    unsigned int unit
)
{
    ValidateUnit(unit);
    this->EnsureCreated();
    this->ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, this->texture);
    this->Configure();

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        this->data.colorSpace == TextureColorSpace::Srgb
            ? GL_SRGB8_ALPHA8
            : GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );
    glGenerateMipmap(GL_TEXTURE_2D);
    this->attached = true;
}

void Texture::ActiveTexture(unsigned int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
}

GLint Texture::MaxUnits()
{
    // This application uses one OpenGL context, so the limit is stable.
    static const GLint maxUnits = []
    {
        GLint value = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value);
        return value;
    }();
    return maxUnits;
}

void Texture::ValidateUnit(unsigned int unit)
{
    const GLint maxUnits = MaxUnits();
    if (maxUnits <= 0)
        throw std::runtime_error("OpenGL reported no available texture units");

    if (unit >= static_cast<unsigned int>(maxUnits))
    {
        throw std::out_of_range(
            "Texture unit " + std::to_string(unit) +
            " is out of range; maximum unit count is " +
            std::to_string(maxUnits)
        );
    }
}

void Texture::Configure()
{
    if (this->configured)
        return;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    this->configured = true;
}
