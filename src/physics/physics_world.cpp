#include "wisteria/physics/physics_world.hpp"

#include "physics_world_impl.hpp"

PhysicsWorld::PhysicsWorld(const PhysicsStepSettings& settings)
    : impl(std::make_unique<Impl>(settings))
{
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

void PhysicsWorld::SetGravity(const glm::vec3& gravity)
{
    if (!IsFinite(gravity))
        throw std::invalid_argument("Physics gravity is non-finite");
    impl->world->setGravity(PhysicsBulletConversion::ToBullet(gravity));
    for (Impl::BodySlot& slot : impl->bodies)
    {
        if (!slot.body || slot.runtimeSettings.gravityOverride)
            continue;
        slot.runtimeSettings.gravity = gravity;
    }
}

glm::vec3 PhysicsWorld::Gravity() const noexcept
{
    return PhysicsBulletConversion::FromBullet(impl->world->getGravity());
}

void PhysicsWorld::SetStepSettings(const PhysicsStepSettings& settings)
{
    ValidateStepSettings(settings);
    impl->settings = settings;
    ApplySolverSettings(*impl->world, settings);
}

const PhysicsStepSettings& PhysicsWorld::StepSettings() const noexcept
{
    return impl->settings;
}

PhysicsBodyHandle PhysicsWorld::CreateBody(
    const PhysicsBodyDesc& description
)
{
    ValidateBody(description);

    std::unique_ptr<btCollisionShape> shape = CreateShape(description.shape);
    float resolvedCollisionMargin = shape->getMargin();
    if (description.shape.kind == PhysicsShapeKind::Box)
    {
        resolvedCollisionMargin = ResolveBoxCollisionMargin(description);
        shape->setMargin(resolvedCollisionMargin);
    }

    const btTransform startTransform = PhysicsBulletConversion::ToBullet(
        description.position,
        description.rotation
    );
    auto motionState = std::make_unique<btDefaultMotionState>(startTransform);

    const btScalar mass = description.motionType == PhysicsMotionType::Dynamic
        ? static_cast<btScalar>(description.mass)
        : btScalar(0.0);
    btVector3 localInertia(0.0, 0.0, 0.0);
    if (mass > btScalar(0.0))
        shape->calculateLocalInertia(mass, localInertia);

    btRigidBody::btRigidBodyConstructionInfo construction(
        mass,
        motionState.get(),
        shape.get(),
        localInertia
    );
    construction.m_linearDamping = description.linearDamping;
    construction.m_angularDamping = description.angularDamping;
    construction.m_restitution = description.restitution;
    construction.m_friction = description.friction;

    auto rigidBody = std::make_unique<btRigidBody>(construction);
    rigidBody->setWorldTransform(startTransform);
    rigidBody->setLinearFactor(
        PhysicsBulletConversion::ToBullet(description.linearFactor)
    );
    rigidBody->setAngularFactor(
        PhysicsBulletConversion::ToBullet(description.angularFactor)
    );
    if (description.enableCcd)
    {
        rigidBody->setCcdMotionThreshold(description.ccdMotionThreshold);
        rigidBody->setCcdSweptSphereRadius(
            description.ccdSweptSphereRadius
        );
    }

    if (description.motionType == PhysicsMotionType::Kinematic)
    {
        rigidBody->setCollisionFlags(
            rigidBody->getCollisionFlags() |
            btCollisionObject::CF_KINEMATIC_OBJECT
        );
        rigidBody->setActivationState(DISABLE_DEACTIVATION);
    }
    else if (description.disableDeactivation)
    {
        rigidBody->setActivationState(DISABLE_DEACTIVATION);
    }

    std::uint32_t index = 0;
    if (!impl->freeSlots.empty())
    {
        index = impl->freeSlots.back();
        impl->freeSlots.pop_back();
    }
    else
    {
        if (impl->bodies.size() >=
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()
            ))
        {
            throw std::overflow_error("Physics body handle space exhausted");
        }
        index = static_cast<std::uint32_t>(impl->bodies.size());
        impl->bodies.emplace_back();
    }

    Impl::BodySlot& slot = impl->bodies[index];
    slot.shape = std::move(shape);
    slot.motionState = std::move(motionState);
    slot.body = std::move(rigidBody);
    slot.body->setUserIndex(static_cast<int>(index));
    slot.motionType = description.motionType;
    slot.runtimeSettings = PhysicsBodyRuntimeSettings{
        resolvedCollisionMargin,
        description.enableCcd,
        description.enableCcd ? description.ccdMotionThreshold : 0.0f,
        description.enableCcd ? description.ccdSweptSphereRadius : 0.0f,
        description.collisionGroup,
        description.collisionMask,
        false,
        PhysicsBulletConversion::FromBullet(impl->world->getGravity()),
        description.linearDamping,
        description.angularDamping
    };

    impl->world->addRigidBody(
        slot.body.get(),
        static_cast<int>(description.collisionGroup),
        static_cast<int>(description.collisionMask)
    );
    ++impl->bodyCount;
    return PhysicsBodyHandle{index, slot.generation};
}

bool PhysicsWorld::DestroyBody(PhysicsBodyHandle body) noexcept
{
    if (!impl->Find(body))
        return false;
    impl->Release(body.index);
    // Contact handles are snapshots from the previous fixed tick. Once a
    // body is destroyed, discard the snapshot instead of exposing a stale
    // generation until the next simulation step.
    impl->contactPairs.clear();
    return true;
}

void PhysicsWorld::Clear() noexcept
{
    impl->Clear();
}

bool PhysicsWorld::Contains(PhysicsBodyHandle body) const noexcept
{
    return impl->Find(body) != nullptr;
}

std::size_t PhysicsWorld::BodyCount() const noexcept
{
    return impl->bodyCount;
}

PhysicsWorldStatistics PhysicsWorld::Statistics() const noexcept
{
    PhysicsWorldStatistics statistics;
    statistics.bodyCount = impl->bodyCount;
    statistics.constraintCount = impl->constraintCount;
    statistics.solverIterations = impl->settings.solverIterations;
    statistics.splitImpulse = impl->settings.splitImpulse;
    statistics.splitImpulsePenetrationThreshold =
        impl->settings.splitImpulsePenetrationThreshold;
    statistics.maximumErrorReduction =
        impl->settings.maximumErrorReduction;
    statistics.restitutionVelocityThreshold =
        impl->settings.restitutionVelocityThreshold;
    statistics.contactPairCount = impl->contactPairs.size();
    bool hasBoxMargin = false;

    for (const Impl::BodySlot& slot : impl->bodies)
    {
        if (!slot.body)
            continue;

        switch (slot.motionType)
        {
        case PhysicsMotionType::Static:
            ++statistics.staticBodyCount;
            break;
        case PhysicsMotionType::Dynamic:
            ++statistics.dynamicBodyCount;
            break;
        case PhysicsMotionType::Kinematic:
            ++statistics.kinematicBodyCount;
            break;
        }

        if (slot.runtimeSettings.ccdEnabled)
            ++statistics.ccdBodyCount;
        if (dynamic_cast<const btBoxShape*>(slot.shape.get()) != nullptr)
        {
            const float margin = slot.runtimeSettings.collisionMargin;
            if (!hasBoxMargin)
            {
                statistics.minimumBoxCollisionMargin = margin;
                statistics.maximumBoxCollisionMargin = margin;
                hasBoxMargin = true;
            }
            else
            {
                statistics.minimumBoxCollisionMargin = std::min(
                    statistics.minimumBoxCollisionMargin,
                    margin
                );
                statistics.maximumBoxCollisionMargin = std::max(
                    statistics.maximumBoxCollisionMargin,
                    margin
                );
            }
        }

        if (slot.motionType != PhysicsMotionType::Static)
        {
            if (slot.body->isActive())
                ++statistics.activeBodyCount;
            else
                ++statistics.sleepingBodyCount;
        }

        const btTransform& transform = slot.body->getWorldTransform();
        const btVector3& position = transform.getOrigin();
        const btQuaternion rotation = transform.getRotation();
        const btVector3& linearVelocity = slot.body->getLinearVelocity();
        const btVector3& angularVelocity = slot.body->getAngularVelocity();
        const btScalar values[] = {
            position.x(), position.y(), position.z(),
            rotation.w(), rotation.x(), rotation.y(), rotation.z(),
            linearVelocity.x(), linearVelocity.y(), linearVelocity.z(),
            angularVelocity.x(), angularVelocity.y(), angularVelocity.z()
        };
        for (const btScalar value : values)
        {
            if (!std::isfinite(static_cast<double>(value)))
            {
                statistics.finite = false;
                break;
            }
        }
    }

    const int manifoldCount = impl->dispatcher->getNumManifolds();
    statistics.contactManifoldCount = manifoldCount > 0
        ? static_cast<std::size_t>(manifoldCount)
        : 0U;
    for (int index = 0; index < manifoldCount; ++index)
    {
        const btPersistentManifold* manifold =
            impl->dispatcher->getManifoldByIndexInternal(index);
        if (manifold != nullptr && manifold->getNumContacts() > 0)
        {
            statistics.contactPointCount += static_cast<std::size_t>(
                manifold->getNumContacts()
            );
        }
    }
    return statistics;
}

