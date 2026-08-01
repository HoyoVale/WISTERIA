#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using MorphIndex = std::uint32_t;

inline constexpr MorphIndex InvalidMorphIndex =
    static_cast<MorphIndex>(-1);

enum class MorphCategory : std::uint8_t
{
    System = 0,
    Eyebrow = 1,
    Eye = 2,
    Mouth = 3,
    Other = 4
};

enum class MorphKind : std::uint8_t
{
    Vertex = 0,
    Group = 1
};

struct GroupMorphMember
{
    MorphIndex morphIndex = InvalidMorphIndex;
    float weight = 0.0f;
};

struct MorphDefinition
{
    std::string name;
    MorphCategory category = MorphCategory::Other;
    MorphKind kind = MorphKind::Vertex;
    std::vector<GroupMorphMember> groupMembers;
};

struct VertexMorphOffset
{
    std::uint32_t vertexIndex = 0U;
    glm::vec3 offset{0.0f};
};

struct MeshMorphTarget
{
    MorphIndex morphIndex = InvalidMorphIndex;
    std::vector<VertexMorphOffset> offsets;
};

// Immutable model-level morph name/index table shared by every instance.
class MorphSet
{
public:
    explicit MorphSet(std::vector<MorphDefinition> definitions);

    std::size_t MorphCount() const noexcept;
    std::span<const MorphDefinition> Definitions() const noexcept;
    const MorphDefinition& DefinitionAt(MorphIndex index) const;
    std::optional<MorphIndex> FindMorph(std::string_view name) const;
    void ExpandWeights(
        std::span<const float> source,
        std::vector<float>& output
    ) const;

private:
    std::vector<MorphDefinition> definitions;
    std::unordered_map<std::string, MorphIndex> indices;
};

// Per-Entity runtime weights. It references a shared MorphSet but never
// modifies model or Mesh resources.
class MorphState
{
public:
    explicit MorphState(const MorphSet& morphSet);

    const MorphSet& GetMorphSet() const noexcept;
    std::size_t MorphCount() const noexcept;
    float Weight(MorphIndex index) const;
    float Weight(std::string_view name) const;
    void SetWeight(MorphIndex index, float weight);
    void SetWeight(std::string_view name, float weight);
    void SetWeights(std::span<const float> weights);
    void Reset() noexcept;
    std::span<const float> Weights() const noexcept;
    std::span<const float> EffectiveWeights() const;
    std::uint64_t Revision() const noexcept;

private:
    std::size_t CheckedIndex(MorphIndex index) const;

    const MorphSet* morphSet = nullptr;
    std::vector<float> weights;
    mutable std::vector<float> effectiveWeights;
    mutable std::uint64_t effectiveRevision =
        std::numeric_limits<std::uint64_t>::max();
    std::uint64_t revision = 0U;
};
