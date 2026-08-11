#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/render_graph.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace wisteria
{
void RenderGraph::AddPass(const RenderPassDescriptor& descriptor)
{
    if (this->passes.contains(descriptor.id))
    {
        throw std::invalid_argument(
            "RenderGraph pass is already registered"
        );
    }
    PassNode node;
    node.name = std::string(descriptor.name);
    node.dependencies = descriptor.dependencies;
    this->passes.emplace(descriptor.id, std::move(node));
    this->validated = false;
}

void RenderGraph::AddResource(
    std::string_view name,
    RenderResourceKind kind
)
{
    const std::string key(name);
    if (this->resources.contains(key))
    {
        throw std::invalid_argument(
            "RenderGraph resource is already registered"
        );
    }
    this->resources.emplace(key, kind);
    this->validated = false;
}

void RenderGraph::AddAccess(
    RenderPassId pass,
    std::string_view resource,
    RenderResourceAccess access
)
{
    if (!this->passes.contains(pass))
    {
        throw std::invalid_argument(
            "RenderGraph access references an unknown pass"
        );
    }
    if (!this->resources.contains(std::string(resource)))
    {
        throw std::invalid_argument(
            "RenderGraph access references an unknown resource"
        );
    }
    this->accesses[pass].emplace_back(
        std::string(resource),
        access
    );
    this->validated = false;
}

void RenderGraph::Validate() const
{
    if (this->validated)
        return;

    // Kahn topological sort with cycle detection.
    std::unordered_map<RenderPassId, std::size_t> inDegree;
    std::unordered_map<RenderPassId, std::vector<RenderPassId>> adjacency;
    for (const auto& [id, node] : this->passes)
    {
        inDegree[id] = node.dependencies.size();
        for (const RenderPassId dependency : node.dependencies)
        {
            if (!this->passes.contains(dependency))
            {
                throw std::invalid_argument(
                    "RenderGraph pass dependency is not registered"
                );
            }
            adjacency[dependency].push_back(id);
        }
    }

    // Deterministic topological order: process ready passes by enum value
    // (RenderPassId is the canonical current pass order), so the graph
    // output is stable regardless of container iteration order.
    std::priority_queue<
        RenderPassId,
        std::vector<RenderPassId>,
        std::greater<RenderPassId>
    > ready;
    for (const auto& [id, degree] : inDegree)
    {
        if (degree == 0U)
        ready.push(id);
    }

    std::vector<RenderPassId> order;
    order.reserve(this->passes.size());
    while (!ready.empty())
    {
        const RenderPassId current = ready.top();
        ready.pop();
        order.push_back(current);
        for (const RenderPassId successor : adjacency[current])
        {
            std::size_t& degree = inDegree[successor];
            if (degree == 0U)
            {
                throw std::logic_error(
                    "RenderGraph internal validation state corrupted"
                );
            }
            --degree;
            if (degree == 0U)
                ready.push(successor);
        }
    }
    if (order.size() != this->passes.size())
    {
        throw std::invalid_argument(
            "RenderGraph contains a dependency cycle"
        );
    }

    this->orderedPasses = std::move(order);
    this->validated = true;
}

const std::vector<RenderPassId>& RenderGraph::OrderedPasses() const
{
    if (!this->validated)
        this->Validate();
    return this->orderedPasses;
}

bool RenderGraph::HasPass(RenderPassId id) const
{
    return this->passes.contains(id);
}

std::size_t RenderGraph::PassCount() const noexcept
{
    return this->passes.size();
}

std::size_t RenderGraph::ResourceCount() const noexcept
{
    return this->resources.size();
}
}  // namespace wisteria
