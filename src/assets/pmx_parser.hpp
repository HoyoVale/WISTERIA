#pragma once

#include "pmx_metadata.hpp"

#include <cstdint>
#include <vector>

// Parses PMX binary metadata. Throws std::runtime_error on malformed input.
namespace wisteria
{
PmxMetadata ParsePmxMetadata(const std::vector<std::uint8_t>& bytes);

}  // namespace wisteria
