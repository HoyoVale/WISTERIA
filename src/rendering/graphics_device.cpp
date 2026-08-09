#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/graphics_device.hpp"

#include <stdexcept>

namespace wisteria
{
namespace
{
thread_local GraphicsContextToken gCurrentContext = nullptr;
thread_local GraphicsShareGroupToken gCurrentShareGroup = nullptr;

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
    case GraphicsDevice::ResourceKind::Renderbuffer:
        glDeleteRenderbuffers(1, &name);
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

void GraphicsDevice::SetShareGroupToken(
    GraphicsShareGroupToken token
) noexcept
{
    this->shareGroupToken = token;
}

GraphicsShareGroupToken GraphicsDevice::ShareGroupToken() const noexcept
{
    return this->shareGroupToken;
}

bool GraphicsDevice::HasShareGroupToken() const noexcept
{
    return this->shareGroupToken != nullptr;
}

void GraphicsDevice::RequireShareGroupToken(
    GraphicsShareGroupToken currentShareGroup
) const
{
    if (this->shareGroupToken != currentShareGroup)
    {
        throw std::logic_error(
            "GraphicsDevice operation requires its owning GL share group"
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

void GraphicsDevice::SetCurrentContext(
    GraphicsContextToken context
) noexcept
{
    gCurrentContext = context;
}

GraphicsContextToken GraphicsDevice::CurrentContext() noexcept
{
    return gCurrentContext;
}

void GraphicsDevice::SetCurrentShareGroup(
    GraphicsShareGroupToken shareGroup
) noexcept
{
    gCurrentShareGroup = shareGroup;
}

GraphicsShareGroupToken GraphicsDevice::CurrentShareGroup() noexcept
{
    return gCurrentShareGroup;
}

bool GraphicsDevice::ContextIsCurrent() const noexcept
{
    // A null token means legacy/unmanaged usage where callers already
    // guarantee a current context; delete immediately to preserve behavior.
    return this->shareGroupToken == nullptr ||
        gCurrentShareGroup == this->shareGroupToken;
}

bool GraphicsDevice::ContextLocalIsCurrent(
    GraphicsContextToken owner
) noexcept
{
    // A null owner means legacy/unmanaged usage; the caller already
    // guarantees a current context, so delete immediately.
    return owner == nullptr || gCurrentContext == owner;
}

bool GraphicsDevice::IsSharedResource(ResourceKind kind) noexcept
{
    return kind == ResourceKind::Texture ||
        kind == ResourceKind::Buffer ||
        kind == ResourceKind::Renderbuffer;
}

void GraphicsDevice::DeleteResource(
    ResourceKind kind,
    GLuint name,
    GraphicsContextToken owningContext
) noexcept
{
    if (name == 0U)
        return;

    if (IsSharedResource(kind))
    {
        if (this->ContextIsCurrent())
        {
            DeleteObject(kind, name);
            return;
        }
        this->pendingDeletes.push_back(PendingDelete{kind, name, nullptr});
        return;
    }

    if (ContextLocalIsCurrent(owningContext))
    {
        DeleteObject(kind, name);
        return;
    }
    this->pendingDeletes.push_back(
        PendingDelete{kind, name, owningContext}
    );
}

void GraphicsDevice::FlushPendingDeletes() noexcept
{
    if (this->pendingDeletes.empty())
        return;

    std::vector<PendingDelete> remaining;
    remaining.reserve(this->pendingDeletes.size());
    for (const PendingDelete& entry : this->pendingDeletes)
    {
        if (IsSharedResource(entry.kind))
        {
            if (this->ContextIsCurrent())
                DeleteObject(entry.kind, entry.name);
            else
                remaining.push_back(entry);
        }
        else
        {
            if (ContextLocalIsCurrent(entry.owningContext))
                DeleteObject(entry.kind, entry.name);
            else
                remaining.push_back(entry);
        }
    }
    this->pendingDeletes = std::move(remaining);
}

std::size_t GraphicsDevice::PendingDeleteCount() const noexcept
{
    return this->pendingDeletes.size();
}
}  // namespace wisteria
