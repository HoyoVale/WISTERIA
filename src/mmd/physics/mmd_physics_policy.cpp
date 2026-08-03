#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/physics/mmd_physics_policy.hpp"

const MmdPhysicsChainTuning& MmdPhysicsRuntimePolicy::ChainTuning(
    MmdPhysicsChainKind kind
) const noexcept
{
    switch (kind)
    {
    case MmdPhysicsChainKind::Skirt:
        return this->skirt;
    case MmdPhysicsChainKind::Hair:
        return this->hair;
    case MmdPhysicsChainKind::Tail:
        return this->tail;
    case MmdPhysicsChainKind::Accessory:
        return this->accessory;
    case MmdPhysicsChainKind::DecorativeFallback:
        return this->decorativeFallback;
    case MmdPhysicsChainKind::General:
        return this->general;
    }
    return this->general;
}

MmdPhysicsRuntimePolicy MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults()
{
    return {};
}

MmdPhysicsRuntimePolicy MmdPhysicsRuntimePolicy::MmdCompatDefaults()
{
    MmdPhysicsRuntimePolicy policy = WisteriaAdaptiveDefaults();
    policy.name = "mmd-compat-bullet275-v1";
    policy.bullet275.legacySpringConstraint = true;
    policy.bullet275.disableOffsetForConstraintFrame = true;
    policy.bullet275.disableDynamicDeactivation = true;
    // P0 隔离实验显示：本模型在当前步长/重力下开启 linked-body 碰撞会显著
    // 增加约束违规（severe 4 -> 84）。碰撞行为留给 P1 单独审计，默认保持现状。
    policy.bullet275.disableLinkedBodyCollisions = true;
    return policy;
}
