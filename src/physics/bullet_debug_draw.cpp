#include "wisteria/physics/physics_world.hpp"

#include "physics_world_impl.hpp"

namespace wisteria
{
std::span<const PhysicsContactPair> PhysicsWorld::ContactPairs() const noexcept
{
    return impl->contactPairs;
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

}  // namespace wisteria
