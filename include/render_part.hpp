#pragma once

#include <glm/glm.hpp>

class Material;
class Mesh;

// One drawable part of an Entity or ModelAsset. Mesh and Material are owned by
// ResourceManager; RenderPart only keeps stable non-owning references.
class RenderPart
{
public:
    RenderPart(
        Mesh& mesh,
        Material& material,
        const glm::mat4& localTransform = glm::mat4(1.0f)
    );

    Mesh& GetMesh() noexcept;
    Mesh& GetMesh() const noexcept;
    void SetMesh(Mesh& mesh) noexcept;

    Material& GetMaterial() noexcept;
    Material& GetMaterial() const noexcept;
    void SetMaterial(Material& material) noexcept;

    const glm::mat4& LocalTransform() const noexcept;
    void SetLocalTransform(const glm::mat4& localTransform);

private:
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    glm::mat4 localTransform{1.0f};
};
