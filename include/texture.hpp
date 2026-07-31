#pragma once
#include <string>
#include <glad/gl.h>

class Texture{
public:
    Texture();
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void Bind(unsigned int unit = 0);
    void Unbind(unsigned int unit = 0);
    void Upload(const std::string& filePath, unsigned int unit = 0);

    inline GLuint GetTexture() const noexcept { return this->texture; }
private:
    static GLint MaxUnits();
    static void ValidateUnit(unsigned int unit);
    void ActiveTexture(unsigned int unit);
    void Configure();
private:
    GLuint texture = 0;
    bool configured = false;
};
