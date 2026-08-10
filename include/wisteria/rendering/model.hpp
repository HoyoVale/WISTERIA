#pragma once

#include "wisteria/rendering/render_device.hpp"

#include <glm/glm.hpp>

#include <type_traits>
#include <vector>

#include "wisteria/rendering/vbo.hpp"

namespace wisteria
{
template<
    typename VertexType = float,
    typename IndexType = unsigned int
>
struct ModelData {
    static_assert(
        std::is_integral_v<IndexType> &&
        std::is_unsigned_v<IndexType> &&
        !std::is_same_v<std::remove_cv_t<IndexType>, bool> &&
        (sizeof(IndexType) == 1 || sizeof(IndexType) == 2 || sizeof(IndexType) == 4),
        "IndexType must be an unsigned 8-bit, 16-bit, or 32-bit integer type"
    );

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

    // R2.0 Phase 0C Step 2: engine-semantic index format. The OpenGL backend
    // maps this to GL_UNSIGNED_*; the CPU asset layer never exposes GLenum.
    static constexpr IndexFormat IndexFormatValue() noexcept {
        if constexpr (sizeof(IndexType) == 1)
            return IndexFormat::Uint8;
        else if constexpr (sizeof(IndexType) == 2)
            return IndexFormat::Uint16;
        else
            return IndexFormat::Uint32;
    }
};

using DefaultModelData = ModelData<>;

class Model {
public:
    explicit Model(DefaultModelData data);
    virtual ~Model() = default;

    const DefaultModelData& Data() const noexcept;

private:
    DefaultModelData data;
};
}  // namespace wisteria
