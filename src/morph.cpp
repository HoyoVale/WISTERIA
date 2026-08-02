#include "pch.hpp"
#include "morph.hpp"
#include "pose_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
bool IsFinite(const glm::vec3& value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFinite(const glm::vec4& value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z) &&
        std::isfinite(value.w);
}

bool IsFinite(const glm::quat& value)
{
    return std::isfinite(value.w) &&
        std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

void ValidateMaterialOffset(const MaterialMorphOffset& offset)
{
    if (offset.operation < MaterialMorphOperation::Multiply ||
        offset.operation > MaterialMorphOperation::Add ||
        !IsFinite(offset.diffuse) ||
        !IsFinite(offset.specular) ||
        !std::isfinite(offset.shininess) ||
        !IsFinite(offset.ambient) ||
        !IsFinite(offset.edgeColor) ||
        !std::isfinite(offset.edgeSize) ||
        !IsFinite(offset.textureFactor) ||
        !IsFinite(offset.sphereTextureFactor) ||
        !IsFinite(offset.toonTextureFactor))
    {
        throw std::invalid_argument("Material morph contains an invalid offset");
    }
}

template<typename T>
void ApplyMaterialValue(
    T& value,
    const T& factor,
    MaterialMorphOperation operation,
    float weight
)
{
    if (operation == MaterialMorphOperation::Multiply)
        value *= glm::mix(T(1), factor, weight);
    else
        value += factor * weight;
}

void ApplyMaterialValue(
    float& value,
    float factor,
    MaterialMorphOperation operation,
    float weight
)
{
    if (operation == MaterialMorphOperation::Multiply)
        value *= glm::mix(1.0f, factor, weight);
    else
        value += factor * weight;
}
}

MorphSet::MorphSet(std::vector<MorphDefinition> definitions)
    : definitions(std::move(definitions))
{
    if (this->definitions.empty())
        throw std::invalid_argument("MorphSet must contain at least one morph");
    if (this->definitions.size() >=
        static_cast<std::size_t>(InvalidMorphIndex))
    {
        throw std::length_error("MorphSet contains too many morphs");
    }

    this->indices.reserve(this->definitions.size());
    for (std::size_t index = 0; index < this->definitions.size(); ++index)
    {
        const MorphDefinition& definition = this->definitions[index];
        if (definition.name.empty())
            throw std::invalid_argument("Morph name must not be empty");
        if (definition.category < MorphCategory::System ||
            definition.category > MorphCategory::Other)
        {
            throw std::invalid_argument("Morph category is invalid");
        }
        if (definition.kind < MorphKind::Vertex ||
            definition.kind > MorphKind::Impulse)
        {
            throw std::invalid_argument("Morph kind is invalid");
        }
        this->kinds[static_cast<std::size_t>(definition.kind)] = true;

        if (definition.kind != MorphKind::Group &&
            !definition.groupMembers.empty())
        {
            throw std::invalid_argument(
                "Only Group morphs can contain group members"
            );
        }
        if (definition.kind != MorphKind::Flip &&
            !definition.flipMembers.empty())
        {
            throw std::invalid_argument(
                "Only Flip morphs can contain flip members"
            );
        }
        if (definition.kind != MorphKind::Bone &&
            !definition.boneOffsets.empty())
        {
            throw std::invalid_argument(
                "Only Bone morphs can contain bone offsets"
            );
        }
        if (definition.kind != MorphKind::Material &&
            !definition.materialOffsets.empty())
        {
            throw std::invalid_argument(
                "Only Material morphs can contain material offsets"
            );
        }
        if (definition.kind != MorphKind::Impulse &&
            !definition.impulseOffsets.empty())
        {
            throw std::invalid_argument(
                "Only Impulse morphs can contain impulse offsets"
            );
        }

        std::unordered_set<MorphIndex> memberIndices;
        for (const GroupMorphMember& member : definition.groupMembers)
        {
            if (static_cast<std::size_t>(member.morphIndex) >=
                    this->definitions.size() ||
                !std::isfinite(member.weight) ||
                !memberIndices.emplace(member.morphIndex).second)
            {
                throw std::invalid_argument(
                    "Group morph contains an invalid member"
                );
            }
        }

        for (const FlipMorphMember& member : definition.flipMembers)
        {
            if (static_cast<std::size_t>(member.morphIndex) >=
                    this->definitions.size() ||
                !std::isfinite(member.weight))
            {
                throw std::invalid_argument(
                    "Flip morph contains an invalid member"
                );
            }
        }

        std::unordered_set<BoneIndex> boneIndices;
        for (const BoneMorphOffset& offset : definition.boneOffsets)
        {
            const float lengthSquared = glm::dot(offset.rotation, offset.rotation);
            if (offset.boneIndex == InvalidBoneIndex ||
                !IsFinite(offset.translation) ||
                !IsFinite(offset.rotation) ||
                !std::isfinite(lengthSquared) ||
                lengthSquared <= 0.000001f ||
                !boneIndices.emplace(offset.boneIndex).second)
            {
                throw std::invalid_argument(
                    "Bone morph contains an invalid offset"
                );
            }
        }
        for (const MaterialMorphOffset& offset : definition.materialOffsets)
            ValidateMaterialOffset(offset);
        for (const ImpulseMorphOffset& offset : definition.impulseOffsets)
        {
            if (offset.rigidBodyIndex == InvalidRigidBodyIndex ||
                !IsFinite(offset.velocity) ||
                !IsFinite(offset.torque))
            {
                throw std::invalid_argument(
                    "Impulse morph contains an invalid offset"
                );
            }
        }

        if (!this->indices.emplace(
                definition.name,
                static_cast<MorphIndex>(index)
            ).second)
        {
            throw std::invalid_argument("Morph names must be unique");
        }
    }

    std::vector<std::uint8_t> visitState(this->definitions.size(), 0U);
    const std::function<void(MorphIndex)> visit =
        [&](MorphIndex morphIndex)
    {
        std::uint8_t& state = visitState[morphIndex];
        if (state == 2U)
            return;
        if (state == 1U)
            throw std::invalid_argument("Group morph graph contains a cycle");
        state = 1U;
        const MorphDefinition& definition = this->definitions[morphIndex];
        if (definition.kind == MorphKind::Group)
        {
            for (const GroupMorphMember& member : definition.groupMembers)
                visit(member.morphIndex);
        }
        state = 2U;
    };
    for (std::size_t index = 0; index < this->definitions.size(); ++index)
        visit(static_cast<MorphIndex>(index));
}

std::size_t MorphSet::MorphCount() const noexcept
{
    return this->definitions.size();
}

std::span<const MorphDefinition> MorphSet::Definitions() const noexcept
{
    return this->definitions;
}

const MorphDefinition& MorphSet::DefinitionAt(MorphIndex index) const
{
    if (static_cast<std::size_t>(index) >= this->definitions.size())
        throw std::out_of_range("Morph index is out of range");
    return this->definitions[index];
}

std::optional<MorphIndex> MorphSet::FindMorph(std::string_view name) const
{
    const auto iterator = this->indices.find(std::string(name));
    return iterator == this->indices.end()
        ? std::nullopt
        : std::optional<MorphIndex>(iterator->second);
}

bool MorphSet::HasKind(MorphKind kind) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(kind);
    return index < this->kinds.size() && this->kinds[index];
}

void MorphSet::ExpandWeights(
    std::span<const float> source,
    std::vector<float>& output
) const
{
    if (source.size() != this->definitions.size())
        throw std::invalid_argument("Morph weight count does not match MorphSet");
    for (float weight : source)
    {
        if (!std::isfinite(weight))
            throw std::invalid_argument("Morph weights must be finite");
    }

    // PMX 2.1 Flip morphs are controls, not deformation leaves. They first
    // overwrite the selected target morph value in source-index order. Group
    // entries that reference Flip morphs participate in the same first pass.
    std::vector<float> resolved(source.begin(), source.end());
    const auto applyFlip =
        [&](const MorphDefinition& flip, float control)
    {
        if (control <= 0.0f || flip.flipMembers.empty())
            return;

        std::size_t selected = flip.flipMembers.size() - 1U;
        if (control < 1.0f)
        {
            const double slot = std::floor(
                (static_cast<double>(flip.flipMembers.size()) + 1.0) *
                static_cast<double>(control)
            );
            if (slot < 1.0)
                return;
            selected = std::min(
                static_cast<std::size_t>(slot - 1.0),
                flip.flipMembers.size() - 1U
            );
        }

        const FlipMorphMember& member = flip.flipMembers[selected];
        resolved[member.morphIndex] = member.weight;
    };

    const std::function<void(MorphIndex, float)> applyGroupFlips =
        [&](MorphIndex morphIndex, float weight)
    {
        if (weight == 0.0f)
            return;
        if (!std::isfinite(weight))
            throw std::overflow_error("Expanded Flip morph weight is not finite");

        const MorphDefinition& group = this->definitions[morphIndex];
        for (const GroupMorphMember& member : group.groupMembers)
        {
            const float memberWeight = weight * member.weight;
            if (!std::isfinite(memberWeight))
            {
                throw std::overflow_error(
                    "Expanded Flip morph weight overflowed"
                );
            }
            const MorphDefinition& target =
                this->definitions[member.morphIndex];
            if (target.kind == MorphKind::Flip)
                applyFlip(target, memberWeight);
            else if (target.kind == MorphKind::Group)
                applyGroupFlips(member.morphIndex, memberWeight);
        }
    };

    for (std::size_t index = 0; index < this->definitions.size(); ++index)
    {
        const MorphDefinition& definition = this->definitions[index];
        if (definition.kind == MorphKind::Flip)
            applyFlip(definition, resolved[index]);
        else if (definition.kind == MorphKind::Group)
        {
            applyGroupFlips(
                static_cast<MorphIndex>(index),
                resolved[index]
            );
        }
    }

    output.assign(this->definitions.size(), 0.0f);
    const std::function<void(MorphIndex, float)> accumulate =
        [&](MorphIndex morphIndex, float weight)
    {
        if (weight == 0.0f)
            return;
        if (!std::isfinite(weight))
            throw std::overflow_error("Expanded morph weight is not finite");

        const MorphDefinition& definition = this->definitions[morphIndex];
        if (definition.kind == MorphKind::Flip)
            return;
        if (definition.kind != MorphKind::Group)
        {
            const float next = output[morphIndex] + weight;
            if (!std::isfinite(next))
                throw std::overflow_error("Expanded morph weight overflowed");
            output[morphIndex] = next;
            return;
        }

        for (const GroupMorphMember& member : definition.groupMembers)
        {
            if (this->definitions[member.morphIndex].kind == MorphKind::Flip)
                continue;
            accumulate(member.morphIndex, weight * member.weight);
        }
    };

    for (std::size_t index = 0; index < resolved.size(); ++index)
    {
        accumulate(
            static_cast<MorphIndex>(index),
            resolved[index]
        );
    }
}

void MorphSet::ApplyBoneMorphs(
    std::span<const float> weights,
    PoseBuffer& pose
) const
{
    if (weights.size() != this->definitions.size())
        throw std::invalid_argument("Morph weight count does not match MorphSet");

    for (std::size_t index = 0; index < this->definitions.size(); ++index)
    {
        const MorphDefinition& definition = this->definitions[index];
        if (definition.kind != MorphKind::Bone)
            continue;
        const float weight = weights[index];
        if (!std::isfinite(weight))
            throw std::invalid_argument("Bone morph weight must be finite");
        if (weight == 0.0f)
            continue;

        for (const BoneMorphOffset& offset : definition.boneOffsets)
        {
            if (static_cast<std::size_t>(offset.boneIndex) >= pose.BoneCount())
            {
                throw std::out_of_range(
                    "Bone morph references a bone outside the Skeleton"
                );
            }
            BoneTransform transform = pose.TransformAt(offset.boneIndex);
            transform.translation += offset.translation * weight;
            const glm::quat delta = glm::normalize(glm::slerp(
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                glm::normalize(offset.rotation),
                weight
            ));
            transform.rotation = glm::normalize(transform.rotation * delta);
            pose.SetTransform(offset.boneIndex, transform);
        }
    }
}

void MorphSet::ApplyMaterialMorphs(
    std::uint32_t materialIndex,
    std::span<const float> weights,
    MaterialMorphValues& values
) const
{
    if (weights.size() != this->definitions.size())
        throw std::invalid_argument("Morph weight count does not match MorphSet");

    for (std::size_t index = 0; index < this->definitions.size(); ++index)
    {
        const MorphDefinition& definition = this->definitions[index];
        if (definition.kind != MorphKind::Material)
            continue;
        const float weight = weights[index];
        if (!std::isfinite(weight))
            throw std::invalid_argument("Material morph weight must be finite");
        if (weight == 0.0f)
            continue;

        for (const MaterialMorphOffset& offset : definition.materialOffsets)
        {
            if (offset.materialIndex != AllMaterialMorphTargets &&
                offset.materialIndex != materialIndex)
            {
                continue;
            }
            ApplyMaterialValue(
                values.diffuse,
                offset.diffuse,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.specular,
                offset.specular,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.shininess,
                offset.shininess,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.ambient,
                offset.ambient,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.edgeColor,
                offset.edgeColor,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.edgeSize,
                offset.edgeSize,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.textureFactor,
                offset.textureFactor,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.sphereTextureFactor,
                offset.sphereTextureFactor,
                offset.operation,
                weight
            );
            ApplyMaterialValue(
                values.toonTextureFactor,
                offset.toonTextureFactor,
                offset.operation,
                weight
            );
        }
    }
}

void MorphSet::EvaluateImpulseMorphs(
    std::span<const float> weights,
    std::vector<MmdRigidBodyImpulse>& output
) const
{
    if (weights.size() != this->definitions.size())
        throw std::invalid_argument("Morph weight count does not match MorphSet");

    output.clear();
    std::unordered_map<RigidBodyIndex, std::size_t> outputIndices;
    for (std::size_t index = 0; index < this->definitions.size(); ++index)
    {
        const MorphDefinition& definition = this->definitions[index];
        if (definition.kind != MorphKind::Impulse)
            continue;

        const float weight = weights[index];
        if (!std::isfinite(weight))
            throw std::invalid_argument("Impulse morph weight must be finite");
        if (weight == 0.0f)
            continue;

        for (const ImpulseMorphOffset& offset : definition.impulseOffsets)
        {
            auto [iterator, inserted] = outputIndices.emplace(
                offset.rigidBodyIndex,
                output.size()
            );
            if (inserted)
            {
                MmdRigidBodyImpulse next;
                next.rigidBodyIndex = offset.rigidBodyIndex;
                output.push_back(next);
            }
            MmdRigidBodyImpulse& impulse = output[iterator->second];

            const bool stopControl =
                offset.velocity == glm::vec3(0.0f) &&
                offset.torque == glm::vec3(0.0f);
            if (stopControl)
            {
                impulse.reset = true;
                continue;
            }

            const glm::vec3 velocity = offset.velocity * weight;
            const glm::vec3 torque = offset.torque * weight;
            if (!IsFinite(velocity) || !IsFinite(torque))
            {
                throw std::overflow_error(
                    "Impulse morph value overflowed"
                );
            }
            if (offset.local)
            {
                impulse.localLinearImpulse += velocity;
                impulse.localTorqueImpulse += torque;
            }
            else
            {
                impulse.globalLinearImpulse += velocity;
                impulse.globalTorqueImpulse += torque;
            }
            if (!IsFinite(impulse.localLinearImpulse) ||
                !IsFinite(impulse.localTorqueImpulse) ||
                !IsFinite(impulse.globalLinearImpulse) ||
                !IsFinite(impulse.globalTorqueImpulse))
            {
                throw std::overflow_error(
                    "Accumulated Impulse morph value overflowed"
                );
            }
        }
    }
}

MorphState::MorphState(const MorphSet& morphSet)
    : morphSet(&morphSet),
      weights(morphSet.MorphCount(), 0.0f),
      effectiveWeights(morphSet.MorphCount(), 0.0f),
      effectiveRevision(std::numeric_limits<std::uint64_t>::max())
{
}

const MorphSet& MorphState::GetMorphSet() const noexcept
{
    return *this->morphSet;
}

std::size_t MorphState::MorphCount() const noexcept
{
    return this->weights.size();
}

float MorphState::Weight(MorphIndex index) const
{
    return this->weights[this->CheckedIndex(index)];
}

float MorphState::Weight(std::string_view name) const
{
    const std::optional<MorphIndex> index = this->morphSet->FindMorph(name);
    if (!index.has_value())
        throw std::out_of_range("Morph name does not exist: " + std::string(name));
    return this->Weight(*index);
}

void MorphState::SetWeight(MorphIndex index, float weight)
{
    if (!std::isfinite(weight))
        throw std::invalid_argument("Morph weight must be finite");
    const std::size_t checked = this->CheckedIndex(index);
    if (this->weights[checked] == weight)
        return;
    this->weights[checked] = weight;
    ++this->revision;
}

void MorphState::SetWeight(std::string_view name, float weight)
{
    const std::optional<MorphIndex> index = this->morphSet->FindMorph(name);
    if (!index.has_value())
        throw std::out_of_range("Morph name does not exist: " + std::string(name));
    this->SetWeight(*index, weight);
}

void MorphState::SetWeights(std::span<const float> weights)
{
    if (weights.size() != this->weights.size())
        throw std::invalid_argument("Morph weight count does not match MorphSet");
    for (float weight : weights)
    {
        if (!std::isfinite(weight))
            throw std::invalid_argument("Morph weights must be finite");
    }
    if (std::equal(weights.begin(), weights.end(), this->weights.begin()))
        return;
    std::copy(weights.begin(), weights.end(), this->weights.begin());
    ++this->revision;
}

void MorphState::Reset() noexcept
{
    if (std::all_of(
            this->weights.begin(),
            this->weights.end(),
            [](float weight) { return weight == 0.0f; }
        ))
    {
        return;
    }
    std::fill(this->weights.begin(), this->weights.end(), 0.0f);
    ++this->revision;
}

std::span<const float> MorphState::Weights() const noexcept
{
    return this->weights;
}

std::span<const float> MorphState::EffectiveWeights() const
{
    if (this->effectiveRevision != this->revision)
    {
        this->morphSet->ExpandWeights(
            this->weights,
            this->effectiveWeights
        );
        this->effectiveRevision = this->revision;
    }
    return this->effectiveWeights;
}

void MorphState::EvaluateImpulseMorphs(
    std::vector<MmdRigidBodyImpulse>& output
) const
{
    this->morphSet->EvaluateImpulseMorphs(
        this->EffectiveWeights(),
        output
    );
}

std::uint64_t MorphState::Revision() const noexcept
{
    return this->revision;
}

std::size_t MorphState::CheckedIndex(MorphIndex index) const
{
    if (static_cast<std::size_t>(index) >= this->weights.size())
        throw std::out_of_range("Morph index is out of range");
    return static_cast<std::size_t>(index);
}
