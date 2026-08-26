#pragma once

#include <cstdint>

namespace wisteria
{

enum class ToneMappingMode : std::uint8_t
{
    Reinhard = 0,
    Aces = 1
};

struct ToneMappingSettings
{
    ToneMappingMode mode = ToneMappingMode::Aces;
    float exposure = 1.0f;
};

}  // namespace wisteria
