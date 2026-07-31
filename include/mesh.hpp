#pragma once
#include "model.hpp"
#include "vao.hpp"
#include "vbo.hpp"
#include "ebo.hpp"

class Mesh{
public:
    Mesh(const Model &_model);
    ~Mesh();

    void Attach();
    void Draw();
private:
    const Model* model = nullptr;
    VAO* vao = nullptr;
    VBO* vbo = nullptr;
    EBO* ebo = nullptr;
};
