#pragma once

#include "wisteria/rendering/headless_context.hpp"

#include <memory>

namespace wisteria
{
// Provider factory. Returns nullptr (with a diagnostic on stderr) when the
// provider cannot be built or initialized; callers may then decide to fall
// back to a reference provider.
std::unique_ptr<IHeadlessContext> CreateHeadlessContext(
    const HeadlessContextOptions& options = {}
);
}  // namespace wisteria
