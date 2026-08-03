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
