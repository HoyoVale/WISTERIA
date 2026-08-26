#pragma once

// Internal VRM 0.x semantic parser. It bridges the vendored VRM.h data model
// to the engine-owned wisteria::VrmMetadata used by ModelAsset and runtime
// code. This header deliberately stays out of include/wisteria.

#include <nlohmann/json.hpp>

#include "wisteria/animation/skeleton.hpp"
#include "wisteria/assets/vrm_metadata.hpp"

#include <optional>
#include <string>

namespace wisteria::assets
{

bool HasVrmExtension(const nlohmann::json& gltfJson) noexcept;

// Parses the VRMC_vrm extension from a glTF/GLB JSON root. `skeleton` is
// optional and is only used to resolve humanoid node names to engine bone
// indices. Humanoid entries that cannot be resolved keep
// VrmHumanoidBoneBinding::bone == InvalidBoneIndex.
std::optional<VrmMetadata> ParseVrmMetadata(
    const nlohmann::json& gltfJson,
    const Skeleton* skeleton,
    std::string& error
);

}  // namespace wisteria::assets
