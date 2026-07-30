#include "pch.hpp"
#include "shader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
Shader::Shader(std::string VertexPath, std::string FragmrntPath)
{
    path = new Path({VertexPath, FragmrntPath});
    shaderList.push_back(CreateShader(GL_VERTEX_SHADER, path->VertexPath));
    shaderList.push_back(CreateShader(GL_FRAGMENT_SHADER, path->FragmentPath));
}

Shader::~Shader()
{
    delete this->path;
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
    const std::string source = ReadTextFile(strShaderFile);
    const char *strFileData = source.c_str();
    glShaderSource(shader, 1, &strFileData, NULL);
    
    glCompileShader(shader);
    
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint infoLogLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
        
        GLchar *strInfoLog = new GLchar[infoLogLength + 1];
        glGetShaderInfoLog(shader, infoLogLength, NULL, strInfoLog);
        
        const char *strShaderType = NULL;
        switch(eShaderType)
        {
        case GL_VERTEX_SHADER: strShaderType = "vertex"; break;
        case GL_GEOMETRY_SHADER: strShaderType = "geometry"; break;
        case GL_FRAGMENT_SHADER: strShaderType = "fragment"; break;
        }
        
        fprintf(stderr, "Compile failure in %s shader:\n%s\n", strShaderType, strInfoLog);
        delete[] strInfoLog;
    }

	return shader;
}

Program::Program(std::vector<GLuint> shaderList)
{
    this->program = CreateProgram(shaderList);
    for (GLuint shader : shaderList) glDeleteShader(shader);
}

Program::~Program(){
    if (program != 0) {
        glDeleteProgram(program);
    }
}

GLuint Program::CreateProgram(const std::vector<GLuint> &shaderList)
{
    GLuint program = glCreateProgram();
    
    for(size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
    	glAttachShader(program, shaderList[iLoop]);
    
    glLinkProgram(program);
    
    GLint status;
    glGetProgramiv (program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint infoLogLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        
        GLchar *strInfoLog = new GLchar[infoLogLength + 1];
        glGetProgramInfoLog(program, infoLogLength, NULL, strInfoLog);
        fprintf(stderr, "Linker failure: %s\n", strInfoLog);
        delete[] strInfoLog;
    }
    
    for(size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
        glDetachShader(program, shaderList[iLoop]);

    return program;
}

void Program::Use()
{
    glUseProgram(this->GetProgram());
}

void Program::unUse()
{
    glUseProgram(0);
}

unsigned int Program::GetUniformLocation(std::string valueName)
{
    unsigned int location = glGetUniformLocation(this->program, valueName.c_str());
    if (location == -1) {
        std::cerr << "can not find uniform: " << valueName << std::endl;
        exit(1);
    }
    return location;
}

void Program::UniformMat4f(std::string valueName, glm::mat4 value)
{
    this->Use();
    glUniformMatrix4fv(GetUniformLocation(valueName), 1, GL_FALSE,glm::value_ptr(value));
    this->unUse();
}
void Program::UniformMat3f(std::string valueName, glm::mat3 value)
{
    this->Use();
    glUniformMatrix3fv(GetUniformLocation(valueName), 1, GL_FALSE,glm::value_ptr(value));
    this->unUse();
}
void Program::UniformMat2f(std::string valueName, glm::mat2 value)
{
    this->Use();
    glUniformMatrix2fv(GetUniformLocation(valueName), 1, GL_FALSE,glm::value_ptr(value));
    this->unUse();
}
void Program::Uniform1f(std::string valueName, float value)
{
    this->Use();
    glUniform1f(GetUniformLocation(valueName),value);
    this->unUse();
}
void Program::Uniform2f(std::string valueName, float x, float y)
{
    this->Use();
    glUniform2f(GetUniformLocation(valueName),x,y);
    this->unUse();
}
void Program::Uniform3f(std::string valueName, float x, float y, float z)
{
    this->Use();
    glUniform3f(GetUniformLocation(valueName),x,y,z);
    this->unUse();
}
void Program::Uniform1i(std::string valueName, int value)
{
    this->Use();
    glUniform1i(GetUniformLocation(valueName),value);
    this->unUse();
}
void Program::Uniform1ui(std::string valueName, unsigned int value)
{
    this->Use();
    glUniform1ui(GetUniformLocation(valueName),value);
    this->unUse();
}
void Program::UniformTex(Texture &texture, std::string UniformName)
{
    this->Use();
    glUniform1i(GetUniformLocation(UniformName), (int)texture.GetIndex());
    this->unUse();
}