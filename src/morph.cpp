#include "pch.hpp"
#include "morph.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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
            definition.kind > MorphKind::Group)
            throw std::invalid_argument("Morph kind is invalid");
        if (definition.kind == MorphKind::Vertex &&
            !definition.groupMembers.empty())
        {
            throw std::invalid_argument(
                "Vertex morph cannot contain group members"
            );
        }
        std::unordered_set<MorphIndex> memberIndices;
        for (const GroupMorphMember& member : definition.groupMembers)
        {
            if (definition.kind != MorphKind::Group ||
                static_cast<std::size_t>(member.morphIndex) >=
                    this->definitions.size() ||
                !std::isfinite(member.weight) ||
                !memberIndices.emplace(member.morphIndex).second)
            {
                throw std::invalid_argument(
                    "Group morph contains an invalid member"
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

    output.assign(this->definitions.size(), 0.0f);
    const std::function<void(MorphIndex, float)> accumulate =
        [&](MorphIndex morphIndex, float weight)
    {
        if (weight == 0.0f)
            return;
        if (!std::isfinite(weight))
            throw std::overflow_error("Expanded morph weight is not finite");
        const MorphDefinition& definition = this->definitions[morphIndex];
        if (definition.kind == MorphKind::Vertex)
        {
            const float next = output[morphIndex] + weight;
            if (!std::isfinite(next))
                throw std::overflow_error("Expanded morph weight overflowed");
            output[morphIndex] = next;
            return;
        }
        for (const GroupMorphMember& member : definition.groupMembers)
            accumulate(member.morphIndex, weight * member.weight);
    };

    for (std::size_t index = 0; index < source.size(); ++index)
    {
        accumulate(
            static_cast<MorphIndex>(index),
            source[index]
        );
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
