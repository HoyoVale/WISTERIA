#pragma once
#include <string>
#include <iostream>
#include <glad/gl.h>
#include <stb_image.h>

GLint GetMaxUnits();

class Texture{
public:
    Texture();
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void Bind(unsigned int unit = 0);
    void Unbind(unsigned int unit = 0);
    void Upload(const std::string& filePath, unsigned int unit = 0);

    inline GLuint GetTexture() const { return this->texture; }
private:
    void ActiveTexture(unsigned int unit);
private:
    GLuint texture = 0;
};
