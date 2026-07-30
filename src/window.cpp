#include "pch.hpp"
#include "window.hpp"
#include "shader.hpp"
#include "vbo.hpp"
#include "vao.hpp"
#include "ebo.hpp"
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
        -0.5f, -0.5f, 0.0f, 0.7f, 0.2f, 0.1f,
         0.5f, -0.5f, 0.0f, 0.1f, 0.7f, 0.2f,
         0.5f,  0.5f, 0.0f, 0.2f, 0.1f, 0.7f,
        -0.5f,  0.5f, 0.0f, 0.5f, 0.5f, 0.0f
    };

    unsigned int indices[] ={
        0, 1, 2,
        2, 3, 0
    };
    VAO* vao = new VAO();
    vao->Bind();
    VBO* vbo = new VBO();
    vbo->Upload(vertices, 2*3*4* sizeof(float));
    vao->BindBuffer(*vbo, {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT, true}
    });
    EBO* ebo = new EBO();
    ebo->Bind();
    ebo->Upload(indices, 2*3*sizeof(unsigned int));

    vao->unBind();

    std::string strVertexShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basicColor.vert";
    std::string strFragmentShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basicColor.frag";
    Shader* shader = new Shader(strVertexShader, strFragmentShader);
    Program* program = new Program(shader->GetShaderList());

    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        program->Use();
        vao->Bind();
        //glDrawArrays(GL_TRIANGLES, 0, 3);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        program->unUse();

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    glDisableVertexAttribArray(0);
    return false;
}
