#include "wisteria/physics/physics_world.hpp"

#include "physics_world_impl.hpp"

PhysicsBodyState PhysicsWorld::State(PhysicsBodyHandle body) const
{
    const Impl::BodySlot& slot = impl->Require(body);
    const btTransform& transform = slot.body->getWorldTransform();
    return PhysicsBodyState{
        PhysicsBulletConversion::FromBullet(transform.getOrigin()),
        PhysicsBulletConversion::FromBullet(transform.getRotation()),
        PhysicsBulletConversion::FromBullet(slot.body->getLinearVelocity()),
        PhysicsBulletConversion::FromBullet(slot.body->getAngularVelocity()),
        slot.body->isActive()
    };
}

PhysicsBodyRuntimeSettings PhysicsWorld::RuntimeSettings(
    PhysicsBodyHandle body
) const
{
    return impl->Require(body).runtimeSettings;
}

void PhysicsWorld::ConfigureCcd(
    PhysicsBodyHandle body,
    bool enabled,
    float motionThreshold,
    float sweptSphereRadius
)
{
    if (!std::isfinite(motionThreshold) ||
        !std::isfinite(sweptSphereRadius) ||
        motionThreshold < 0.0f || sweptSphereRadius < 0.0f)
    {
        throw std::invalid_argument("Physics CCD configuration is invalid");
    }
    Impl::BodySlot& slot = impl->Require(body);
    if (enabled && slot.motionType != PhysicsMotionType::Dynamic)
    {
        throw std::invalid_argument(
            "CCD can only be enabled for dynamic rigid bodies"
        );
    }
    if (enabled && (motionThreshold <= 0.0f || sweptSphereRadius <= 0.0f))
    {
        throw std::invalid_argument(
            "Enabled CCD requires positive threshold and swept radius"
        );
    }
    slot.body->setCcdMotionThreshold(enabled ? motionThreshold : 0.0f);
    slot.body->setCcdSweptSphereRadius(
        enabled ? sweptSphereRadius : 0.0f
    );
    slot.runtimeSettings.ccdEnabled = enabled;
    slot.runtimeSettings.ccdMotionThreshold =
        enabled ? motionThreshold : 0.0f;
    slot.runtimeSettings.ccdSweptSphereRadius =
        enabled ? sweptSphereRadius : 0.0f;
}

void PhysicsWorld::ConfigureGravity(
    PhysicsBodyHandle body,
    bool overrideWorldGravity,
    const glm::vec3& gravity
)
{
    if (!IsFinite(gravity))
        throw std::invalid_argument("Physics body gravity is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    if (slot.motionType != PhysicsMotionType::Dynamic)
    {
        if (overrideWorldGravity)
        {
            throw std::invalid_argument(
                "Gravity override can only be enabled for dynamic bodies"
            );
        }
        return;
    }

    int flags = slot.body->getFlags();
    if (overrideWorldGravity)
        flags |= BT_DISABLE_WORLD_GRAVITY;
    else
        flags &= ~BT_DISABLE_WORLD_GRAVITY;
    slot.body->setFlags(flags);
    const glm::vec3 appliedGravity = overrideWorldGravity
        ? gravity
        : PhysicsBulletConversion::FromBullet(impl->world->getGravity());
    slot.body->setGravity(PhysicsBulletConversion::ToBullet(appliedGravity));
    slot.runtimeSettings.gravityOverride = overrideWorldGravity;
    slot.runtimeSettings.gravity = appliedGravity;
    slot.body->activate(true);
}

void PhysicsWorld::SetDamping(
    PhysicsBodyHandle body,
    float linearDamping,
    float angularDamping
)
{
    if (!std::isfinite(linearDamping) ||
        !std::isfinite(angularDamping) ||
        linearDamping < 0.0f || linearDamping > 1.0f ||
        angularDamping < 0.0f || angularDamping > 1.0f)
    {
        throw std::invalid_argument("Physics damping is outside [0, 1]");
    }
    Impl::BodySlot& slot = impl->Require(body);
    slot.body->setDamping(linearDamping, angularDamping);
    slot.runtimeSettings.linearDamping = linearDamping;
    slot.runtimeSettings.angularDamping = angularDamping;
    slot.body->activate(true);
}

void PhysicsWorld::SetKinematic(
    PhysicsBodyHandle body,
    bool kinematic
)
{
    Impl::BodySlot& slot = impl->Require(body);
    if (kinematic)
    {
        slot.body->setCollisionFlags(
            slot.body->getCollisionFlags() |
            btCollisionObject::CF_KINEMATIC_OBJECT
        );
        slot.body->setActivationState(DISABLE_DEACTIVATION);
    }
    else
    {
        slot.body->setCollisionFlags(
            slot.body->getCollisionFlags() &
            ~btCollisionObject::CF_KINEMATIC_OBJECT
        );
        if (slot.motionType == PhysicsMotionType::Dynamic)
            slot.body->setActivationState(ACTIVE_TAG);
        slot.body->activate(true);
    }
}

void PhysicsWorld::SetCollisionPairIgnored(
    PhysicsBodyHandle bodyA,
    PhysicsBodyHandle bodyB,
    bool ignored
)
{
    if (bodyA == bodyB)
        return;
    Impl::BodySlot& first = impl->Require(bodyA);
    Impl::BodySlot& second = impl->Require(bodyB);
    first.body->setIgnoreCollisionCheck(second.body.get(), ignored);
    second.body->setIgnoreCollisionCheck(first.body.get(), ignored);
}


void PhysicsWorld::SetTransform(
    PhysicsBodyHandle body,
    const glm::vec3& position,
    const glm::quat& rotation,
    bool clearVelocity
)
{
    if (!IsFinite(position) || !IsFinite(rotation) ||
        glm::length(rotation) <= 0.000001f)
    {
        throw std::invalid_argument("Physics transform is invalid");
    }

    Impl::BodySlot& slot = impl->Require(body);
    const btTransform transform = PhysicsBulletConversion::ToBullet(
        position,
        rotation
    );
    slot.body->setWorldTransform(transform);
    if (clearVelocity || slot.motionType != PhysicsMotionType::Kinematic)
        slot.body->setInterpolationWorldTransform(transform);
    if (slot.motionState)
        slot.motionState->setWorldTransform(transform);
    if (clearVelocity)
    {
        slot.body->setLinearVelocity(btVector3(0.0, 0.0, 0.0));
        slot.body->setAngularVelocity(btVector3(0.0, 0.0, 0.0));
        slot.body->clearForces();
    }
    slot.body->activate(true);
    impl->world->updateSingleAabb(slot.body.get());
}

void PhysicsWorld::SetLinearVelocity(
    PhysicsBodyHandle body,
    const glm::vec3& velocity
)
{
    if (!IsFinite(velocity))
        throw std::invalid_argument("Physics linear velocity is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    slot.body->setLinearVelocity(PhysicsBulletConversion::ToBullet(velocity));
    slot.body->activate(true);
}

void PhysicsWorld::SetAngularVelocity(
    PhysicsBodyHandle body,
    const glm::vec3& velocity
)
{
    if (!IsFinite(velocity))
        throw std::invalid_argument("Physics angular velocity is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    slot.body->setAngularVelocity(PhysicsBulletConversion::ToBullet(velocity));
    slot.body->activate(true);
}

void PhysicsWorld::SetLinearFactor(
    PhysicsBodyHandle body,
    const glm::vec3& factor
)
{
    if (!IsFinite(factor))
        throw std::invalid_argument("Physics linear factor is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    slot.body->setLinearFactor(PhysicsBulletConversion::ToBullet(factor));
    slot.body->activate(true);
}

void PhysicsWorld::SetAngularFactor(
    PhysicsBodyHandle body,
    const glm::vec3& factor
)
{
    if (!IsFinite(factor))
        throw std::invalid_argument("Physics angular factor is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    slot.body->setAngularFactor(PhysicsBulletConversion::ToBullet(factor));
    slot.body->activate(true);
}

void PhysicsWorld::ApplyCentralImpulse(
    PhysicsBodyHandle body,
    const glm::vec3& impulse
)
{
    if (!IsFinite(impulse))
        throw std::invalid_argument("Physics impulse is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    if (slot.motionType != PhysicsMotionType::Dynamic)
        return;
    slot.body->applyCentralImpulse(PhysicsBulletConversion::ToBullet(impulse));
    slot.body->activate(true);
}

void PhysicsWorld::ApplyTorqueImpulse(
    PhysicsBodyHandle body,
    const glm::vec3& impulse
)
{
    if (!IsFinite(impulse))
        throw std::invalid_argument("Physics torque impulse is non-finite");
    Impl::BodySlot& slot = impl->Require(body);
    if (slot.motionType != PhysicsMotionType::Dynamic)
        return;
    slot.body->applyTorqueImpulse(PhysicsBulletConversion::ToBullet(impulse));
    slot.body->activate(true);
}

void PhysicsWorld::ClearDynamics(PhysicsBodyHandle body)
{
    Impl::BodySlot& slot = impl->Require(body);
    slot.body->setLinearVelocity(btVector3(0.0, 0.0, 0.0));
    slot.body->setAngularVelocity(btVector3(0.0, 0.0, 0.0));
    slot.body->clearForces();
    slot.body->activate(true);
}

void PhysicsWorld::Activate(PhysicsBodyHandle body)
{
    impl->Require(body).body->activate(true);
}

