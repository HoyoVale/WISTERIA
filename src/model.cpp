#include "pch.hpp"
#include "model.hpp"

Model::Model(const DefaultModelData& data, const Transform& transform)
    : data(&data),
      transform(transform)
{
}

const DefaultModelData& Model::Data() const
{
    if (this->data == nullptr)
        throw std::logic_error("Model has no model data");

    return *this->data;
}

Transform& Model::GetTransform() noexcept
{
    return this->transform;
}

const Transform& Model::GetTransform() const noexcept
{
    return this->transform;
}
