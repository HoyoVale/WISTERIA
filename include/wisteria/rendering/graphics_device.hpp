#pragma once

#include <glad/gl.h>

#include "wisteria/rendering/program_cache.hpp"

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

    // Opaque identity of the GL context that owns this device's share group.
    // The platform layer registers the first window context here; core
    // rendering code never needs GLFW. GPU teardown verifies the current
    // context matches before releasing resources.
    void SetContextToken(const void* token) noexcept;
    const void* ContextToken() const noexcept;
    bool HasContextToken() const noexcept;
    void RequireContextToken(const void* currentContext) const;

    // Frees every GPU resource owned by the device (currently the shader
    // program cache). Must be called with a context of the share group
    // current; the platform layer guarantees this before GLFW teardown.
    void ReleaseAll() noexcept;

    std::size_t ProgramCount() const noexcept;

    // Unified GPU-object deletion. When the calling thread's current GL
    // context is this device's share-group context (or the device has no
    // registered token, i.e. legacy mode), the object is deleted immediately;
    // otherwise it is queued and released by FlushPendingDeletes the next
    // time the owning context is current. This makes cross-thread or
    // after-context resource destruction safe instead of calling glDelete*
    // with no (or the wrong) context current.
    void DeleteResource(ResourceKind kind, GLuint name) noexcept;

    // Deletes every queued object. No-op when the owning context is not
    // current on the calling thread.
    void FlushPendingDeletes() noexcept;

    std::size_t PendingDeleteCount() const noexcept;

    // Platform hook: records which GL context is current on the calling
    // thread. The platform layer calls this after every glfwMakeContextCurrent.
    static void SetCurrentContext(const void* context) noexcept;
    static const void* CurrentContext() noexcept;

private:
    bool ContextIsCurrent() const noexcept;

    std::shared_ptr<ProgramCache> programs = std::make_shared<ProgramCache>();
    const void* contextToken = nullptr;
    std::vector<std::pair<ResourceKind, GLuint>> pendingDeletes;
};
}  // namespace wisteria
