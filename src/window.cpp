#include "pch.hpp"
#include "window.hpp"
#include "shader.hpp"
#include "vbo.hpp"
#include "vao.hpp"
#include <iostream>
#include <vector>
#include <glad/gl.h>

Window::Window(int width, int height)
{
    this->size = new WindowSize({width, height});

    if(!glfwInit())
        std::cerr << "[ERROR]GLFW initialization failed!" << std::endl;

    window = glfwCreateWindow(
        this->size->width,
        this->size->height,
        "FLORAL WISTERIA",
        NULL,
        NULL
    );

    if(!window)
    {
        glfwTerminate();
        std::cerr << "[ERROR]Window initialization failed!" << std::endl;
    }
}

Window::~Window(){
    delete this->size;
    glfwDestroyWindow(this->window);
    glfwTerminate();
}

bool Window::Run()
{
    glfwMakeContextCurrent(this->GetGLFWwindow());

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "Failed to load OpenGL functions\n";
        glfwTerminate();
        return false;
    }

    float vertices[] = {
        -0.5f, -0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, 0.0f, 1.0f,
         0.0f,  0.5f, 0.0f, 1.0f
    };
    VAO* vao = new VAO();
    vao->Bind();
    VBO* vbo = new VBO();
    vbo->Upload(vertices, 12 * sizeof(float));
    vao->BindBuffer(*vbo, {{"position", 4, FLOAT}});
    std::string strVertexShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basic.vert";
    std::string strFragmentShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basic.frag";
    Shader* shader = new Shader(strVertexShader, strFragmentShader);
    Program* program = new Program(shader->GetShaderList());

    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        program->Use();
        vao->Bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        program->unUse();

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    glDisableVertexAttribArray(0);
    return false;
}
