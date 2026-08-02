#pragma once

#include <btBulletDynamicsCommon.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace PhysicsBulletConversion
{
btVector3 ToBullet(const glm::vec3& value) noexcept;
glm::vec3 FromBullet(const btVector3& value) noexcept;

btQuaternion ToBullet(const glm::quat& value) noexcept;
glm::quat FromBullet(const btQuaternion& value) noexcept;

btTransform ToBullet(
    const glm::vec3& position,
    const glm::quat& rotation
) noexcept;

glm::mat4 FromBullet(const btTransform& transform) noexcept;
}
