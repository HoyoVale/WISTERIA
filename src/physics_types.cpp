#include "physics_types.hpp"

PhysicsShapeDesc PhysicsShapeDesc::Sphere(float radius) noexcept
{
    return PhysicsShapeDesc{
        PhysicsShapeKind::Sphere,
        glm::vec3(radius, 0.0f, 0.0f)
    };
}

PhysicsShapeDesc PhysicsShapeDesc::Box(
    const glm::vec3& halfExtents
) noexcept
{
    return PhysicsShapeDesc{
        PhysicsShapeKind::Box,
        halfExtents
    };
}

PhysicsShapeDesc PhysicsShapeDesc::Capsule(
    float radius,
    float cylinderHeight
) noexcept
{
    return PhysicsShapeDesc{
        PhysicsShapeKind::Capsule,
        glm::vec3(radius, cylinderHeight, 0.0f)
    };
}

bool PhysicsBodyHandle::IsValid() const noexcept
{
    return index != std::numeric_limits<std::uint32_t>::max() &&
        generation != 0;
}


bool PhysicsConstraintHandle::IsValid() const noexcept
{
    return this->index != std::numeric_limits<std::uint32_t>::max() &&
        this->generation != 0U;
}
