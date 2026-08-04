#pragma once
#include <cstdint>
#include <filesystem>
#include <glad/gl.h>
#include <span>
#include <string>
#include <vector>

namespace wisteria
{
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
    explicit Texture(TextureData data = {});
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

    inline GLuint GetTexture() const noexcept { return this->texture; }
    bool IsAttached() const noexcept;
    TextureColorSpace ColorSpace() const noexcept;
private:
    void EnsureCreated();
    static GLint MaxUnits();
    static void ValidateUnit(unsigned int unit);
    void ActiveTexture(unsigned int unit);
    void Configure();
    void UploadDecodedPixels(
        const unsigned char* pixels,
        int width,
        int height,
        unsigned int unit
    );
private:
    TextureData data;
    GLuint texture = 0;
    bool configured = false;
    bool attached = false;
};
}  // namespace wisteria
