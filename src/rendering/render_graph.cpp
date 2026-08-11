#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/render_graph.hpp"

#include <algorithm>
#include <functional>
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
    RenderResourceKind kind,
    RenderResourceLifetime lifetime
)
{
    const std::string key(name);
    if (this->resources.contains(key))
    {
        throw std::invalid_argument(
            "RenderGraph resource is already registered"
        );
    }
    this->resources.emplace(key, std::make_pair(kind, lifetime));
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

void RenderGraph::SetPassCallback(
    RenderPassId pass,
    std::function<void()> callback
)
{
    if (!this->passes.contains(pass))
    {
        throw std::invalid_argument(
            "RenderGraph callback references an unknown pass"
        );
    }
    if (!callback)
    {
        throw std::invalid_argument(
            "RenderGraph callback must not be empty"
        );
    }
    this->callbacks[pass] = std::move(callback);
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

    // Unordered resource hazard gate: for every logical resource, any two
    // distinct passes with at least one write must be ordered by an
    // explicit dependency path. The deterministic tie-break must never
    // silently decide a hazard; the DAG must express it.
    std::unordered_map<
        RenderPassId,
        std::unordered_set<RenderPassId>
    > reaches;
    for (const auto& [id, node] : this->passes)
    {
        std::unordered_set<RenderPassId> visited;
        std::function<void(RenderPassId)> visit =
            [&](RenderPassId current)
        {
            for (const RenderPassId successor : adjacency[current])
            {
                if (visited.insert(successor).second)
                    visit(successor);
            }
        };
        visit(id);
        reaches[id] = std::move(visited);
    }

    std::unordered_map<
        std::string,
        std::vector<std::pair<RenderPassId, RenderResourceAccess>>
    > accessesByResource;
    for (const auto& [pass, list] : this->accesses)
    {
        for (const auto& [resource, access] : list)
            accessesByResource[resource].push_back({pass, access});
    }
    for (const auto& [resource, pairs] : accessesByResource)
    {
        for (std::size_t i = 0U; i < pairs.size(); ++i)
        {
            for (std::size_t j = i + 1U; j < pairs.size(); ++j)
            {
                const auto& a = pairs[i];
                const auto& b = pairs[j];
                if (a.first == b.first)
                    continue;
                if (a.second == RenderResourceAccess::Read &&
                    b.second == RenderResourceAccess::Read)
                {
                    continue;
                }
                const bool ordered =
                    reaches[a.first].contains(b.first) ||
                    reaches[b.first].contains(a.first);
                if (!ordered)
                {
                    throw std::invalid_argument(
                        "RenderGraph contains an unordered resource hazard"
                    );
                }
            }
        }
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

void RenderGraph::Execute()
{
    const std::vector<RenderPassId>& order = this->OrderedPasses();
    // Preflight: every pass must be wired before ANY pass executes, so a
    // wiring error can never leave a half-executed frame.
    for (const RenderPassId id : order)
    {
        const auto iterator = this->callbacks.find(id);
        if (iterator == this->callbacks.end() || !iterator->second)
        {
            throw std::logic_error(
                "RenderGraph pass has no execution callback"
            );
        }
    }
    for (const RenderPassId id : order)
    {
        const auto iterator = this->callbacks.find(id);
        iterator->second();
    }
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
