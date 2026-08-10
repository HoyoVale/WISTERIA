#include "wisteria/common/pch.hpp"

#include "texture_gpu_resource.hpp"

#include <limits>
#include <stdexcept>

namespace wisteria
{
TextureGpuResource::TextureGpuResource(GraphicsDevice* nextDevice)
    : device(nextDevice)
{
}

TextureGpuResource::~TextureGpuResource()
{
    if (this->texture != 0U)
    {
        if (this->device != nullptr)
        {
            this->device->DeleteResource(
                GraphicsDevice::ResourceKind::Texture,
                this->texture
            );
        }
        else
        {
            glDeleteTextures(1, &this->texture);
        }
        this->texture = 0U;
    }
}

void TextureGpuResource::Bind(unsigned int unit)
{
    if (!this->attached)
        throw std::logic_error("Texture must be attached before binding");

    ValidateUnit(unit);
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, this->texture);
}

void TextureGpuResource::Unbind(unsigned int unit)
{
    ValidateUnit(unit);
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, 0U);
}

void TextureGpuResource::UploadDecodedPixels(
    const unsigned char* pixels,
    int width,
    int height,
    TextureColorSpace colorSpace,
    unsigned int unit
)
{
    if (pixels == nullptr || width <= 0 || height <= 0)
        throw std::invalid_argument("decoded texture pixels/dimensions invalid");
    ValidateUnit(unit);
    EnsureCreated();
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, this->texture);
    Configure(colorSpace);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        colorSpace == TextureColorSpace::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
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

bool TextureGpuResource::IsAttached() const noexcept
{
    return this->attached;
}

GLuint TextureGpuResource::Id() const noexcept
{
    return this->texture;
}

void TextureGpuResource::EnsureCreated()
{
    if (this->texture == 0U)
        glGenTextures(1, &this->texture);
    if (this->texture == 0U)
        throw std::runtime_error("Cannot create OpenGL texture");
}

void TextureGpuResource::Configure(TextureColorSpace colorSpace)
{
    (void)colorSpace;
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

void TextureGpuResource::ActiveTexture(unsigned int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
}

GLint TextureGpuResource::MaxUnits()
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

void TextureGpuResource::ValidateUnit(unsigned int unit)
{
    const GLint maxUnits = MaxUnits();
    if (maxUnits <= 0)
        throw std::runtime_error("OpenGL reported no available texture units");

    if (unit >= static_cast<unsigned int>(maxUnits))
    {
        throw std::out_of_range(
            "Texture unit exceeds GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS"
        );
    }
}
}  // namespace wisteria
