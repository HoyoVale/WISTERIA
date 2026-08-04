#pragma once

#include "wisteria/animation/bone.hpp"
#include "wisteria/mmd/physics/mmd_physics_types.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wisteria
{
enum class MmdRigidBodyShape : std::uint8_t
{
    Sphere = 0,
    Box = 1,
    Capsule = 2
};

enum class MmdRigidBodyMode : std::uint8_t
{
    FollowBone = 0,
    Physics = 1,
    PhysicsWithBone = 2
};

enum class MmdJointType : std::uint8_t
{
    Spring6Dof = 0,
    SixDof = 1,
    PointToPoint = 2,
    ConeTwist = 3,
    Slider = 4,
    Hinge = 5
};

struct MmdRigidBodyDefinition
{
    std::string name;
    BoneIndex bone = InvalidBoneIndex;
    std::uint8_t collisionGroup = 0U;
    std::uint16_t nonCollisionMask = 0U;
    MmdRigidBodyShape shape = MmdRigidBodyShape::Sphere;
    glm::vec3 size{0.0f};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float mass = 0.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    MmdRigidBodyMode mode = MmdRigidBodyMode::FollowBone;
    glm::mat4 modelBindTransform{1.0f};
    glm::mat4 boneToBody{1.0f};
    glm::mat4 bodyToBone{1.0f};
};

struct MmdJointDefinition
{
    std::string name;
    MmdJointType type = MmdJointType::Spring6Dof;
    RigidBodyIndex bodyA = InvalidRigidBodyIndex;
    RigidBodyIndex bodyB = InvalidRigidBodyIndex;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearLower{0.0f};
    glm::vec3 linearUpper{0.0f};
    glm::vec3 angularLower{0.0f};
    glm::vec3 angularUpper{0.0f};
    glm::vec3 linearSpring{0.0f};
    glm::vec3 angularSpring{0.0f};
    glm::mat4 modelBindTransform{1.0f};
};

// Immutable model-level PMX physics metadata. Runtime Bullet objects are
// owned by the Saba physics world inside SabaMmdRuntimeModel.
class MmdPhysicsAsset
{
public:
    MmdPhysicsAsset(
        std::vector<MmdRigidBodyDefinition> rigidBodies,
        std::vector<MmdJointDefinition> joints
    );

    std::size_t RigidBodyCount() const noexcept;
    std::span<const MmdRigidBodyDefinition> RigidBodies() const noexcept;
    const MmdRigidBodyDefinition& RigidBodyAt(RigidBodyIndex index) const;
    std::optional<RigidBodyIndex> FindRigidBody(
        std::string_view name
    ) const noexcept;

    std::size_t JointCount() const noexcept;
    std::span<const MmdJointDefinition> Joints() const noexcept;
    const MmdJointDefinition& JointAt(std::size_t index) const;

private:
    std::vector<MmdRigidBodyDefinition> rigidBodies;
    std::vector<MmdJointDefinition> joints;
    std::unordered_map<std::string, RigidBodyIndex> rigidBodyIndices;
};
}  // namespace wisteria
