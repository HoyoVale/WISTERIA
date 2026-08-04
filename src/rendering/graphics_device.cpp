#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/graphics_device.hpp"

#include <stdexcept>

namespace wisteria
{
GraphicsDevice::GraphicsDevice() = default;
GraphicsDevice::~GraphicsDevice() = default;
GraphicsDevice::GraphicsDevice(GraphicsDevice&&) noexcept = default;
GraphicsDevice& GraphicsDevice::operator=(GraphicsDevice&&) noexcept =
    default;

std::shared_ptr<ProgramCache> GraphicsDevice::Programs() const noexcept
{
    return this->programs;
}

void GraphicsDevice::SetContextToken(const void* token) noexcept
{
    this->contextToken = token;
}

const void* GraphicsDevice::ContextToken() const noexcept
{
    return this->contextToken;
}

bool GraphicsDevice::HasContextToken() const noexcept
{
    return this->contextToken != nullptr;
}

void GraphicsDevice::RequireContextToken(const void* currentContext) const
{
    if (this->contextToken != currentContext)
    {
        throw std::logic_error(
            "GraphicsDevice operation requires its owning GL context"
        );
    }
}

void GraphicsDevice::ReleaseAll() noexcept
{
    this->programs->Clear();
}

std::size_t GraphicsDevice::ProgramCount() const noexcept
{
    return this->programs->Size();
}
}  // namespace wisteria
