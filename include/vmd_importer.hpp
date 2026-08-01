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
    std::vector<std::string> unmatchedBoneNames;
    AnimationClip clip;
};

// Imports the skeletal portion of a Vocaloid Motion Data 0002 file. Morph,
// camera, light, self-shadow and IK-state tracks remain separate future
// systems and are intentionally not folded into AnimationClip.
class VmdImporter
{
public:
    ImportedVmdAnimationData Import(
        const std::filesystem::path& filePath,
        const Skeleton& skeleton,
        const VmdImportOptions& options = {}
    ) const;

    // Memory overload keeps parsing testable and is useful for packed assets.
    ImportedVmdAnimationData Import(
        std::span<const std::uint8_t> bytes,
        const Skeleton& skeleton,
        std::string sourceName,
        const VmdImportOptions& options = {}
    ) const;
};
