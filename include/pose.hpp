#pragma once

#include "skeleton.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct BoneTransform
{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 Matrix() const;
    static BoneTransform FromMatrix(const glm::mat4& matrix);
};

// Per-model-instance skeleton state. Local matrices are initialized from the
// shared bind pose; global and skinning matrices are evaluated lazily.
class Pose
{
public:
    explicit Pose(const Skeleton& skeleton);

    const Skeleton& GetSkeleton() const noexcept;
    std::size_t BoneCount() const noexcept;

    void ResetToBindPose();
    void SetLocalMatrix(BoneIndex boneIndex, const glm::mat4& matrix);
    void SetLocalTransform(
        BoneIndex boneIndex,
        const BoneTransform& transform
    );
    void SetLocalMatrices(std::span<const glm::mat4> matrices);

    const glm::mat4& LocalMatrix(BoneIndex boneIndex) const;
    std::span<const glm::mat4> LocalMatrices() const noexcept;

    const glm::mat4& GlobalMatrix(BoneIndex boneIndex) const;
    std::span<const glm::mat4> GlobalMatrices() const;
    std::span<const glm::mat4> SkinningMatrices() const;

    bool IsDirty() const noexcept;
    std::uint64_t Revision() const noexcept;
    void Recalculate() const;

private:
    std::size_t CheckedIndex(BoneIndex boneIndex) const;

    const Skeleton* skeleton = nullptr;
    std::vector<glm::mat4> localMatrices;
    mutable std::vector<glm::mat4> globalMatrices;
    mutable std::vector<glm::mat4> skinningMatrices;
    mutable bool dirty = true;
    std::uint64_t revision = 0;
};
