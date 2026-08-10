#pragma once

// R2.0 Phase 0C Step 6A: backend-neutral vertex layout contract.
// This header must never include glad/gl.h or expose GL types. The OpenGL
// backend maps VertexFormat -> GLenum and VertexSemantic -> attribute
// binding/location.

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wisteria
{
inline constexpr std::uint32_t AutomaticAttributeLocation =
    std::numeric_limits<std::uint32_t>::max();

// Engine-semantic vertex component format.
enum class VertexFormat : std::uint8_t
{
    Float32,
    Int32,
    Uint32,
    Uint8
};

// Engine-semantic attribute role. The backend decides the concrete
// binding/location; the CPU asset layer never hard-codes attribute slots.
enum class VertexSemantic : std::uint8_t
{
    Position,
    Normal,
    TexCoord,
    BoneIndices,
    BoneWeights,
    MorphPosition,
    MorphUv0,
    Custom
};

struct VertexAttribute
{
    // Legacy GLSL attribute name (kept for custom-shader compatibility and
    // lookup); new code should prefer semantic.
    std::string name;
    std::uint32_t size = 0U;
    VertexFormat format = VertexFormat::Float32;
    bool normalized = false;
    // True when the shader input is ivec*/uvec* rather than vec*.
    bool integer = false;
    std::uint32_t location = AutomaticAttributeLocation;
    // R2.0 0C: semantic role; OpenGL backend maps it to a binding.
    VertexSemantic semantic = VertexSemantic::Custom;
};

using VertexLayout = std::vector<VertexAttribute>;

// --- legacy compatibility aliases ---------------------------------------
// Pre-0C code used Layout/DataType/FLOAT/etc. defined inside the OpenGL VBO
// wrapper. These aliases keep that code compiling unchanged while the new
// neutral names become authoritative.
using Layout = VertexAttribute;
using DataType = VertexFormat;

inline constexpr VertexFormat FLOAT = VertexFormat::Float32;
inline constexpr VertexFormat INT = VertexFormat::Int32;
inline constexpr VertexFormat UINT = VertexFormat::Uint32;
inline constexpr VertexFormat UCHAR = VertexFormat::Uint8;
}  // namespace wisteria
