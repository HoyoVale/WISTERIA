#include "physics_world.hpp"
#include "physics_bullet_conversion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
bool IsFinite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFinite(const glm::quat& value) noexcept
{
    return std::isfinite(value.w) &&
        std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

void ValidateStepSettings(const PhysicsStepSettings& settings)
{
    if (settings.maxSubSteps < 0)
        throw std::invalid_argument("Physics maxSubSteps cannot be negative");
    if (!std::isfinite(settings.fixedTimeStep) ||
        settings.fixedTimeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "Physics fixedTimeStep must be finite and positive"
        );
    }
    if (!std::isfinite(settings.maxDeltaTime) ||
        settings.maxDeltaTime <= 0.0f)
    {
        throw std::invalid_argument(
            "Physics maxDeltaTime must be finite and positive"
        );
    }
}

void ValidateShape(const PhysicsShapeDesc& shape)
{
    if (!IsFinite(shape.dimensions))
        throw std::invalid_argument("Physics shape contains non-finite data");

    switch (shape.kind)
    {
    case PhysicsShapeKind::Sphere:
        if (shape.dimensions.x <= 0.0f)
            throw std::invalid_argument("Sphere radius must be positive");
        break;
    case PhysicsShapeKind::Box:
        if (shape.dimensions.x <= 0.0f ||
            shape.dimensions.y <= 0.0f ||
            shape.dimensions.z <= 0.0f)
        {
            throw std::invalid_argument(
                "Box half extents must all be positive"
            );
        }
        break;
    case PhysicsShapeKind::Capsule:
        if (shape.dimensions.x <= 0.0f || shape.dimensions.y < 0.0f)
        {
            throw std::invalid_argument(
                "Capsule radius must be positive and height non-negative"
            );
        }
        break;
    }
}

void ValidateBody(const PhysicsBodyDesc& body)
{
    ValidateShape(body.shape);
    if (!IsFinite(body.position) || !IsFinite(body.rotation))
        throw std::invalid_argument("Physics body transform is non-finite");

    const float rotationLength = glm::length(body.rotation);
    if (!std::isfinite(rotationLength) || rotationLength <= 0.000001f)
        throw std::invalid_argument("Physics body rotation is invalid");

    if (!std::isfinite(body.mass) || body.mass < 0.0f)
        throw std::invalid_argument("Physics body mass is invalid");
    if (body.motionType == PhysicsMotionType::Dynamic && body.mass <= 0.0f)
        throw std::invalid_argument("Dynamic body mass must be positive");

    const float coefficients[] = {
        body.linearDamping,
        body.angularDamping,
        body.restitution,
        body.friction
    };
    for (float coefficient : coefficients)
    {
        if (!std::isfinite(coefficient) || coefficient < 0.0f)
        {
            throw std::invalid_argument(
                "Physics material coefficients must be finite and non-negative"
            );
        }
    }
    if (body.linearDamping > 1.0f || body.angularDamping > 1.0f)
        throw std::invalid_argument("Physics damping must not exceed one");
    if (body.collisionGroup == 0U)
        throw std::invalid_argument("Physics collision group cannot be zero");
}

std::unique_ptr<btCollisionShape> CreateShape(
    const PhysicsShapeDesc& shape
)
{
    switch (shape.kind)
    {
    case PhysicsShapeKind::Sphere:
        return std::make_unique<btSphereShape>(shape.dimensions.x);
    case PhysicsShapeKind::Box:
        return std::make_unique<btBoxShape>(
            PhysicsBulletConversion::ToBullet(shape.dimensions)
        );
    case PhysicsShapeKind::Capsule:
        return std::make_unique<btCapsuleShape>(
            shape.dimensions.x,
            shape.dimensions.y
        );
    }
    throw std::invalid_argument("Unknown physics shape kind");
}
}

class PhysicsWorld::Impl
{
public:
    struct BodySlot
    {
        std::unique_ptr<btCollisionShape> shape;
        std::unique_ptr<btDefaultMotionState> motionState;
        std::unique_ptr<btRigidBody> body;
        PhysicsMotionType motionType = PhysicsMotionType::Static;
        std::uint32_t generation = 1;
    };

    explicit Impl(const PhysicsStepSettings& initialSettings)
        : collisionConfiguration(
            std::make_unique<btDefaultCollisionConfiguration>()
        ),
        dispatcher(std::make_unique<btCollisionDispatcher>(
            collisionConfiguration.get()
        )),
        broadphase(std::make_unique<btDbvtBroadphase>()),
        solver(std::make_unique<btSequentialImpulseConstraintSolver>()),
        world(std::make_unique<btDiscreteDynamicsWorld>(
            dispatcher.get(),
            broadphase.get(),
            solver.get(),
            collisionConfiguration.get()
        )),
        settings(initialSettings)
    {
        ValidateStepSettings(settings);
        world->setGravity(btVector3(0.0f, -9.8f, 0.0f));
    }

    ~Impl()
    {
        Clear();
    }

    BodySlot* Find(PhysicsBodyHandle handle) noexcept
    {
        if (!handle.IsValid() || handle.index >= bodies.size())
            return nullptr;
        BodySlot& slot = bodies[handle.index];
        if (!slot.body || slot.generation != handle.generation)
            return nullptr;
        return &slot;
    }

    const BodySlot* Find(PhysicsBodyHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.index >= bodies.size())
            return nullptr;
        const BodySlot& slot = bodies[handle.index];
        if (!slot.body || slot.generation != handle.generation)
            return nullptr;
        return &slot;
    }

    BodySlot& Require(PhysicsBodyHandle handle)
    {
        BodySlot* slot = Find(handle);
        if (!slot)
            throw std::out_of_range("Physics body handle is stale or invalid");
        return *slot;
    }

    const BodySlot& Require(PhysicsBodyHandle handle) const
    {
        const BodySlot* slot = Find(handle);
        if (!slot)
            throw std::out_of_range("Physics body handle is stale or invalid");
        return *slot;
    }

    void Release(std::uint32_t index) noexcept
    {
        BodySlot& slot = bodies[index];
        if (!slot.body)
            return;

        world->removeRigidBody(slot.body.get());
        slot.body.reset();
        slot.motionState.reset();
        slot.shape.reset();
        slot.motionType = PhysicsMotionType::Static;
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;
        freeSlots.push_back(index);
        --bodyCount;
    }

    void Clear() noexcept
    {
        freeSlots.clear();
        for (std::uint32_t index = 0;
            index < static_cast<std::uint32_t>(bodies.size());
            ++index)
        {
            BodySlot& slot = bodies[index];
            if (slot.body)
            {
                world->removeRigidBody(slot.body.get());
                slot.body.reset();
                slot.motionState.reset();
                slot.shape.reset();
                slot.motionType = PhysicsMotionType::Static;
                ++slot.generation;
                if (slot.generation == 0)
                    slot.generation = 1;
            }
            freeSlots.push_back(index);
        }
        bodyCount = 0;
    }

    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfiguration;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btBroadphaseInterface> broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::unique_ptr<btDiscreteDynamicsWorld> world;
    PhysicsStepSettings settings;
    std::vector<BodySlot> bodies;
    std::vector<std::uint32_t> freeSlots;
    std::size_t bodyCount = 0;
};

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
}

glm::vec3 PhysicsWorld::Gravity() const noexcept
{
    return PhysicsBulletConversion::FromBullet(impl->world->getGravity());
}

void PhysicsWorld::SetStepSettings(const PhysicsStepSettings& settings)
{
    ValidateStepSettings(settings);
    impl->settings = settings;
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
    slot.motionType = description.motionType;

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

void PhysicsWorld::Activate(PhysicsBodyHandle body)
{
    impl->Require(body).body->activate(true);
}

void PhysicsWorld::Step(float deltaTime)
{
    if (!std::isfinite(deltaTime))
        throw std::invalid_argument("Physics deltaTime is non-finite");
    if (deltaTime <= 0.0f)
        return;

    const float safeDeltaTime = std::min(
        deltaTime,
        impl->settings.maxDeltaTime
    );
    impl->world->stepSimulation(
        safeDeltaTime,
        impl->settings.maxSubSteps,
        impl->settings.fixedTimeStep
    );
}
