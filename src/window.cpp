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
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    GLint depthBits = 0;
    glGetIntegerv(GL_DEPTH_BITS, &depthBits);

    std::cout << "Depth bits: "
            << depthBits
            << '\n';
            
    float vertices[] = {
        -0.5f,-0.5f, 0.5f, 0.7f, 0.2f, 0.1f,
         0.5f,-0.5f, 0.5f, 0.1f, 0.7f, 0.2f,
         0.5f, 0.5f, 0.5f, 0.2f, 0.1f, 0.7f,
        -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f,

        -0.5f,-0.5f,-0.5f, 0.7f, 0.2f, 0.1f,
         0.5f,-0.5f,-0.5f, 0.1f, 0.7f, 0.2f,
         0.5f, 0.5f,-0.5f, 0.2f, 0.1f, 0.7f,
        -0.5f, 0.5f,-0.5f, 0.5f, 0.5f, 0.0f
    };

    unsigned int indices[] ={
        0, 1, 2,
        2, 3, 0,
        4, 5, 6,
        6, 7, 4,
        4, 5, 1,
        1, 0, 4,
        7, 6, 2,
        2, 3, 7,
        5, 6, 2,
        2, 1, 5,
        7, 4, 0,
        0, 3, 7
    };

    VAO* vao = new VAO();
    vao->Bind();
    VBO* vbo = new VBO();
    vbo->Upload(vertices, 8*6* sizeof(float));
    vao->BindBuffer(*vbo, {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT}
    });
    EBO* ebo = new EBO();
    ebo->Bind();
    ebo->Upload(indices, 12*3*sizeof(unsigned int));
    vao->unBind();

    std::string strVertexShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basicPlus.vert";
    std::string strFragmentShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basicPlus.frag";
    Shader* shader = new Shader(strVertexShader, strFragmentShader);
    Program* program = new Program(shader->GetShaderList());

    glm::vec3 objectPosition(0.0f, 0.0f, 0.0f);
    glm::vec3 objectScale(1.0f, 1.0f, 1.0f);

    glm::mat4 model(1.0f);

    // 平移
    model = glm::translate(model, objectPosition);

    // 缩放
    model = glm::scale(model, objectScale);
    // glm::mat4 view = glm::mat4(1.0f);
    // glm::mat4 projection = glm::mat4(1.0f);
    // glm::mat4 transform = glm::mat4(1.0f);

    float x = 0, r = 0.01;
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glClear(GL_DEPTH_BUFFER_BIT);
        model = glm::rotate(model,glm::radians(2*r),glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model,glm::radians(r*r),glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model,glm::radians(r),glm::vec3(0.0f, 0.0f, 1.0f));
        if(x >= PI || x <= -PI) r=-r;
        program->Use();
        program->UniformMat4f("transform", model);

        vao->Bind();
        //glDrawArrays(GL_TRIANGLES, 0, 3);
        glDrawElements(GL_TRIANGLES, 12*3, GL_UNSIGNED_INT, nullptr);
        program->unUse();

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    glDisableVertexAttribArray(0);
    return false;
}
