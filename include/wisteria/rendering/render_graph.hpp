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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wisteria
{
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
    Depth
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
    void AddAccess(
        RenderPassId pass,
        std::string_view resource,
        RenderResourceAccess access
    );

    // Validates the DAG and computes the topological order. Idempotent;
    // throws std::invalid_argument on cycle/unknown/duplicate.
    void Validate() const;

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
        std::vector<std::pair<std::string, RenderResourceAccess>>
    > accesses;
};
}  // namespace wisteria
