#pragma once

#include <cstdint>
#include <limits>

using RigidBodyIndex = std::uint32_t;

inline constexpr RigidBodyIndex InvalidRigidBodyIndex =
    std::numeric_limits<RigidBodyIndex>::max();
