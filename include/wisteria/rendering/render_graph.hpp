#pragma once

// R2.0 Phase 0D Stage 2A: RenderGraph core types.
//
// This layer makes the renderer's implicit pass DAG explicit: logical
// passes, logical resources, read/write accesses and dependency/order
// validation. It is backend-neutral (0 glad) and intentionally does NOT
// execute GL: Stage 2B registers the current RenderPacket order, Stage 2C
// migrates pass bodies.
//
// Frozen boundaries (0A contract):
// - RenderGraph owns pass/resource dependencies and execution order;
// - it never updates Scene/Animation/Physics/Runtime;
// - it is never exposed through the Stable C ABI;
// - no aliasing / async / multi-queue / pass merging in this stage.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wisteria
{
struct RenderFramePacket;

// The current implicit execution passes of Renderer::RenderPacket, made
// explicit. SceneColor is NOT a pass: it is the graph output logical
// resource / offline capture boundary (before Present/FXAA).
enum class RenderPassId : std::uint8_t
{
    ShadowDepth,
    GroundReceivers,
    MmdGroundShadow,
    Opaque,
    Skybox,
    Transparent,
    OitComposite,
    PhysicsDebug
};

enum class RenderResourceKind : std::uint8_t
{
    Color,
    Depth,
    // Combined depth+stencil attachment (SceneFramebuffer uses
    // GL_DEPTH24_STENCIL8); accesses must declare Depth or Stencil aspect.
    DepthStencil
};

// Which aspect of a resource one access targets. Distinct aspects of the
// same attachment are independent for hazard purposes (Vulkan maps these
// to separate aspect masks / barriers).
enum class RenderResourceAspect : std::uint8_t
{
    Color,
    Depth,
    Stencil
};

enum class RenderLoadOp : std::uint8_t
{
    // No load semantics (Read access, or default for legacy AddAccess).
    NotApplicable,
    Load,
    Clear
};

enum class RenderStoreOp : std::uint8_t
{
    // No store semantics (Read access, or default for legacy AddAccess).
    NotApplicable,
    Store,
    DontCare
};

// Orthogonal to kind: how long the logical resource lives. Transient only
// means frame-local lifetime; it is NOT resource aliasing (R2.0 excludes
// aliasing entirely).
enum class RenderResourceLifetime : std::uint8_t
{
    External,
    Transient
};

enum class RenderResourceAccess : std::uint8_t
{
    Read,
    Write
};

// Full per-access semantics: target aspect, load/store behavior and the
// clear value used when loadOp == Clear.
struct RenderAccessDesc
{
    std::string resource;
    RenderResourceAccess access = RenderResourceAccess::Read;
    RenderResourceAspect aspect = RenderResourceAspect::Color;
    RenderLoadOp loadOp = RenderLoadOp::NotApplicable;
    RenderStoreOp storeOp = RenderStoreOp::NotApplicable;
    std::array<float, 4> clearValue{0.0f, 0.0f, 0.0f, 1.0f};
};

// First/last pass that touches a Transient resource in execution order.
struct RenderResourceLifetimeSpan
{
    RenderPassId firstUse = RenderPassId::ShadowDepth;
    RenderPassId lastUse = RenderPassId::ShadowDepth;
};

struct RenderPassDescriptor
{
    RenderPassId id = RenderPassId::PhysicsDebug;
    std::string_view name;
    // Passes that must execute before this one.
    std::vector<RenderPassId> dependencies;
};

// Logical frame scheduler. Validation is explicit: an invalid DAG (cycle,
// unknown dependency, duplicate pass/resource, unknown access) throws
// std::invalid_argument instead of silently producing a wrong order.
class RenderGraph
{
public:
    void AddPass(const RenderPassDescriptor& descriptor);
    void AddResource(
        std::string_view name,
        RenderResourceKind kind,
        RenderResourceLifetime lifetime
    );
    void AddAccess(RenderPassId pass, const RenderAccessDesc& desc);
    // Legacy convenience: aspect defaults to the resource kind's primary
    // aspect (Color for Color, Depth for Depth/DepthStencil).
    void AddAccess(
        RenderPassId pass,
        std::string_view resource,
        RenderResourceAccess access
    );
    // Stage 2B: execution callback for a pass. The callback body may call
    // existing engine/OpenGL renderer code; graph migration is incremental.
    void SetPassCallback(
        RenderPassId pass,
        std::function<void()> callback
    );

    // Validates the DAG and computes the topological order. Idempotent;
    // throws std::invalid_argument on cycle/unknown/duplicate.
    void Validate() const;
    // First/last execution-order use of a Transient resource. Requires
    // validation; returns nullopt for unknown or non-transient resources.
    std::optional<RenderResourceLifetimeSpan> TransientLifetime(
        std::string_view resource
    ) const;
    // Executes passes in topological order. Every registered pass must have
    // a callback; execution does not mutate the DAG.
    void Execute();

    const std::vector<RenderPassId>& OrderedPasses() const;
    bool HasPass(RenderPassId id) const;
    std::size_t PassCount() const noexcept;
    std::size_t ResourceCount() const noexcept;

private:
    struct PassNode
    {
        std::string name;
        std::vector<RenderPassId> dependencies;
    };

    mutable std::vector<RenderPassId> orderedPasses;
    mutable bool validated = false;
    std::unordered_map<RenderPassId, PassNode> passes;
    std::unordered_map<
        std::string,
        std::pair<RenderResourceKind, RenderResourceLifetime>
    > resources;
    // pass -> set of resource accesses (resource name, access).
    std::unordered_map<
        RenderPassId,
        std::vector<RenderAccessDesc>
    > accesses;
    std::unordered_map<RenderPassId, std::function<void()>> callbacks;
};

// Stage 2B: build the CURRENT frame's graph from the extracted packet and
// runtime capability options. Passes that do not execute this frame are not
// registered (the graph describes what actually runs, not a maximum
// template). Callbacks are attached by the caller (existing Renderer code).
struct RenderGraphBuildOptions
{
    bool shadowsEnabled = true;
    bool groundShadowEnabled = true;
    bool skyboxEnabled = true;
    bool oitEnabled = true;
};

RenderGraph BuildCurrentRenderGraph(
    const RenderFramePacket& packet,
    const RenderGraphBuildOptions& options = {}
);
}  // namespace wisteria
