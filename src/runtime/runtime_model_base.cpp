#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <stdexcept>
#include <vector>

namespace wisteria
{
void RuntimeModelBase::UploadDynamicVertices(Mesh& mesh)
{
    const ModelVertexFrame frame = this->VertexFrame();
    if (frame.positions.empty() || frame.normals.empty())
        return;
    if (frame.positions.size() != frame.normals.size())
    {
        throw std::logic_error(
            "Runtime vertex frame position/normal counts do not match"
        );
    }

    const std::span<const std::uint32_t> sourceIndices =
        mesh.SourceVertexIndices();
    if (sourceIndices.empty())
    {
        mesh.UploadDynamicVertices(frame.positions, frame.normals);
        return;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    positions.reserve(sourceIndices.size());
    normals.reserve(sourceIndices.size());
    for (const std::uint32_t globalIndex : sourceIndices)
    {
        if (globalIndex >= frame.positions.size())
        {
            throw std::out_of_range(
                "Mesh source vertex index exceeds runtime vertex frame"
            );
        }
        positions.push_back(frame.positions[globalIndex]);
        normals.push_back(frame.normals[globalIndex]);
    }
    mesh.UploadDynamicVertices(positions, normals);
}
}  // namespace wisteria
