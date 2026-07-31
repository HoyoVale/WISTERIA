#pragma once

#include "material.hpp"
#include "mesh.hpp"
#include "transform.hpp"

class Entity {
public:
    Entity(
        Mesh& mesh,
        Material& material,
        const Transform& transform = {}
    );

    Transform& GetTransform() noexcept;
    const Transform& GetTransform() const noexcept;

    Mesh& GetMesh() noexcept;
    const Mesh& GetMesh() const noexcept;
    void SetMesh(Mesh& mesh) noexcept;

    Material& GetMaterial() noexcept;
    const Material& GetMaterial() const noexcept;
    void SetMaterial(Material& material) noexcept;

    bool IsVisible() const noexcept;
    void SetVisible(bool visible) noexcept;

private:
    Transform transform;
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    bool visible = true;
};
