#pragma once

#include "wisteria/rendering/graphics_device.hpp"
#include "wisteria/rendering/texture.hpp"

#include <cstdint>

namespace wisteria
{
// R2.0 Phase 0C Step 3: per-device GPU realization of a Texture CPU asset.
// Owns the GL texture object and all GL upload/bind operations. Decoding
// (file/stb_image) stays in the CPU asset layer (Texture).
class TextureGpuResource
{
public:
    explicit TextureGpuResource(GraphicsDevice* device);
    ~TextureGpuResource();

    void Bind(unsigned int unit);
    void Unbind(unsigned int unit);
    void UploadDecodedPixels(
        const unsigned char* pixels,
        int width,
        int height,
        TextureColorSpace colorSpace,
        unsigned int unit = 0U
    );
    bool IsAttached() const noexcept;
    GLuint Id() const noexcept;

private:
    void EnsureCreated();
    void Configure(TextureColorSpace colorSpace);
    void ActiveTexture(unsigned int unit);
    void ValidateUnit(unsigned int unit);
    static GLint MaxUnits();

    GraphicsDevice* device = nullptr;
    GLuint texture = 0U;
    bool configured = false;
    bool attached = false;
};
}  // namespace wisteria
