#pragma once
#include <glm/glm.hpp>

struct ModelParam{
    glm::vec3 Position = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 Scale = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 Rotation = glm::vec3(0.0f, 0.0f, 0.0f);
};

class Model{
public:
    explicit Model(const ModelParam &modelParam = {});
    ~Model();

    glm::mat4 ModelMat();
    ModelParam &Param(){ return this->modelParam; };
    template<typename T>
    void Translate(T x, T y, T z){
        this->Param().Position.x = static_cast<float>(x);
        this->Param().Position.y = static_cast<float>(y);
        this->Param().Position.z = static_cast<float>(z);
    }
    template<typename T>
    void Scale(T x, T y, T z){
        this->Param().Scale.x = static_cast<float>(x);
        this->Param().Scale.y = static_cast<float>(y);
        this->Param().Scale.z = static_cast<float>(z);
    }
    template<typename T>
    void Rotate(T x, T y, T z){
        this->Param().Rotation.x = static_cast<float>(x);
        this->Param().Rotation.y = static_cast<float>(y);
        this->Param().Rotation.z = static_cast<float>(z);
    }

    void Translate(glm::vec3 translation);
    void Scale(glm::vec3 scale);
    void Rotate(glm::vec3 rotation);
private:
    ModelParam modelParam;
};