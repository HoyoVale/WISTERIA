#include "pch.hpp"
#include "model.hpp"
#include <glm/gtc/matrix_transform.hpp>

Model::Model(const DefaultModelData &data, const ModelParam &modelparam)
:modelParam(modelparam),data(&data)
{
}

glm::mat4 Model::ModelMat() const
{

    glm::mat4 modelMat = glm::mat4(1.0f);
    modelMat = glm::translate(modelMat, this->modelParam.Position);
    modelMat = glm::scale(modelMat, this->modelParam.Scale);
    modelMat = glm::rotate(modelMat, glm::radians(this->modelParam.Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMat = glm::rotate(modelMat, glm::radians(this->modelParam.Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMat = glm::rotate(modelMat, glm::radians(this->modelParam.Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return modelMat;
}

const DefaultModelData& Model::Data() const
{
    if (this->data == nullptr)
        throw std::logic_error("Model has no model data");

    return *this->data;
}

void Model::Translate(glm::vec3 translation)
{
    this->Param().Position = translation;
}
void Model::Scale(glm::vec3 scale)
{
    this->Param().Scale = scale;
}
void Model::Rotate(glm::vec3 rotation)
{
    this->Param().Rotation = rotation;
}
