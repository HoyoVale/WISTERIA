#pragma once

// Internal GLB/VRM JSON chunk extractor. Not installed as a public header:
// asset parsing stays behind ModelImporter/ResourceManager.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wisteria::assets
{

// Parses the JSON chunk of a GLB container. GLB chunk 0 must be JSON per
// the glTF 2.0 binary container specification. Returns nullopt on malformed
// input and writes a human-readable diagnostic to error.
std::optional<nlohmann::json> ParseGlbJson(
    const std::vector<std::uint8_t>& bytes,
    std::string& error
);

}  // namespace wisteria::assets
