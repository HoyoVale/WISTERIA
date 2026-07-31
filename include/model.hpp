#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "vao.hpp"
#include "vbo.hpp"
#include "ebo.hpp"
#include "transform.hpp"

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
        const Transform& transform = {}
    );
    virtual ~Model() = default;

    const DefaultModelData& Data() const;

    Transform& GetTransform() noexcept;
    const Transform& GetTransform() const noexcept;

private:
    const DefaultModelData* data = nullptr;
    Transform transform;
};

