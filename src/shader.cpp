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