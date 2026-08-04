#include "wisteria/physics/physics_bullet_conversion.hpp"
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wisteria
{
namespace PhysicsBulletConversion
{
btVector3 ToBullet(const glm::vec3& value) noexcept
{
    return btVector3(value.x, value.y, value.z);
}

glm::vec3 FromBullet(const btVector3& value) noexcept
{
    return glm::vec3(value.x(), value.y(), value.z());
}

btQuaternion ToBullet(const glm::quat& value) noexcept
{
    const glm::quat normalized = glm::normalize(value);
    return btQuaternion(
        normalized.x,
        normalized.y,
        normalized.z,
        normalized.w
    );
}

glm::quat FromBullet(const btQuaternion& value) noexcept
{
    return glm::normalize(glm::quat(
        value.w(),
        value.x(),
        value.y(),
        value.z()
    ));
}

btTransform ToBullet(
    const glm::vec3& position,
    const glm::quat& rotation
) noexcept
{
    return btTransform(ToBullet(rotation), ToBullet(position));
}

glm::mat4 FromBullet(const btTransform& transform) noexcept
{
    return glm::translate(
        glm::mat4(1.0f),
        FromBullet(transform.getOrigin())
    ) * glm::mat4_cast(FromBullet(transform.getRotation()));
}
}
}  // namespace wisteria
