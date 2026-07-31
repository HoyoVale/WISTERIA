#include "pch.hpp"
#include "model.hpp"

Model::Model(const DefaultModelData& data)
    : data(&data)
{
}

const DefaultModelData& Model::Data() const
{
    if (this->data == nullptr)
        throw std::logic_error("Model has no model data");

    return *this->data;
}
