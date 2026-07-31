#include "pch.hpp"
#include "shader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
const char* ShaderTypeName(GLenum shaderType)
{
    switch (shaderType)
    {
    case GL_VERTEX_SHADER: return "vertex";
    case GL_FRAGMENT_SHADER: return "fragment";
    case GL_GEOMETRY_SHADER: return "geometry";
    default: return "unknown";
    }
}
}

Shader::Shader(std::string vertexPath, std::string fragmentPath)
    : path{std::move(vertexPath), std::move(fragmentPath)}
{
    try
    {
        this->shaderList.push_back(
            CreateShader(GL_VERTEX_SHADER, this->path.VertexPath)
        );
        this->shaderList.push_back(
            CreateShader(GL_FRAGMENT_SHADER, this->path.FragmentPath)
        );
    }
    catch (...)
    {
        for (const GLuint shader : this->shaderList)
            glDeleteShader(shader);
        throw;
    }
}

Shader::~Shader()
{
    for (const GLuint shader : this->shaderList)
        glDeleteShader(shader);
}

const std::vector<GLuint>& Shader::GetShaderList() const noexcept
{
    return this->shaderList;
}

std::string Shader::ReadTextFile(const std::string& path)
{
    std::ifstream file(path);

    if (!file)
        throw std::runtime_error("Cannot open shader: " + path);

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint Shader::CreateShader(GLenum eShaderType, const std::string &strShaderFile)
{
    GLuint shader = glCreateShader(eShaderType);
    if (shader == 0)
        throw std::runtime_error("Cannot create " +
            std::string(ShaderTypeName(eShaderType)) + " shader");

    std::string source;
    try
    {
        source = ReadTextFile(strShaderFile);
    }
    catch (...)
    {
        glDeleteShader(shader);
        throw;
    }
    const char *strFileData = source.c_str();
    glShaderSource(shader, 1, &strFileData, NULL);
    
    glCompileShader(shader);
    
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint infoLogLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
        
        std::string infoLog(static_cast<std::size_t>(infoLogLength), '\0');
        glGetShaderInfoLog(shader, infoLogLength, NULL, infoLog.data());
        glDeleteShader(shader);
        throw std::runtime_error(
            "Compile failure in " +
            std::string(ShaderTypeName(eShaderType)) +
            " shader (" + strShaderFile + "):\n" + infoLog
        );
    }

	return shader;
}

Program::Program(const std::vector<GLuint>& shaderList)
{
    this->program = CreateProgram(shaderList);
}

Program::~Program(){
    if (program != 0) {
        glDeleteProgram(program);
    }
}

GLuint Program::CreateProgram(const std::vector<GLuint> &shaderList)
{
    GLuint program = glCreateProgram();
    if (program == 0)
        throw std::runtime_error("Cannot create OpenGL program");
    
    for(size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
    	glAttachShader(program, shaderList[iLoop]);
    
    glLinkProgram(program);
    
    GLint status;
    glGetProgramiv (program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint infoLogLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        
        std::string infoLog(static_cast<std::size_t>(infoLogLength), '\0');
        glGetProgramInfoLog(program, infoLogLength, NULL, infoLog.data());
        glDeleteProgram(program);
        throw std::runtime_error("Program link failure:\n" + infoLog);
    }
    
    for(size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
        glDetachShader(program, shaderList[iLoop]);

    return program;
}

void Program::Use()
{
    if (this->program == 0)
        throw std::logic_error("Cannot use an invalid OpenGL program");

    glUseProgram(this->GetProgram());
}

void Program::unUse()
{
    glUseProgram(0);
}

GLint Program::GetUniformLocation(const std::string& valueName) const
{
    const auto cached = this->uniformLocationCache.find(valueName);
    if (cached != this->uniformLocationCache.end())
    {
        if (cached->second == -1)
            throw std::runtime_error("Cannot find uniform: " + valueName);
        return cached->second;
    }

    const GLint location = glGetUniformLocation(
        this->program,
        valueName.c_str()
    );
    this->uniformLocationCache.emplace(valueName, location);

    if (location == -1)
        throw std::runtime_error("Cannot find uniform: " + valueName);

    return location;
}

void Program::UniformMat4f(const std::string& valueName, const glm::mat4& value)
{
    glUniformMatrix4fv(GetUniformLocation(valueName), 1, GL_FALSE,glm::value_ptr(value));
}
void Program::UniformMat3f(const std::string& valueName, const glm::mat3& value)
{
    glUniformMatrix3fv(GetUniformLocation(valueName), 1, GL_FALSE,glm::value_ptr(value));
}
void Program::UniformMat2f(const std::string& valueName, const glm::mat2& value)
{
    glUniformMatrix2fv(GetUniformLocation(valueName), 1, GL_FALSE,glm::value_ptr(value));
}
void Program::Uniform1f(const std::string& valueName, float value)
{
    glUniform1f(GetUniformLocation(valueName),value);
}
void Program::Uniform2f(const std::string& valueName, float x, float y)
{
    glUniform2f(GetUniformLocation(valueName),x,y);
}
void Program::Uniform3f(const std::string& valueName, float x, float y, float z)
{
    glUniform3f(GetUniformLocation(valueName),x,y,z);
}
void Program::Uniform4f(
    const std::string& valueName,
    float x,
    float y,
    float z,
    float w
)
{
    glUniform4f(GetUniformLocation(valueName),x,y,z,w);
}
void Program::Uniform1i(const std::string& valueName, int value)
{
    glUniform1i(GetUniformLocation(valueName),value);
}
void Program::Uniform1ui(const std::string& valueName, unsigned int value)
{
    glUniform1ui(GetUniformLocation(valueName),value);
}
void Program::UniformTex(const std::string& uniformName, unsigned int textureUnit)
{
    this->Uniform1i(uniformName, static_cast<int>(textureUnit));
}
