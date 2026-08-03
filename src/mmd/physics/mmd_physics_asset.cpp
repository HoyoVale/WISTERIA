#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/physics/mmd_physics_asset.hpp"

#include <glm/gtc/matrix_access.hpp>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const glm::vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const glm::quat& value) noexcept
{
    return IsFinite(value.w) && IsFinite(value.x) &&
        IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const glm::mat4& value) noexcept
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!IsFinite(value[column][row]))
                return false;
        }
    }
    return true;
}

bool IsValidRigidBodyIndex(
    RigidBodyIndex index,
    std::size_t rigidBodyCount
) noexcept
{
    return index == InvalidRigidBodyIndex ||
        static_cast<std::size_t>(index) < rigidBodyCount;
}
}

MmdPhysicsAsset::MmdPhysicsAsset(
    std::vector<MmdRigidBodyDefinition> rigidBodies,
    std::vector<MmdJointDefinition> joints
)
    : rigidBodies(std::move(rigidBodies)),
      joints(std::move(joints))
{
    if (this->rigidBodies.size() >
        static_cast<std::size_t>(InvalidRigidBodyIndex))
    {
        throw std::length_error("MMD physics asset has too many rigid bodies");
    }

    this->rigidBodyIndices.reserve(this->rigidBodies.size());
    for (std::size_t index = 0; index < this->rigidBodies.size(); ++index)
    {
        MmdRigidBodyDefinition& body = this->rigidBodies[index];
        if (body.name.empty())
            throw std::invalid_argument("MMD rigid-body name must not be empty");
        if (body.collisionGroup >= 16U)
        {
            throw std::invalid_argument(
                "MMD rigid-body collision group is out of range"
            );
        }
        if (static_cast<std::uint8_t>(body.shape) >
                static_cast<std::uint8_t>(MmdRigidBodyShape::Capsule) ||
            static_cast<std::uint8_t>(body.mode) >
                static_cast<std::uint8_t>(MmdRigidBodyMode::PhysicsWithBone) ||
            !IsFinite(body.size) ||
            body.size.x < 0.0f || body.size.y < 0.0f || body.size.z < 0.0f ||
            !IsFinite(body.position) || !IsFinite(body.rotation) ||
            !IsFinite(body.mass) || body.mass < 0.0f ||
            !IsFinite(body.linearDamping) || body.linearDamping < 0.0f ||
            !IsFinite(body.angularDamping) || body.angularDamping < 0.0f ||
            !IsFinite(body.restitution) || body.restitution < 0.0f ||
            !IsFinite(body.friction) || body.friction < 0.0f ||
            !IsFinite(body.modelBindTransform) ||
            !IsFinite(body.boneToBody) || !IsFinite(body.bodyToBone))
        {
            throw std::invalid_argument(
                "MMD rigid-body definition contains invalid values"
            );
        }
        const float rotationLengthSquared = glm::dot(body.rotation, body.rotation);
        if (!std::isfinite(rotationLengthSquared) ||
            rotationLengthSquared <= 0.000001f)
        {
            throw std::invalid_argument(
                "MMD rigid-body rotation is invalid"
            );
        }
        body.rotation = glm::normalize(body.rotation);
        this->rigidBodyIndices.emplace(
            body.name,
            static_cast<RigidBodyIndex>(index)
        );
    }

    for (MmdJointDefinition& joint : this->joints)
    {
        if (joint.name.empty())
            throw std::invalid_argument("MMD joint name must not be empty");
        if (static_cast<std::uint8_t>(joint.type) >
            static_cast<std::uint8_t>(MmdJointType::Hinge))
        {
            throw std::invalid_argument("MMD joint type is invalid");
        }
        if (!IsValidRigidBodyIndex(joint.bodyA, this->rigidBodies.size()) ||
            !IsValidRigidBodyIndex(joint.bodyB, this->rigidBodies.size()) ||
            (joint.bodyA == InvalidRigidBodyIndex &&
                joint.bodyB == InvalidRigidBodyIndex))
        {
            throw std::invalid_argument(
                "MMD joint references an invalid rigid body"
            );
        }
        if (!IsFinite(joint.position) || !IsFinite(joint.rotation) ||
            !IsFinite(joint.linearLower) || !IsFinite(joint.linearUpper) ||
            !IsFinite(joint.angularLower) || !IsFinite(joint.angularUpper) ||
            !IsFinite(joint.linearSpring) || !IsFinite(joint.angularSpring) ||
            !IsFinite(joint.modelBindTransform))
        {
            throw std::invalid_argument(
                "MMD joint definition contains invalid values"
            );
        }
        const float rotationLengthSquared = glm::dot(joint.rotation, joint.rotation);
        if (!std::isfinite(rotationLengthSquared) ||
            rotationLengthSquared <= 0.000001f)
        {
            throw std::invalid_argument("MMD joint rotation is invalid");
        }
        joint.rotation = glm::normalize(joint.rotation);
        if (joint.linearLower.x > joint.linearUpper.x ||
            joint.linearLower.y > joint.linearUpper.y ||
            joint.linearLower.z > joint.linearUpper.z ||
            joint.angularLower.x > joint.angularUpper.x ||
            joint.angularLower.y > joint.angularUpper.y ||
            joint.angularLower.z > joint.angularUpper.z)
        {
            throw std::invalid_argument(
                "MMD joint lower limit exceeds its upper limit"
            );
        }
    }
}

std::size_t MmdPhysicsAsset::RigidBodyCount() const noexcept
{
    return this->rigidBodies.size();
}

std::span<const MmdRigidBodyDefinition> MmdPhysicsAsset::RigidBodies() const noexcept
{
    return this->rigidBodies;
}

const MmdRigidBodyDefinition& MmdPhysicsAsset::RigidBodyAt(
    RigidBodyIndex index
) const
{
    if (static_cast<std::size_t>(index) >= this->rigidBodies.size())
        throw std::out_of_range("MMD rigid-body index is out of range");
    return this->rigidBodies[index];
}

std::optional<RigidBodyIndex> MmdPhysicsAsset::FindRigidBody(
    std::string_view name
) const noexcept
{
    const auto iterator = this->rigidBodyIndices.find(std::string(name));
    return iterator == this->rigidBodyIndices.end()
        ? std::nullopt
        : std::optional<RigidBodyIndex>(iterator->second);
}

std::size_t MmdPhysicsAsset::JointCount() const noexcept
{
    return this->joints.size();
}

std::span<const MmdJointDefinition> MmdPhysicsAsset::Joints() const noexcept
{
    return this->joints;
}

const MmdJointDefinition& MmdPhysicsAsset::JointAt(std::size_t index) const
{
    if (index >= this->joints.size())
        throw std::out_of_range("MMD joint index is out of range");
    return this->joints[index];
}
