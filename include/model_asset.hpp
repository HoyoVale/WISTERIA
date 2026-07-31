#pragma once

#include "render_part.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Shared, scene-independent description of an imported or procedural model.
class ModelAsset
{
public:
    explicit ModelAsset(std::string name);
    ~ModelAsset() = default;

    ModelAsset(const ModelAsset&) = delete;
    ModelAsset& operator=(const ModelAsset&) = delete;
    ModelAsset(ModelAsset&&) = delete;
    ModelAsset& operator=(ModelAsset&&) = delete;

    const std::string& Name() const noexcept;
    std::size_t PartCount() const noexcept;
    std::span<const RenderPart> Parts() const noexcept;

    RenderPart& AddPart(
        Mesh& mesh,
        Material& material,
        const glm::mat4& localTransform = glm::mat4(1.0f)
    );

private:
    std::string name;
    std::vector<RenderPart> parts;
};
