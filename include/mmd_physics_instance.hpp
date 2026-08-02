#pragma once

#include "mmd_physics_asset.hpp"
#include "physics_types.hpp"
#include <cstddef>
#include <limits>
#include <vector>

class PhysicsWorld;
class Pose;
class Transform;

// Per-Entity runtime bridge between immutable PMX physics metadata and the
// shared scene PhysicsWorld. Bullet objects remain owned by PhysicsWorld;
// this object owns only safe WISTERIA handles and MMD synchronization state.
class MmdPhysicsInstance
{
public:
    MmdPhysicsInstance(
        PhysicsWorld& world,
        const MmdPhysicsAsset& asset,
        Pose& pose,
        const Transform& transform
    );
    ~MmdPhysicsInstance();

    MmdPhysicsInstance(const MmdPhysicsInstance&) = delete;
    MmdPhysicsInstance& operator=(const MmdPhysicsInstance&) = delete;
    MmdPhysicsInstance(MmdPhysicsInstance&&) = delete;
    MmdPhysicsInstance& operator=(MmdPhysicsInstance&&) = delete;

    std::size_t RigidBodyCount() const noexcept;
    std::size_t ConstraintCount() const noexcept;
    PhysicsBodyHandle BodyHandleAt(RigidBodyIndex index) const;
    PhysicsBodyState BodyStateAt(RigidBodyIndex index) const;
    void ApplyCentralImpulse(RigidBodyIndex index, const glm::vec3& impulse);
    void ApplyTorqueImpulse(RigidBodyIndex index, const glm::vec3& impulse);

    void PrePhysicsUpdate(const Transform& transform, float deltaTime);
    void PostPhysicsUpdate(const Transform& transform);
    void ResetToPose(const Transform& transform);

private:
    struct RuntimeBody
    {
        const MmdRigidBodyDefinition* definition = nullptr;
        PhysicsBodyHandle handle{};
        glm::vec3 lastAnimatedPosition{0.0f};
        glm::quat lastAnimatedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool hasAnimatedTransform = false;
    };

    void DestroyRuntime() noexcept;

    PhysicsWorld* world = nullptr;
    const MmdPhysicsAsset* asset = nullptr;
    Pose* pose = nullptr;
    std::vector<RuntimeBody> rigidBodies;
    std::vector<PhysicsConstraintHandle> constraints;
    std::vector<std::size_t> drivenRuntimeBodyByBone;
    std::vector<glm::mat4> localMatrixScratch;
    std::vector<glm::mat4> globalMatrixScratch;
};
