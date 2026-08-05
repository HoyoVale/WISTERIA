#pragma once

#include "wisteria/rendering/model.hpp"

namespace wisteria
{
// Procedural mesh generators for asset-free scene building. All outputs use
// the engine's standard interleaved layout
// (pos3 color3 uv2 normal3 tangent4) and front-facing windings.

DefaultModelData BuildSphereMeshData(
    float radius,
    int stacks,
    int slices
);

DefaultModelData BuildCylinderMeshData(
    float radius,
    float height,
    int segments
);

DefaultModelData BuildCapsuleMeshData(
    float radius,
    float height,
    int segments
);
}  // namespace wisteria
