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
    Textureindex++;
    if(Textureindex >= GetMaxUnits()) 
    {
        std::cerr << "Texture slots are full!" << std::endl;
        Textureindex--;
        return;
    }
    this->index = Textureindex;
}

Texture::~Texture()
{
    if(this->texture != 0)
    {
        this->unBind();
        glDeleteTextures(1, &this->texture);
    }
}

void Texture::Bind()
{
    if(index >= GetMaxUnits()) {
        std::cerr << "Texture slots are full!" << std::endl;
        exit(1);
    }
    ActiveTexture(this->index);
    glBindTexture(GL_TEXTURE_2D, this->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}

void Texture::Bind(unsigned int index)
{
    if(index >= GetMaxUnits()) {
        std::cerr << "Texture slots are full!" << std::endl;
        exit(1);
    }
    ActiveTexture(index);
    glBindTexture(GL_TEXTURE_2D, this->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}

void Texture::unBind()
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Upload(std::string FilePath)
{
    this->Bind();
    int width, height, channels;

    unsigned char* pixels = stbi_load(FilePath.c_str(), &width, &height, &channels, 4);

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

void Texture::ActiveTexture(unsigned int &index)
{
    glActiveTexture(GL_TEXTURE0 + index);
}