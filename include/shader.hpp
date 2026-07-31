#pragma once
#include <string>
#include <filesystem>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
#include <vector>

inline const std::string shaderRootPath =
    (std::filesystem::current_path() / "assets" / "shaders").string() + "\\";

struct Path{
    std::string VertexPath = shaderRootPath + "basicTex.vert"; 
    std::string FragmentPath = shaderRootPath + "basicTex.frag";
};

class Shader{
public:
    Shader(
        std::string vertexPath = shaderRootPath + "basicTex.vert",
        std::string fragmentPath = shaderRootPath + "basicTex.frag"
    );
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    const std::vector<GLuint>& GetShaderList() const noexcept;
private:
    std::string ReadTextFile(const std::string& path);
    GLuint CreateShader(GLenum eShaderType, const std::string &strShaderFile);

private:
    Path path;
    std::vector<GLuint> shaderList;
};

class Program{
public:
    explicit Program(const std::vector<GLuint>& shaderList);
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    void Use();
    void unUse();

    // The program must be active before setting uniforms.
    void UniformMat4f(const std::string& valueName, const glm::mat4& value);
    void UniformMat3f(const std::string& valueName, const glm::mat3& value);
    void UniformMat2f(const std::string& valueName, const glm::mat2& value);
    void Uniform1f(const std::string& valueName, float value);
    void Uniform2f(const std::string& valueName, float x, float y);
    void Uniform3f(const std::string& valueName, float x, float y, float z);
    void Uniform1i(const std::string& valueName, int value);
    void Uniform1ui(const std::string& valueName, unsigned int value);
    void UniformTex(const std::string& uniformName, unsigned int textureUnit);

    inline GLuint GetProgram() const { return program; };

private:
    GLuint CreateProgram(const std::vector<GLuint>& shaderList);
    GLint GetUniformLocation(const std::string& valueName) const;
private:
    GLuint program = 0;
    mutable std::unordered_map<std::string, GLint> uniformLocationCache;
};
