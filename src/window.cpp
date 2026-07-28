#include "pch.hpp"
#include "window.hpp"
#include <iostream>
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

Window::~Window(){}

bool Window::Run()
{
    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "[ERROR]GLAD2 initialization failed!" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(this->GetGLFWwindow());

    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }

    return false;
}

