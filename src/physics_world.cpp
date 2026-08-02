#include "physics_world.hpp"
#include "physics_bullet_conversion.hpp"
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <BulletDynamics/ConstraintSolver/btSliderConstraint.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
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
    if (!IsFinite(body.linearFactor) || !IsFinite(body.angularFactor) ||
        body.linearFactor.x < 0.0f || body.linearFactor.y < 0.0f ||
        body.linearFactor.z < 0.0f || body.angularFactor.x < 0.0f ||
        body.angularFactor.y < 0.0f || body.angularFactor.z < 0.0f)
    {
        throw std::invalid_argument(
            "Physics motion factors must be finite and non-negative"
        );
    }
    if (body.collisionGroup == 0U)
        throw std::invalid_argument("Physics collision group cannot be zero");
}

bool IsValidLimitPair(
    const glm::vec3& lower,
    const glm::vec3& upper
) noexcept
{
    return lower.x <= upper.x && lower.y <= upper.y && lower.z <= upper.z;
}

void ValidateConstraintFrame(const PhysicsConstraintFrame& frame)
{
    if (!IsFinite(frame.position) || !IsFinite(frame.rotation))
        throw std::invalid_argument("Physics constraint frame is non-finite");
    const float length = glm::length(frame.rotation);
    if (!std::isfinite(length) || length <= 0.000001f)
        throw std::invalid_argument("Physics constraint rotation is invalid");
}

void ValidateSpring6Dof(const PhysicsSpring6DofDesc& constraint)
{
    if (!constraint.bodyA.IsValid() && !constraint.bodyB.IsValid())
    {
        throw std::invalid_argument(
            "Spring 6DOF constraint requires at least one rigid body"
        );
    }
    ValidateConstraintFrame(constraint.frameA);
    ValidateConstraintFrame(constraint.frameB);
    const glm::vec3 vectors[] = {
        constraint.linearLower,
        constraint.linearUpper,
        constraint.angularLower,
        constraint.angularUpper,
        constraint.linearStiffness,
        constraint.angularStiffness,
        constraint.linearDamping,
        constraint.angularDamping
    };
    for (const glm::vec3& vector : vectors)
    {
        if (!IsFinite(vector))
            throw std::invalid_argument("Physics constraint contains non-finite data");
    }
    if (!IsValidLimitPair(constraint.linearLower, constraint.linearUpper) ||
        !IsValidLimitPair(constraint.angularLower, constraint.angularUpper))
    {
        throw std::invalid_argument(
            "Physics constraint lower limit exceeds its upper limit"
        );
    }
    const glm::vec3 nonNegative[] = {
        constraint.linearStiffness,
        constraint.angularStiffness,
        constraint.linearDamping,
        constraint.angularDamping
    };
    for (const glm::vec3& vector : nonNegative)
    {
        if (vector.x < 0.0f || vector.y < 0.0f || vector.z < 0.0f)
        {
            throw std::invalid_argument(
                "Physics spring stiffness and damping must be non-negative"
            );
        }
    }
}

void ValidateConstraintBodies(
    PhysicsBodyHandle bodyA,
    PhysicsBodyHandle bodyB,
    std::string_view name
)
{
    if (!bodyA.IsValid() && !bodyB.IsValid())
    {
        throw std::invalid_argument(
            std::string(name) + " requires at least one rigid body"
        );
    }
}

void ValidateSixDof(const PhysicsSixDofDesc& constraint)
{
    ValidateConstraintBodies(constraint.bodyA, constraint.bodyB, "6DOF constraint");
    ValidateConstraintFrame(constraint.frameA);
    ValidateConstraintFrame(constraint.frameB);
    if (!IsFinite(constraint.linearLower) || !IsFinite(constraint.linearUpper) ||
        !IsFinite(constraint.angularLower) || !IsFinite(constraint.angularUpper) ||
        !IsValidLimitPair(constraint.linearLower, constraint.linearUpper) ||
        !IsValidLimitPair(constraint.angularLower, constraint.angularUpper))
    {
        throw std::invalid_argument("6DOF constraint limits are invalid");
    }
}

void ValidatePointToPoint(const PhysicsPointToPointDesc& constraint)
{
    ValidateConstraintBodies(
        constraint.bodyA,
        constraint.bodyB,
        "Point-to-point constraint"
    );
    if (!IsFinite(constraint.pivotA) || !IsFinite(constraint.pivotB))
        throw std::invalid_argument("Point-to-point pivot is non-finite");
}

void ValidateConeTwist(const PhysicsConeTwistDesc& constraint)
{
    ValidateConstraintBodies(
        constraint.bodyA,
        constraint.bodyB,
        "Cone-twist constraint"
    );
    ValidateConstraintFrame(constraint.frameA);
    ValidateConstraintFrame(constraint.frameB);
    const float spans[] = {
        constraint.swingSpan1,
        constraint.swingSpan2,
        constraint.twistSpan
    };
    for (float span : spans)
    {
        if (!std::isfinite(span) || span < 0.0f)
            throw std::invalid_argument("Cone-twist span is invalid");
    }
}

void ValidateSlider(const PhysicsSliderDesc& constraint)
{
    ValidateConstraintBodies(constraint.bodyA, constraint.bodyB, "Slider constraint");
    ValidateConstraintFrame(constraint.frameA);
    ValidateConstraintFrame(constraint.frameB);
    const float values[] = {
        constraint.linearLower,
        constraint.linearUpper,
        constraint.angularLower,
        constraint.angularUpper
    };
    for (float value : values)
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("Slider limit is non-finite");
    }
    if (constraint.linearLower > constraint.linearUpper ||
        constraint.angularLower > constraint.angularUpper)
    {
        throw std::invalid_argument("Slider lower limit exceeds upper limit");
    }
}

void ValidateHinge(const PhysicsHingeDesc& constraint)
{
    ValidateConstraintBodies(constraint.bodyA, constraint.bodyB, "Hinge constraint");
    ValidateConstraintFrame(constraint.frameA);
    ValidateConstraintFrame(constraint.frameB);
    if (!std::isfinite(constraint.lowerAngle) ||
        !std::isfinite(constraint.upperAngle) ||
        constraint.lowerAngle > constraint.upperAngle)
    {
        throw std::invalid_argument("Hinge limits are invalid");
    }
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

class PhysicsDebugCollector final : public btIDebugDraw
{
public:
    void drawLine(
        const btVector3& from,
        const btVector3& to,
        const btVector3& color
    ) override
    {
        this->lines.push_back(PhysicsDebugLine{
            PhysicsBulletConversion::FromBullet(from),
            PhysicsBulletConversion::FromBullet(to),
            PhysicsBulletConversion::FromBullet(color)
        });
    }

    void drawContactPoint(
        const btVector3& pointOnB,
        const btVector3& normalOnB,
        btScalar distance,
        int,
        const btVector3& color
    ) override
    {
        this->drawLine(
            pointOnB,
            pointOnB + normalOnB * distance,
            color
        );
    }

    void reportErrorWarning(const char* warningString) override
    {
        std::cerr << "[Bullet debug] " << warningString << std::endl;
    }

    void draw3dText(const btVector3&, const char*) override
    {
    }

    void setDebugMode(int mode) override
    {
        this->mode = mode;
    }

    int getDebugMode() const override
    {
        return this->mode;
    }

    void Clear() noexcept
    {
        this->lines.clear();
    }

    std::vector<PhysicsDebugLine> lines;

private:
    int mode = btIDebugDraw::DBG_DrawWireframe |
        btIDebugDraw::DBG_DrawConstraints |
        btIDebugDraw::DBG_DrawConstraintLimits;
};

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

    struct ConstraintSlot
    {
        std::unique_ptr<btTypedConstraint> constraint;
        PhysicsBodyHandle bodyA{};
        PhysicsBodyHandle bodyB{};
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
        world->setDebugDrawer(&debugCollector);
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

    ConstraintSlot* Find(PhysicsConstraintHandle handle) noexcept
    {
        if (!handle.IsValid() || handle.index >= constraints.size())
            return nullptr;
        ConstraintSlot& slot = constraints[handle.index];
        if (!slot.constraint || slot.generation != handle.generation)
            return nullptr;
        return &slot;
    }

    const ConstraintSlot* Find(PhysicsConstraintHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.index >= constraints.size())
            return nullptr;
        const ConstraintSlot& slot = constraints[handle.index];
        if (!slot.constraint || slot.generation != handle.generation)
            return nullptr;
        return &slot;
    }

    ConstraintSlot& Require(PhysicsConstraintHandle handle)
    {
        ConstraintSlot* slot = Find(handle);
        if (!slot)
        {
            throw std::out_of_range(
                "Physics constraint handle is stale or invalid"
            );
        }
        return *slot;
    }

    std::pair<BodySlot*, BodySlot*> ResolveConstraintBodies(
        PhysicsBodyHandle bodyA,
        PhysicsBodyHandle bodyB,
        std::string_view name
    )
    {
        BodySlot* first = bodyA.IsValid() ? Find(bodyA) : nullptr;
        BodySlot* second = bodyB.IsValid() ? Find(bodyB) : nullptr;
        if (bodyA.IsValid() && first == nullptr)
            throw std::out_of_range(std::string(name) + " bodyA handle is invalid");
        if (bodyB.IsValid() && second == nullptr)
            throw std::out_of_range(std::string(name) + " bodyB handle is invalid");
        if (first != nullptr && second != nullptr && first == second)
            throw std::invalid_argument(std::string(name) + " cannot connect a body to itself");
        return {first, second};
    }

    PhysicsConstraintHandle StoreConstraint(
        std::unique_ptr<btTypedConstraint> next,
        PhysicsBodyHandle bodyA,
        PhysicsBodyHandle bodyB,
        bool disableCollisions
    )
    {
        std::uint32_t index = 0U;
        if (!freeConstraintSlots.empty())
        {
            index = freeConstraintSlots.back();
            freeConstraintSlots.pop_back();
        }
        else
        {
            if (constraints.size() >= static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                throw std::overflow_error(
                    "Physics constraint handle space exhausted"
                );
            }
            index = static_cast<std::uint32_t>(constraints.size());
            constraints.emplace_back();
        }
        ConstraintSlot& slot = constraints[index];
        slot.constraint = std::move(next);
        slot.bodyA = bodyA;
        slot.bodyB = bodyB;
        world->addConstraint(slot.constraint.get(), disableCollisions);
        ++constraintCount;
        return PhysicsConstraintHandle{index, slot.generation};
    }

    void ReleaseConstraint(std::uint32_t index) noexcept
    {
        ConstraintSlot& slot = constraints[index];
        if (!slot.constraint)
            return;
        world->removeConstraint(slot.constraint.get());
        slot.constraint.reset();
        slot.bodyA = {};
        slot.bodyB = {};
        ++slot.generation;
        if (slot.generation == 0U)
            slot.generation = 1U;
        freeConstraintSlots.push_back(index);
        --constraintCount;
    }

    void ReleaseConstraintsReferencing(PhysicsBodyHandle body) noexcept
    {
        for (std::uint32_t index = 0;
             index < static_cast<std::uint32_t>(constraints.size());
             ++index)
        {
            ConstraintSlot& slot = constraints[index];
            if (slot.constraint && (slot.bodyA == body || slot.bodyB == body))
                ReleaseConstraint(index);
        }
    }

    void Release(std::uint32_t index) noexcept
    {
        BodySlot& slot = bodies[index];
        if (!slot.body)
            return;

        ReleaseConstraintsReferencing(PhysicsBodyHandle{index, slot.generation});
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
        freeConstraintSlots.clear();
        for (std::uint32_t index = 0;
             index < static_cast<std::uint32_t>(constraints.size());
             ++index)
        {
            ConstraintSlot& slot = constraints[index];
            if (slot.constraint)
            {
                world->removeConstraint(slot.constraint.get());
                slot.constraint.reset();
                slot.bodyA = {};
                slot.bodyB = {};
                ++slot.generation;
                if (slot.generation == 0U)
                    slot.generation = 1U;
            }
            freeConstraintSlots.push_back(index);
        }
        constraintCount = 0;

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
    PhysicsDebugCollector debugCollector;
    std::unique_ptr<btDiscreteDynamicsWorld> world;
    PhysicsStepSettings settings;
    std::vector<BodySlot> bodies;
    std::vector<std::uint32_t> freeSlots;
    std::size_t bodyCount = 0;
    std::vector<ConstraintSlot> constraints;
    std::vector<std::uint32_t> freeConstraintSlots;
    std::size_t constraintCount = 0;
    bool debugDrawEnabled = false;
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
    rigidBody->setLinearFactor(
        PhysicsBulletConversion::ToBullet(description.linearFactor)
    );
    rigidBody->setAngularFactor(
        PhysicsBulletConversion::ToBullet(description.angularFactor)
    );

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

PhysicsConstraintHandle PhysicsWorld::CreateSpring6DofConstraint(
    const PhysicsSpring6DofDesc& description
)
{
    ValidateSpring6Dof(description);
    const auto [bodyA, bodyB] = impl->ResolveConstraintBodies(
        description.bodyA,
        description.bodyB,
        "Spring 6DOF"
    );

    const btTransform frameA = PhysicsBulletConversion::ToBullet(
        description.frameA.position,
        description.frameA.rotation
    );
    const btTransform frameB = PhysicsBulletConversion::ToBullet(
        description.frameB.position,
        description.frameB.rotation
    );

    std::unique_ptr<btGeneric6DofSpring2Constraint> constraint;
    if (bodyA != nullptr && bodyB != nullptr)
    {
        constraint = std::make_unique<btGeneric6DofSpring2Constraint>(
            *bodyA->body,
            *bodyB->body,
            frameA,
            frameB,
            RO_XYZ
        );
    }
    else if (bodyA != nullptr)
    {
        constraint = std::make_unique<btGeneric6DofSpring2Constraint>(
            *bodyA->body,
            frameA,
            RO_XYZ
        );
    }
    else
    {
        constraint = std::make_unique<btGeneric6DofSpring2Constraint>(
            *bodyB->body,
            frameB,
            RO_XYZ
        );
    }

    constraint->setLinearLowerLimit(
        PhysicsBulletConversion::ToBullet(description.linearLower)
    );
    constraint->setLinearUpperLimit(
        PhysicsBulletConversion::ToBullet(description.linearUpper)
    );
    constraint->setAngularLowerLimit(
        PhysicsBulletConversion::ToBullet(description.angularLower)
    );
    constraint->setAngularUpperLimit(
        PhysicsBulletConversion::ToBullet(description.angularUpper)
    );
    for (int axis = 0; axis < 3; ++axis)
    {
        const float stiffness = description.linearStiffness[axis];
        if (stiffness > 0.0f)
        {
            constraint->enableSpring(axis, true);
            constraint->setStiffness(axis, stiffness, true);
            constraint->setDamping(
                axis,
                description.linearDamping[axis],
                true
            );
        }
        const float angularStiffness = description.angularStiffness[axis];
        if (angularStiffness > 0.0f)
        {
            const int angularAxis = axis + 3;
            constraint->enableSpring(angularAxis, true);
            constraint->setStiffness(angularAxis, angularStiffness, true);
            constraint->setDamping(
                angularAxis,
                description.angularDamping[axis],
                true
            );
        }
    }
    constraint->setEquilibriumPoint();

    return impl->StoreConstraint(
        std::move(constraint),
        description.bodyA,
        description.bodyB,
        description.disableCollisionsBetweenLinkedBodies
    );
}

PhysicsConstraintHandle PhysicsWorld::CreateSixDofConstraint(
    const PhysicsSixDofDesc& description
)
{
    ValidateSixDof(description);
    const auto [bodyA, bodyB] = impl->ResolveConstraintBodies(
        description.bodyA,
        description.bodyB,
        "6DOF"
    );
    const btTransform frameA = PhysicsBulletConversion::ToBullet(
        description.frameA.position,
        description.frameA.rotation
    );
    const btTransform frameB = PhysicsBulletConversion::ToBullet(
        description.frameB.position,
        description.frameB.rotation
    );
    std::unique_ptr<btGeneric6DofConstraint> constraint;
    if (bodyA != nullptr && bodyB != nullptr)
    {
        constraint = std::make_unique<btGeneric6DofConstraint>(
            *bodyA->body, *bodyB->body, frameA, frameB, true
        );
    }
    else if (bodyA != nullptr)
    {
        constraint = std::make_unique<btGeneric6DofConstraint>(
            *bodyA->body, frameA, true
        );
    }
    else
    {
        constraint = std::make_unique<btGeneric6DofConstraint>(
            *bodyB->body, frameB, true
        );
    }
    constraint->setLinearLowerLimit(
        PhysicsBulletConversion::ToBullet(description.linearLower)
    );
    constraint->setLinearUpperLimit(
        PhysicsBulletConversion::ToBullet(description.linearUpper)
    );
    constraint->setAngularLowerLimit(
        PhysicsBulletConversion::ToBullet(description.angularLower)
    );
    constraint->setAngularUpperLimit(
        PhysicsBulletConversion::ToBullet(description.angularUpper)
    );
    return impl->StoreConstraint(
        std::move(constraint), description.bodyA, description.bodyB,
        description.disableCollisionsBetweenLinkedBodies
    );
}

PhysicsConstraintHandle PhysicsWorld::CreatePointToPointConstraint(
    const PhysicsPointToPointDesc& description
)
{
    ValidatePointToPoint(description);
    const auto [bodyA, bodyB] = impl->ResolveConstraintBodies(
        description.bodyA, description.bodyB, "Point-to-point"
    );
    const btVector3 pivotA = PhysicsBulletConversion::ToBullet(description.pivotA);
    const btVector3 pivotB = PhysicsBulletConversion::ToBullet(description.pivotB);
    std::unique_ptr<btPoint2PointConstraint> constraint;
    if (bodyA != nullptr && bodyB != nullptr)
        constraint = std::make_unique<btPoint2PointConstraint>(*bodyA->body, *bodyB->body, pivotA, pivotB);
    else if (bodyA != nullptr)
        constraint = std::make_unique<btPoint2PointConstraint>(*bodyA->body, pivotA);
    else
        constraint = std::make_unique<btPoint2PointConstraint>(*bodyB->body, pivotB);
    return impl->StoreConstraint(
        std::move(constraint), description.bodyA, description.bodyB,
        description.disableCollisionsBetweenLinkedBodies
    );
}

PhysicsConstraintHandle PhysicsWorld::CreateConeTwistConstraint(
    const PhysicsConeTwistDesc& description
)
{
    ValidateConeTwist(description);
    const auto [bodyA, bodyB] = impl->ResolveConstraintBodies(
        description.bodyA, description.bodyB, "Cone-twist"
    );
    const btTransform frameA = PhysicsBulletConversion::ToBullet(description.frameA.position, description.frameA.rotation);
    const btTransform frameB = PhysicsBulletConversion::ToBullet(description.frameB.position, description.frameB.rotation);
    std::unique_ptr<btConeTwistConstraint> constraint;
    if (bodyA != nullptr && bodyB != nullptr)
        constraint = std::make_unique<btConeTwistConstraint>(*bodyA->body, *bodyB->body, frameA, frameB);
    else if (bodyA != nullptr)
        constraint = std::make_unique<btConeTwistConstraint>(*bodyA->body, frameA);
    else
        constraint = std::make_unique<btConeTwistConstraint>(*bodyB->body, frameB);
    constraint->setLimit(
        description.swingSpan1,
        description.swingSpan2,
        description.twistSpan
    );
    return impl->StoreConstraint(
        std::move(constraint), description.bodyA, description.bodyB,
        description.disableCollisionsBetweenLinkedBodies
    );
}

PhysicsConstraintHandle PhysicsWorld::CreateSliderConstraint(
    const PhysicsSliderDesc& description
)
{
    ValidateSlider(description);
    const auto [bodyA, bodyB] = impl->ResolveConstraintBodies(
        description.bodyA, description.bodyB, "Slider"
    );
    const btTransform frameA = PhysicsBulletConversion::ToBullet(description.frameA.position, description.frameA.rotation);
    const btTransform frameB = PhysicsBulletConversion::ToBullet(description.frameB.position, description.frameB.rotation);
    std::unique_ptr<btSliderConstraint> constraint;
    if (bodyA != nullptr && bodyB != nullptr)
        constraint = std::make_unique<btSliderConstraint>(*bodyA->body, *bodyB->body, frameA, frameB, true);
    else if (bodyA != nullptr)
        constraint = std::make_unique<btSliderConstraint>(*bodyA->body, frameA, true);
    else
        constraint = std::make_unique<btSliderConstraint>(*bodyB->body, frameB, true);
    constraint->setLowerLinLimit(description.linearLower);
    constraint->setUpperLinLimit(description.linearUpper);
    constraint->setLowerAngLimit(description.angularLower);
    constraint->setUpperAngLimit(description.angularUpper);
    return impl->StoreConstraint(
        std::move(constraint), description.bodyA, description.bodyB,
        description.disableCollisionsBetweenLinkedBodies
    );
}

PhysicsConstraintHandle PhysicsWorld::CreateHingeConstraint(
    const PhysicsHingeDesc& description
)
{
    ValidateHinge(description);
    const auto [bodyA, bodyB] = impl->ResolveConstraintBodies(
        description.bodyA, description.bodyB, "Hinge"
    );
    const btTransform frameA = PhysicsBulletConversion::ToBullet(description.frameA.position, description.frameA.rotation);
    const btTransform frameB = PhysicsBulletConversion::ToBullet(description.frameB.position, description.frameB.rotation);
    std::unique_ptr<btHingeConstraint> constraint;
    if (bodyA != nullptr && bodyB != nullptr)
        constraint = std::make_unique<btHingeConstraint>(*bodyA->body, *bodyB->body, frameA, frameB, true);
    else if (bodyA != nullptr)
        constraint = std::make_unique<btHingeConstraint>(*bodyA->body, frameA, true);
    else
        constraint = std::make_unique<btHingeConstraint>(*bodyB->body, frameB, true);
    constraint->setLimit(description.lowerAngle, description.upperAngle);
    return impl->StoreConstraint(
        std::move(constraint), description.bodyA, description.bodyB,
        description.disableCollisionsBetweenLinkedBodies
    );
}

bool PhysicsWorld::DestroyConstraint(
    PhysicsConstraintHandle constraint
) noexcept
{
    if (!impl->Find(constraint))
        return false;
    impl->ReleaseConstraint(constraint.index);
    return true;
}

bool PhysicsWorld::Contains(PhysicsConstraintHandle constraint) const noexcept
{
    return impl->Find(constraint) != nullptr;
}

std::size_t PhysicsWorld::ConstraintCount() const noexcept
{
    return impl->constraintCount;
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

void PhysicsWorld::SetDebugDrawEnabled(bool enabled) noexcept
{
    impl->debugDrawEnabled = enabled;
    if (!enabled)
        impl->debugCollector.Clear();
}

bool PhysicsWorld::DebugDrawEnabled() const noexcept
{
    return impl->debugDrawEnabled;
}

std::span<const PhysicsDebugLine> PhysicsWorld::DebugLines() const noexcept
{
    return impl->debugCollector.lines;
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
    if (impl->debugDrawEnabled)
    {
        impl->debugCollector.Clear();
        impl->world->debugDrawWorld();
    }
}
