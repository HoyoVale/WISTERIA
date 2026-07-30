#pragma once
#include <string>
#include <filesystem>
#include <glad/gl.h>
#include <vector>
#include "texture.hpp"

static std::string shaderRootPath = std::filesystem::current_path().string() + "assets\\shaders\\";

struct Path{
    std::string VertexPath = shaderRootPath + "basic.vert"; 
    std::string FragmentPath = shaderRootPath + "basic.frag";
};

class Shader{
public:
    Shader(std::string VertexPath = shaderRootPath + "basic.vert", 
        std::string FragmrntPath = shaderRootPath + "basic.frag");
    ~Shader();
    inline std::vector<GLuint> GetShaderList() const { return shaderList; };
private:
    std::string ReadTextFile(const std::string& path);
    GLuint CreateShader(GLenum eShaderType, const std::string &strShaderFile);

private:
    Path* path = nullptr;
    std::vector<GLuint> shaderList;
};

class Program{
public:
    Program(std::vector<GLuint> shaderList);
    ~Program();
    GLuint CreateProgram(const std::vector<GLuint> &shaderList);

    void Use();
    void unUse();
    void UniformMat4f(std::string valueName, glm::mat4 value);
    void UniformMat3f(std::string valueName, glm::mat3 value);
    void UniformMat2f(std::string valueName, glm::mat2 value);
    void Uniform1f(std::string valueName, float value);
    void Uniform2f(std::string valueName, float x, float y);
    void Uniform3f(std::string valueName, float x, float y, float z);
    void Uniform1i(std::string valueName, int value);
    void Uniform1ui(std::string valueName, unsigned int value);
    void UniformTex(Texture &texture, std::string UniformNAme);

    inline GLuint GetProgram() const { return program; };

private:
    unsigned int GetUniformLocation(std::string valueName);
private:
    GLuint program = 0;
};