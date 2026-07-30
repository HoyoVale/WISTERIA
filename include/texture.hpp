#pragma once
#include <string>
#include <iostream>
#include <glad/gl.h>
#include <stb_image.h>

static unsigned int Textureindex = 0;

GLint GetMaxUnits();

class Texture{
public:
    Texture();
    ~Texture();

    void Bind();
    void Bind(unsigned int index);
    void unBind();
    void Upload(std::string FilePath);

    inline unsigned int GetIndex() { return this->index; };
private:
    void ActiveTexture(unsigned int &index);
private:
    GLuint texture;
    unsigned int index;
};