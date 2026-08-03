#pragma once

#include "wisteria/animation/bone.hpp"
#include "wisteria/mmd/physics/mmd_physics_types.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
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
inline constexpr std::uint32_t AllMaterialMorphTargets =
    std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t MmdUvChannelCount = 5U;

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
    Group = 1,
    Bone = 2,
    Uv = 3,
    Material = 4,
    Flip = 5,
    Impulse = 6
};

enum class MaterialMorphOperation : std::uint8_t
{
    Multiply = 0,
    Add = 1
};

struct GroupMorphMember
{
    MorphIndex morphIndex = InvalidMorphIndex;
    float weight = 0.0f;
};

struct FlipMorphMember
{
    MorphIndex morphIndex = InvalidMorphIndex;
    float weight = 0.0f;
};

struct BoneMorphOffset
{
    BoneIndex boneIndex = InvalidBoneIndex;
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct ImpulseMorphOffset
{
    RigidBodyIndex rigidBodyIndex = InvalidRigidBodyIndex;
    bool local = false;
    glm::vec3 velocity{0.0f};
    glm::vec3 torque{0.0f};
};

// Aggregated per-Entity PMX 2.1 impulse request. A physics step should
// process reset flags first, then apply global and local channels separately.
struct MmdRigidBodyImpulse
{
    RigidBodyIndex rigidBodyIndex = InvalidRigidBodyIndex;
    bool reset = false;
    glm::vec3 globalLinearImpulse{0.0f};
    glm::vec3 globalTorqueImpulse{0.0f};
    glm::vec3 localLinearImpulse{0.0f};
    glm::vec3 localTorqueImpulse{0.0f};
};

struct MaterialMorphOffset
{
    std::uint32_t materialIndex = AllMaterialMorphTargets;
    MaterialMorphOperation operation = MaterialMorphOperation::Add;
    glm::vec4 diffuse{0.0f};
    glm::vec3 specular{0.0f};
    float shininess = 0.0f;
    glm::vec3 ambient{0.0f};
    glm::vec4 edgeColor{0.0f};
    float edgeSize = 0.0f;
    glm::vec4 textureFactor{0.0f};
    glm::vec4 sphereTextureFactor{0.0f};
    glm::vec4 toonTextureFactor{0.0f};
};

struct MorphDefinition
{
    std::string name;
    MorphCategory category = MorphCategory::Other;
    MorphKind kind = MorphKind::Vertex;
    std::vector<GroupMorphMember> groupMembers;
    std::vector<FlipMorphMember> flipMembers;
    std::vector<BoneMorphOffset> boneOffsets;
    std::vector<MaterialMorphOffset> materialOffsets;
    std::vector<ImpulseMorphOffset> impulseOffsets;
};

struct VertexMorphOffset
{
    std::uint32_t vertexIndex = 0U;
    glm::vec3 offset{0.0f};
};

struct UvMorphOffset
{
    std::uint32_t vertexIndex = 0U;
    std::uint8_t channel = 0U;
    glm::vec4 offset{0.0f};
};

struct MeshMorphTarget
{
    MorphIndex morphIndex = InvalidMorphIndex;
    std::vector<VertexMorphOffset> offsets;
    std::vector<UvMorphOffset> uvOffsets;
};

struct MorphVertexDelta
{
    glm::vec3 position{0.0f};
    std::array<glm::vec4, MmdUvChannelCount> uv{};
};

struct MaterialMorphValues
{
    glm::vec4 diffuse{1.0f};
    glm::vec3 specular{1.0f};
    float shininess = 32.0f;
    glm::vec3 ambient{0.0f};
    glm::vec4 edgeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;
    glm::vec4 textureFactor{1.0f};
    glm::vec4 sphereTextureFactor{1.0f};
    glm::vec4 toonTextureFactor{1.0f};
};

class PoseBuffer;

// Immutable model-level morph name/index table shared by every instance.
class MorphSet
{
public:
    explicit MorphSet(std::vector<MorphDefinition> definitions);

    std::size_t MorphCount() const noexcept;
    std::span<const MorphDefinition> Definitions() const noexcept;
    const MorphDefinition& DefinitionAt(MorphIndex index) const;
    std::optional<MorphIndex> FindMorph(std::string_view name) const;
    bool HasKind(MorphKind kind) const noexcept;
    void ExpandWeights(
        std::span<const float> source,
        std::vector<float>& output
    ) const;
    void ApplyBoneMorphs(
        std::span<const float> weights,
        PoseBuffer& pose
    ) const;
    void ApplyMaterialMorphs(
        std::uint32_t materialIndex,
        std::span<const float> weights,
        MaterialMorphValues& values
    ) const;
    void EvaluateImpulseMorphs(
        std::span<const float> weights,
        std::vector<MmdRigidBodyImpulse>& output
    ) const;

private:
    std::vector<MorphDefinition> definitions;
    std::unordered_map<std::string, MorphIndex> indices;
    std::array<bool, 7U> kinds{};
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
    void EvaluateImpulseMorphs(
        std::vector<MmdRigidBodyImpulse>& output
    ) const;
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
