#pragma once

#include "wisteria/assets/importer.hpp"

#include <filesystem>

// PMX/PMD/VMD/VPD importer backed by Saba's own parsers. Output stays in the
// format-agnostic ImportedModelData so the resource layer is unchanged.
class SabaMmdImporter final : public ModelImporter
{
public:
    ImportedModelData Import(
        const std::filesystem::path& filePath
    ) const override;
};
