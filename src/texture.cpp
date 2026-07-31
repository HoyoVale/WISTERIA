#include "pch.hpp"
#include "texture.hpp"
#include <stdexcept>
#include <stb_image.h>

Texture::Texture()
{
    glGenTextures(1, &this->texture);
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

void Texture::Upload(const std::string& filePath, unsigned int unit)
{
    this->Bind(unit);
    this->Configure();
    int width, height, channels;

    unsigned char* pixels = stbi_load(filePath.c_str(), &width, &height, &channels, 4);

    if (pixels == nullptr) {
        const char* failureReason = stbi_failure_reason();
        throw std::runtime_error(
            "Cannot load texture: " + filePath + " (" +
            (failureReason != nullptr ? failureReason : "unknown stb_image error") +
            ")"
        );
    }

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );

    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(pixels);
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
