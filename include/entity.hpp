#pragma once

#include "behaviour.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "transform.hpp"
#include <concepts>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

class Entity {
public:
    Entity(
        Mesh& mesh,
        Material& material,
        const Transform& transform = {}
    );
    ~Entity();

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) = delete;
    Entity& operator=(Entity&&) = delete;

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

    template<typename T, typename... Arguments>
        requires std::derived_from<T, Behaviour>
    T& AddBehaviour(Arguments&&... arguments)
    {
        auto behaviour = std::make_unique<T>(
            std::forward<Arguments>(arguments)...
        );
        T& result = *behaviour;
        this->behaviours.emplace_back(std::move(behaviour));
        result.OnAttach(*this);
        return result;
    }

    bool RemoveBehaviour(Behaviour& behaviour);
    void ClearBehaviours() noexcept;

    template<typename T>
        requires std::derived_from<T, Behaviour>
    T* FindBehaviour() noexcept
    {
        for (const std::unique_ptr<Behaviour>& behaviour : this->behaviours)
        {
            if (T* result = dynamic_cast<T*>(behaviour.get()))
                return result;
        }
        return nullptr;
    }

    template<typename T>
        requires std::derived_from<T, Behaviour>
    const T* FindBehaviour() const noexcept
    {
        for (const std::unique_ptr<Behaviour>& behaviour : this->behaviours)
        {
            if (const T* result = dynamic_cast<const T*>(behaviour.get()))
                return result;
        }
        return nullptr;
    }

    std::size_t BehaviourCount() const noexcept;
    void UpdateBehaviours(float deltaTime);
    void DispatchEvent(const Event& event);

private:
    void FlushPendingBehaviourRemovals() noexcept;

    Transform transform;
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    std::vector<std::unique_ptr<Behaviour>> behaviours;
    std::vector<Behaviour*> pendingBehaviourRemovals;
    bool processingBehaviours = false;
    bool clearBehavioursRequested = false;
    bool visible = true;
};
