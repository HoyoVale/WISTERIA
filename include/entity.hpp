#pragma once

#include "behaviour.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "render_part.hpp"
#include "transform.hpp"
#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

class Entity {
public:
    explicit Entity(const Transform& transform = {});
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

    Mesh& GetMesh();
    const Mesh& GetMesh() const;
    void SetMesh(Mesh& mesh);

    Material& GetMaterial();
    const Material& GetMaterial() const;
    void SetMaterial(Material& material);

    RenderPart& AddRenderPart(
        Mesh& mesh,
        Material& material,
        const glm::mat4& localTransform = glm::mat4(1.0f)
    );
    bool RemoveRenderPart(const RenderPart& part);
    void ClearRenderParts() noexcept;
    std::size_t RenderPartCount() const noexcept;
    std::span<RenderPart> RenderParts() noexcept;
    std::span<const RenderPart> RenderParts() const noexcept;

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
    std::vector<RenderPart> renderParts;
    std::vector<std::unique_ptr<Behaviour>> behaviours;
    std::vector<Behaviour*> pendingBehaviourRemovals;
    bool processingBehaviours = false;
    bool clearBehavioursRequested = false;
    bool visible = true;
};
