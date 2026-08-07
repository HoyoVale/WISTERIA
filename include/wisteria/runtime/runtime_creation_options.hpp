#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

namespace wisteria
{
// R1.4 Phase 0A: WISTERIA-governed semantic profile. This is NOT a backend
// selection knob; backend adapters translate it internally
// ("Saba executes; WISTERIA governs", contract §2B).
enum class RuntimeCompatibilityProfile : std::uint8_t
{
    Raw = 0,
    Community = 1,
    Adaptive = 2
};

// Stable physics settings subset shared by all deterministic runtimes.
struct RuntimePhysicsSettings
{
    float fixedTimeStep = 1.0f / 120.0f;
    int maxSubSteps = 10;
    glm::vec3 gravity{0.0f, -98.0f, 0.0f};
    bool enabled = true;
};

// Generic runtime creation options. The internal C++ type intentionally has
// no reserved[]; versioning belongs to the Stable C ABI structs
// (struct_size / struct_version / reserved).
struct RuntimeCreationOptions
{
    RuntimeCompatibilityProfile compatibility =
        RuntimeCompatibilityProfile::Raw;
    RuntimePhysicsSettings physics;
};
}  // namespace wisteria
