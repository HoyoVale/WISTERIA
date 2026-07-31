#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "vao.hpp"
#include "vbo.hpp"
#include "ebo.hpp"


struct ModelParam{
    glm::vec3 Position = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 Scale = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 Rotation = glm::vec3(0.0f, 0.0f, 0.0f);
};

template<
    typename VertexType = float,
    typename IndexType = unsigned int
>
struct ModelData{
    std::vector<VertexType> vertices;
    std::vector<IndexType> indices;
    std::vector<Layout> layout;

    std::size_t VertexBytes() const noexcept {
        return vertices.size() * sizeof(VertexType);
    }

    std::size_t IndexBytes() const noexcept {
        return indices.size() * sizeof(IndexType);
    }

    std::size_t IndexCount() const noexcept {
        return indices.size();
    }
};

using DefaultModelData = ModelData<>;

class Model{
public:
    explicit Model(
        const DefaultModelData& data,
        const ModelParam& modelParam = {}
    );
    virtual ~Model() = default;

    glm::mat4 ModelMat() const;
    const DefaultModelData& Data() const;
    ModelParam &Param(){ return this->modelParam; };
    const ModelParam &Param() const { return this->modelParam; };
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
public:
    const DefaultModelData* data = nullptr;
private:
    ModelParam modelParam;
};

