#include "pch.hpp"
#include "window.hpp"
#include "shader.hpp"
#include "vbo.hpp"
#include "vao.hpp"
#include "ebo.hpp"
#include "texture.hpp"
#include "camera.hpp"
#include <stb_image.h>
#include <iostream>
#include <vector>
#include <glad/gl.h>

void FramebufferSizeCallback(
    GLFWwindow* window,
    int width,
    int height)
{
    glViewport(0, 0, width, height);
}

Window::Window(int width, int height)
{
    this->size = new WindowSize({width, height});

    CameraParam c(
        glm::vec3(0.0f,0.0f,3.0f),
        glm::vec3(0.0f,0.0f,0.0f),
        glm::vec3(0.0f,1.0f,0.0f)
    );
    this->camera = new Camera(c);
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

    if(!this->init()) 
    {
        std::cerr << "window init failed!";
        return;
    }       
}

Window::~Window(){
    delete this->size;
    delete this->camera;
    glfwDestroyWindow(this->window);
    glfwTerminate();
}

bool Window::init()
{
    glfwMakeContextCurrent(this->GetGLFWwindow());

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "Failed to load OpenGL functions\n";
        glfwTerminate();
        return false;
    }

    glfwSetFramebufferSizeCallback(this->window, FramebufferSizeCallback);
    this->computeParam();
    // TODO renderer init
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    //glCullFace(GL_BACK); 

    // GLint depthBits = 0;
    // glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    // std::cout << "Depth bits: "<< depthBits<< '\n';
    return true;
}

void Window::computeParam()
{
    int framebufferWidth = 0;
    int framebufferHeight = 0;

    glfwGetFramebufferSize(this->window,&framebufferWidth,&framebufferHeight);
    glViewport(0, 0, framebufferWidth,framebufferHeight);
    this->aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    this->projection = glm::perspective(glm::radians(45.0f),this->aspect, 0.1f, 1000.0f);
}

bool Window::Run()
{
    // 每个顶点：position(3) + color(3) + texCoord(2) = 8 个 float。
    // 每个面使用独立顶点，保证六个面的纹理坐标互不冲突。
    float vertices[] = {
        // front (+Z)
        -0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,

        // back (-Z)
        -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,

        // left (-X)
        -0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,

        // right (+X)
         0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,

        // top (+Y)
        -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,

        // bottom (-Y)
        -0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f
    };

    unsigned int indices[] = {
         0,  1,  2,   2,  3,  0, // front
         4,  5,  6,   6,  7,  4, // back
         8,  9, 10,  10, 11,  8, // left
        12, 13, 14,  14, 15, 12, // right
        16, 17, 18,  18, 19, 16, // top
        20, 21, 22,  22, 23, 20  // bottom
    };

    VAO* vao = new VAO();
    vao->Bind();
    VBO* vbo = new VBO();
    vbo->Upload(vertices, sizeof(vertices));
    vao->BindBuffer(*vbo, {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT}
    });
    EBO* ebo = new EBO();
    ebo->Bind();
    ebo->Upload(indices, sizeof(indices));
    vao->unBind();

    std::string strVertexShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basicTex.vert";
    std::string strFragmentShader = "C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\shaders\\basicTex.frag";
    Shader* shader = new Shader(strVertexShader, strFragmentShader);
    Program* program = new Program(shader->GetShaderList());

    Texture* texture = new Texture();
    texture->Bind();
    texture->Upload("C:\\Users\\hoyo\\Desktop\\temp\\learn\\FGGP\\assets\\textures\\icon.png");
    program->UniformTex(*texture, "texture");

    glm::vec3 objectPosition(0.0f, 0.0f, 0.0f);
    glm::vec3 objectScale(1.0f, 1.0f, 1.0f);

    float r = 0.0f, t=0.01f;
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->computeParam();
        glm::mat4 model(1.0f);
        model = glm::translate(model, objectPosition);
        model = glm::scale(model, objectScale);
        model = glm::rotate(model, glm::radians(r), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(2*r), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(3*r), glm::vec3(0.0f, 0.0f, 1.0f));
        r+=t;
        if(r<=-180.0 or r>= 180.0) t=-t;
        glm::mat4 transform = this->Projection() *this->View() *model;

        program->UniformMat4f("transform", transform);
        
        // 绘制
        program->Use();
        vao->Bind();
        //glDrawArrays(GL_TRIANGLES, 0, 3);
        //glDepthMask(GL_FALSE);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(sizeof(indices) / sizeof(indices[0])),
            GL_UNSIGNED_INT,
            nullptr
        );
        //glDepthMask(GL_TRUE);
        program->unUse();

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    
    return false;
}
