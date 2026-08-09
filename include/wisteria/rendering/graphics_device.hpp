#pragma once

#include <glad/gl.h>

#include "wisteria/rendering/program_cache.hpp"
#include "wisteria/rendering/graphics_context.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace wisteria
{
// The owning root for every GPU resource in one OpenGL share group. An
// Application owns exactly one GraphicsDevice; all of its windows share the
// device's context, and every shader program created through the device
// belongs to it. The owning context must be current before any GPU work, and
// ReleaseAll must run with a context of the share group current.
class GraphicsDevice
{
public:
    enum class ResourceKind
    {
        Texture,
        VertexArray,
        Buffer,
        Framebuffer,
        Renderbuffer
    };

    GraphicsDevice();
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;
    GraphicsDevice(GraphicsDevice&&) noexcept;
    GraphicsDevice& operator=(GraphicsDevice&&) noexcept;

    // The application-wide shader program cache. Materials created through a
    // device-bound ResourceManager share this cache so identical
    // (vertex, fragment) shader pairs compile once per application instead of
    // once per material.
    std::shared_ptr<ProgramCache> Programs() const noexcept;

    // Opaque identity of the GL share group that owns this device's GPU
    // resources. The platform layer registers the share group (all windows
    // sharing resources map to the same token); core rendering code never
    // needs GLFW or EGL. GPU teardown verifies the current share group
    // matches before releasing resources.
    void SetShareGroupToken(GraphicsShareGroupToken token) noexcept;
    GraphicsShareGroupToken ShareGroupToken() const noexcept;
    bool HasShareGroupToken() const noexcept;
    void RequireShareGroupToken(
        GraphicsShareGroupToken currentShareGroup
    ) const;

    // Frees every GPU resource owned by the device (currently the shader
    // program cache). Must be called with a context of the share group
    // current; the platform layer guarantees this before GLFW teardown.
    void ReleaseAll() noexcept;

    std::size_t ProgramCount() const noexcept;

    // Unified GPU-object deletion.
    //
    // Shared objects (Texture/Buffer/Renderbuffer) are deleted immediately
    // when the current share group is this device's share group, otherwise
    // queued until that share group is current again.
    //
    // Context-local objects (VertexArray/Framebuffer) carry owningContext,
    // the native context in whose namespace the object lives. They are
    // deleted immediately only when that exact context is current; otherwise
    // queued with the owner recorded. A sibling context of the same share
    // group must never flush a context-local queue entry.
    void DeleteResource(
        ResourceKind kind,
        GLuint name,
        GraphicsContextToken owningContext = nullptr
    ) noexcept;

    // Deletes every queued object. No-op when the owning context is not
    // current on the calling thread.
    void FlushPendingDeletes() noexcept;

    std::size_t PendingDeleteCount() const noexcept;

    // Platform hooks: record which native context and share group are
    // current on the calling thread. The platform layer calls this after
    // every context activation (GLFW window or headless EGL), mapping the
    // native context to its context token and share-group token.
    static void SetCurrentContext(GraphicsContextToken context) noexcept;
    static GraphicsContextToken CurrentContext() noexcept;
    static void SetCurrentShareGroup(
        GraphicsShareGroupToken shareGroup
    ) noexcept;
    static GraphicsShareGroupToken CurrentShareGroup() noexcept;

private:
    bool ContextIsCurrent() const noexcept;
    static bool ContextLocalIsCurrent(GraphicsContextToken owner) noexcept;
    static bool IsSharedResource(ResourceKind kind) noexcept;

    std::shared_ptr<ProgramCache> programs = std::make_shared<ProgramCache>();
    GraphicsShareGroupToken shareGroupToken = nullptr;
    struct PendingDelete
    {
        ResourceKind kind;
        GLuint name;
        GraphicsContextToken owningContext;
    };
    std::vector<PendingDelete> pendingDeletes;
};
}  // namespace wisteria
