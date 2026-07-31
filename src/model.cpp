#include "pch.hpp"
#include "model.hpp"
#include <utility>

Model::Model(DefaultModelData data)
    : data(std::move(data))
{
}

const DefaultModelData& Model::Data() const noexcept
{
    return this->data;
}
