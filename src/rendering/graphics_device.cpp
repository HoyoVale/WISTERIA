#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/graphics_device.hpp"

#include <stdexcept>

namespace wisteria
{
namespace
{
thread_local const void* gCurrentContext = nullptr;

void DeleteObject(GraphicsDevice::ResourceKind kind, GLuint name) noexcept
{
    switch (kind)
    {
    case GraphicsDevice::ResourceKind::Texture:
        glDeleteTextures(1, &name);
        break;
    case GraphicsDevice::ResourceKind::VertexArray:
        glDeleteVertexArrays(1, &name);
        break;
    case GraphicsDevice::ResourceKind::Buffer:
        glDeleteBuffers(1, &name);
        break;
    case GraphicsDevice::ResourceKind::Framebuffer:
        glDeleteFramebuffers(1, &name);
        break;
    }
}
}

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
    this->FlushPendingDeletes();
    this->programs->Clear();
}

std::size_t GraphicsDevice::ProgramCount() const noexcept
{
    return this->programs->Size();
}

void GraphicsDevice::SetCurrentContext(const void* context) noexcept
{
    gCurrentContext = context;
}

const void* GraphicsDevice::CurrentContext() noexcept
{
    return gCurrentContext;
}

bool GraphicsDevice::ContextIsCurrent() const noexcept
{
    // A null token means legacy/unmanaged usage where callers already
    // guarantee a current context; delete immediately to preserve behavior.
    return this->contextToken == nullptr ||
        gCurrentContext == this->contextToken;
}

void GraphicsDevice::DeleteResource(ResourceKind kind, GLuint name) noexcept
{
    if (name == 0U)
        return;
    if (this->ContextIsCurrent())
    {
        DeleteObject(kind, name);
        return;
    }
    this->pendingDeletes.emplace_back(kind, name);
}

void GraphicsDevice::FlushPendingDeletes() noexcept
{
    if (!this->ContextIsCurrent())
        return;
    for (const auto& [kind, name] : this->pendingDeletes)
        DeleteObject(kind, name);
    this->pendingDeletes.clear();
}

std::size_t GraphicsDevice::PendingDeleteCount() const noexcept
{
    return this->pendingDeletes.size();
}
}  // namespace wisteria
