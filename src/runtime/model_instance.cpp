#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/model_instance.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <stdexcept>
#include <unordered_set>

namespace wisteria
{
ModelInstance::ModelInstance(
    const ModelAsset& asset,
    std::unique_ptr<IModelRuntimeDriver> runtime
)
    : asset(&asset), runtime(std::move(runtime))
{
    if (this->runtime == nullptr ||
        !this->runtime->NeedsDynamicVertexUpload())
    {
        return;
    }

    std::unordered_set<const Mesh*> visited;
    for (const RenderPart& part : asset.Parts())
    {
        const Mesh* source = &part.GetMesh();
        if (!visited.emplace(source).second)
            continue;
        std::unique_ptr<Mesh> clone = source->CloneForInstance();
        Mesh* clonePointer = clone.get();
        clonePointer->SetDynamicVertexProvider(
            [this](Mesh& mesh)
            {
                this->UploadDynamicVertices(mesh);
            }
        );
        this->meshMap.emplace(source, clonePointer);
        this->instanceMeshes.emplace_back(std::move(clone));
    }
}

const ModelAsset& ModelInstance::Asset() const noexcept
{
    return *this->asset;
}

bool ModelInstance::HasRuntime() const noexcept
{
    return this->runtime != nullptr;
}

IModelRuntimeDriver* ModelInstance::TryGetRuntime() noexcept
{
    return this->runtime.get();
}

const IModelRuntimeDriver* ModelInstance::TryGetRuntime() const noexcept
{
    return this->runtime.get();
}

MmdRuntimeModel* ModelInstance::TryGetMmdRuntime() noexcept
{
    return dynamic_cast<MmdRuntimeModel*>(this->runtime.get());
}

const MmdRuntimeModel* ModelInstance::TryGetMmdRuntime() const noexcept
{
    return dynamic_cast<const MmdRuntimeModel*>(this->runtime.get());
}

Mesh& ModelInstance::ResolveMesh(const Mesh& assetMesh)
{
    const auto iterator = this->meshMap.find(&assetMesh);
    return iterator == this->meshMap.end()
        ? const_cast<Mesh&>(assetMesh)
        : *iterator->second;
}

const Mesh& ModelInstance::ResolveMesh(const Mesh& assetMesh) const
{
    const auto iterator = this->meshMap.find(&assetMesh);
    return iterator == this->meshMap.end() ? assetMesh : *iterator->second;
}

std::size_t ModelInstance::InstanceMeshCount() const noexcept
{
    return this->instanceMeshes.size();
}

void ModelInstance::Update(float deltaTime)
{
    if (this->runtime != nullptr)
        this->runtime->Update(deltaTime);
}

void ModelInstance::Reset()
{
    if (this->runtime != nullptr)
        this->runtime->Reset();
}

void ModelInstance::UploadDynamicVertices(Mesh& mesh)
{
    if (this->runtime == nullptr)
        return;
    const ModelVertexFrame frame = this->runtime->VertexFrame();
    if (frame.positions.empty() || frame.normals.empty())
        return;
    if (frame.positions.size() != frame.normals.size())
        throw std::logic_error("Runtime vertex frame is inconsistent");

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
    for (std::uint32_t index : sourceIndices)
    {
        if (index >= frame.positions.size())
        {
            throw std::out_of_range(
                "Instance mesh references an invalid runtime vertex"
            );
        }
        positions.push_back(frame.positions[index]);
        normals.push_back(frame.normals[index]);
    }
    mesh.UploadDynamicVertices(positions, normals);
}
}  // namespace wisteria
