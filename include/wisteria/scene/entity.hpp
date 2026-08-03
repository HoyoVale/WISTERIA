#pragma once

#include "wisteria/animation/animator.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/rendering/render_part.hpp"
#include "wisteria/animation/pose.hpp"
#include "wisteria/core/transform.hpp"
#include <concepts>
#include <cstddef>
#include <memory>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

class MmdPhysicsAsset;
class MmdPhysicsInstance;
struct MmdPhysicsRuntimePolicy;
class PhysicsInstance;
class PhysicsWorld;

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
        const glm::mat4& localTransform = glm::mat4(1.0f),
        std::optional<std::uint32_t> morphMaterialIndex = std::nullopt
    );
    bool RemoveRenderPart(const RenderPart& part);
    void ClearRenderParts() noexcept;
    std::size_t RenderPartCount() const noexcept;
    std::span<RenderPart> RenderParts() noexcept;
    std::span<const RenderPart> RenderParts() const noexcept;

    bool IsVisible() const noexcept;
    void SetVisible(bool visible) noexcept;

    bool HasPose() const noexcept;
    Pose* TryGetPose() noexcept;
    const Pose* TryGetPose() const noexcept;
    Pose& GetPose();
    const Pose& GetPose() const;
    void SetSkeleton(const Skeleton& skeleton);

    bool HasAnimator() const noexcept;
    Animator* TryGetAnimator() noexcept;
    const Animator* TryGetAnimator() const noexcept;
    Animator& GetAnimator();
    const Animator& GetAnimator() const;

    bool HasMorphState() const noexcept;
    MorphState* TryGetMorphState() noexcept;
    const MorphState* TryGetMorphState() const noexcept;
    MorphState& GetMorphState();
    const MorphState& GetMorphState() const;
    void SetMorphSet(const MorphSet& morphSet);

    bool HasPhysicsInstance() const noexcept;
    PhysicsInstance* TryGetPhysicsInstance() noexcept;
    const PhysicsInstance* TryGetPhysicsInstance() const noexcept;
    PhysicsInstance& GetPhysicsInstance();
    const PhysicsInstance& GetPhysicsInstance() const;
    void SetPhysicsInstance(std::unique_ptr<PhysicsInstance> instance);

    // Transitional typed access for MMD-only features such as Impulse Morph.
    // Entity lifecycle itself uses PhysicsInstance and is format-agnostic.
    bool HasMmdPhysics() const noexcept;
    MmdPhysicsInstance* TryGetMmdPhysics() noexcept;
    const MmdPhysicsInstance* TryGetMmdPhysics() const noexcept;
    MmdPhysicsInstance& GetMmdPhysics();
    const MmdPhysicsInstance& GetMmdPhysics() const;
    void SetMmdPhysics(
        PhysicsWorld& world,
        const MmdPhysicsAsset& physics
    );
    void SetMmdPhysics(
        PhysicsWorld& world,
        const MmdPhysicsAsset& physics,
        const MmdPhysicsRuntimePolicy& policy
    );
    void PrePhysicsUpdate(float deltaTime);
    void PreparePhysicsSubstep(float alpha, float fixedTimeStep);
    void ObservePhysicsSubstep(float fixedTimeStep);
    void PostPhysicsUpdate();
    void SolveAfterPhysicsPose();
    void ResetPhysicsToCurrentPose();
    void AppendPhysicsDebugLines(std::vector<PhysicsDebugLine>& lines);

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
    void Update(float deltaTime);
    void UpdateBehaviours(float deltaTime);
    void DispatchEvent(const Event& event);

private:
    void FlushPendingBehaviourRemovals() noexcept;

    Transform transform;
    std::unique_ptr<Pose> pose;
    std::unique_ptr<MorphState> morphState;
    std::unique_ptr<Animator> animator;
    std::unique_ptr<PhysicsInstance> physicsInstance;
    std::vector<RenderPart> renderParts;
    std::vector<std::unique_ptr<Behaviour>> behaviours;
    std::vector<Behaviour*> pendingBehaviourRemovals;
    bool processingBehaviours = false;
    bool clearBehavioursRequested = false;
    bool physicsResetPending = false;
    std::uint64_t observedAnimatorDiscontinuityRevision = 0U;
    bool visible = true;
};
