#pragma once

#include "animation.hpp"
#include "skeleton.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

struct VmdImportOptions
{
    std::string clipName;
    float framesPerSecond = 30.0f;
};

struct ImportedVmdAnimationData
{
    std::string modelName;
    std::size_t sourceBoneTrackCount = 0;
    std::size_t sourceMorphTrackCount = 0;
    std::size_t sourceIkStateTrackCount = 0;
    std::vector<std::string> unmatchedBoneNames;
    std::vector<std::string> unmatchedMorphNames;
    std::vector<std::string> unmatchedIkNames;
    AnimationClip clip;
};

// Imports bone motion, vertex-morph weights and per-frame IK switches from a
// Vocaloid Motion Data 0002 file. Camera, light and self-shadow data remain
// separate future systems and are skipped without losing the following data.
class VmdImporter
{
public:
    ImportedVmdAnimationData Import(
        const std::filesystem::path& filePath,
        const Skeleton& skeleton,
        const VmdImportOptions& options = {},
        const MorphSet* morphSet = nullptr
    ) const;

    // Memory overload keeps parsing testable and is useful for packed assets.
    ImportedVmdAnimationData Import(
        std::span<const std::uint8_t> bytes,
        const Skeleton& skeleton,
        std::string sourceName,
        const VmdImportOptions& options = {},
        const MorphSet* morphSet = nullptr
    ) const;
};
