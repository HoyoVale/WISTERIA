#pragma once

#include <cstdint>

// R2.0: engine-semantic backend identity shared by neutral render
// contracts. OpenGL is the only R2.0 backend; Vulkan enters in R2.1 as an
// additive id. Backend-neutral (Gate A0).
namespace wisteria
{
enum class RenderBackendId : std::uint32_t
{
    OpenGL = 1U,
    Vulkan = 2U
};
}  // namespace wisteria
