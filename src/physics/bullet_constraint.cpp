#include "wisteria/physics/physics_world.hpp"

#include "physics_world_impl.hpp"

namespace wisteria
{
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

    std::unique_ptr<btTypedConstraint> constraint;
    if (description.useLegacySpringConstraint)
    {
        std::unique_ptr<btGeneric6DofSpringConstraint> legacy;
        if (bodyA != nullptr && bodyB != nullptr)
        {
            legacy = std::make_unique<btGeneric6DofSpringConstraint>(
                *bodyA->body,
                *bodyB->body,
                frameA,
                frameB,
                true
            );
        }
        else if (bodyA != nullptr)
        {
            legacy = std::make_unique<btGeneric6DofSpringConstraint>(
                *bodyA->body,
                frameA,
                true
            );
        }
        else
        {
            legacy = std::make_unique<btGeneric6DofSpringConstraint>(
                *bodyB->body,
                frameB,
                true
            );
        }

        legacy->setUseFrameOffset(
            !description.disableOffsetForConstraintFrame
        );
        for (int axis = 0; axis < 6; ++axis)
        {
            legacy->setParam(
                BT_CONSTRAINT_STOP_ERP,
                description.constraintStopErp,
                axis
            );
        }
        legacy->setLinearLowerLimit(
            PhysicsBulletConversion::ToBullet(description.linearLower)
        );
        legacy->setLinearUpperLimit(
            PhysicsBulletConversion::ToBullet(description.linearUpper)
        );
        legacy->setAngularLowerLimit(
            PhysicsBulletConversion::ToBullet(description.angularLower)
        );
        legacy->setAngularUpperLimit(
            PhysicsBulletConversion::ToBullet(description.angularUpper)
        );
        for (int axis = 0; axis < 3; ++axis)
        {
            const float stiffness = description.linearStiffness[axis];
            if (stiffness > 0.0f)
            {
                legacy->enableSpring(axis, true);
                legacy->setStiffness(axis, stiffness);
            }
            const float angularStiffness = description.angularStiffness[axis];
            if (angularStiffness > 0.0f)
            {
                const int angularAxis = axis + 3;
                legacy->enableSpring(angularAxis, true);
                legacy->setStiffness(angularAxis, angularStiffness);
            }
        }
        constraint = std::move(legacy);
    }
    else
    {
        std::unique_ptr<btGeneric6DofSpring2Constraint> spring2;
        if (bodyA != nullptr && bodyB != nullptr)
        {
            spring2 = std::make_unique<btGeneric6DofSpring2Constraint>(
                *bodyA->body,
                *bodyB->body,
                frameA,
                frameB,
                RO_XYZ
            );
        }
        else if (bodyA != nullptr)
        {
            spring2 = std::make_unique<btGeneric6DofSpring2Constraint>(
                *bodyA->body,
                frameA,
                RO_XYZ
            );
        }
        else
        {
            spring2 = std::make_unique<btGeneric6DofSpring2Constraint>(
                *bodyB->body,
                frameB,
                RO_XYZ
            );
        }

        spring2->setLinearLowerLimit(
            PhysicsBulletConversion::ToBullet(description.linearLower)
        );
        spring2->setLinearUpperLimit(
            PhysicsBulletConversion::ToBullet(description.linearUpper)
        );
        spring2->setAngularLowerLimit(
            PhysicsBulletConversion::ToBullet(description.angularLower)
        );
        spring2->setAngularUpperLimit(
            PhysicsBulletConversion::ToBullet(description.angularUpper)
        );
        for (int axis = 0; axis < 3; ++axis)
        {
            const float stiffness = description.linearStiffness[axis];
            if (stiffness > 0.0f)
            {
                spring2->enableSpring(axis, true);
                spring2->setStiffness(axis, stiffness, true);
                spring2->setDamping(
                    axis,
                    description.linearDamping[axis],
                    true
                );
            }
            const float angularStiffness = description.angularStiffness[axis];
            if (angularStiffness > 0.0f)
            {
                const int angularAxis = axis + 3;
                spring2->enableSpring(angularAxis, true);
                spring2->setStiffness(angularAxis, angularStiffness, true);
                spring2->setDamping(
                    angularAxis,
                    description.angularDamping[axis],
                    true
                );
            }
        }
        spring2->setEquilibriumPoint();
        constraint = std::move(spring2);
    }

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
    if (description.bullet275Mode)
    {
        constraint->setUseFrameOffset(false);
        for (int axis = 0; axis < 6; ++axis)
        {
            constraint->setParam(
                BT_CONSTRAINT_STOP_ERP,
                description.constraintStopErp,
                axis
            );
        }
    }
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

PhysicsConstraintRuntimeState PhysicsWorld::ConstraintState(
    PhysicsConstraintHandle constraint
) const
{
    const Impl::ConstraintSlot* slot = impl->Find(constraint);
    if (slot == nullptr)
    {
        throw std::out_of_range(
            "Physics constraint handle is stale or invalid"
        );
    }
    const btJointFeedback* feedback = slot->feedback.get();
    if (feedback == nullptr || impl->lastFixedTimeStep <= 0.0f)
        return {};
    const float linearForce = static_cast<float>(std::max(
        feedback->m_appliedForceBodyA.length(),
        feedback->m_appliedForceBodyB.length()
    ));
    const float angularForce = static_cast<float>(std::max(
        feedback->m_appliedTorqueBodyA.length(),
        feedback->m_appliedTorqueBodyB.length()
    ));
    return PhysicsConstraintRuntimeState{
        linearForce * impl->lastFixedTimeStep,
        angularForce * impl->lastFixedTimeStep
    };
}

std::size_t PhysicsWorld::ConstraintCount() const noexcept
{
    return impl->constraintCount;
}

}  // namespace wisteria
