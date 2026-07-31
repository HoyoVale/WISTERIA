#include "pch.hpp"
#include "texture.hpp"
#include <iostream>


GLint GetMaxUnits()
{
    GLint maxUnits;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&maxUnits);
    //std::cout << "maxUnits: " << maxUnits << std::endl;
    return maxUnits;
}

Texture::Texture()
{
    glGenTextures(1, &this->texture);
}

Texture::~Texture()
{
    if(this->texture != 0)
    {
        this->Unbind();
        glDeleteTextures(1, &this->texture);
    }
}

void Texture::Bind(unsigned int unit)
{
    if(unit >= static_cast<unsigned int>(GetMaxUnits())) {
        std::cerr << "Texture slots are full!" << std::endl;
        exit(1);
    }
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, this->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}

void Texture::Unbind(unsigned int unit)
{
    if(unit >= static_cast<unsigned int>(GetMaxUnits())) {
        std::cerr << "Texture slots are full!" << std::endl;
        exit(1);
    }
    ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Upload(const std::string& filePath, unsigned int unit)
{
    this->Bind(unit);
    int width, height, channels;

    unsigned char* pixels = stbi_load(filePath.c_str(), &width, &height, &channels, 4);

    if (pixels == nullptr) {
        throw std::runtime_error(
            "Cannot load texture: " + filePath + " (" +
            stbi_failure_reason() + ")"
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
