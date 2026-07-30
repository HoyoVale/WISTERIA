#pragma once
#include <string>
#include <filesystem>
#include <glad/gl.h>
#include <vector>

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

    inline GLuint GetProgram() const { return program; };
private:
    GLuint program = 0;
};