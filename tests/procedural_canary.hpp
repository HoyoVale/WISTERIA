#pragma once

// R1.5 Phase 0B — test-only ProceduralTestBackend.
//
// Proves the runtime abstraction is not Saba-specific through the exact
// chain: Registry → Runtime → ModelInstance → Entity → Snapshot. It reuses
// ModelBackendKind::WisteriaGeneric and never adds a backend kind.
//
// Canary selection convention (test-only): the ModelAsset name selects the
// runtime variant. Production backends never use names this way; this
// shortcut keeps the test backend out of RuntimeCreationOptions, whose
// semantics are frozen for Generic runtimes (§5.5).
//
//   procedural-vertex-canary       vertex-only, no Pose/Morph/Animator
//   procedural-one-bone-canary     1-bone Pose + Animator, no geometry
//   procedural-root-motion-canary  root-motion delta, no Pose/geometry
//   procedural-malformed-canary    3 positions / 0 normals (invalid frame)

#include "test_support.hpp"

#include "wisteria/runtime/model_backend.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
class ProceduralVertexRuntime final : public IModelRuntimeDriver
{
public:
    bool Initialize() override
    {
        this->Rebuild();
        return true;
    }

    void Update(float deltaTime) override
    {
        this->time += deltaTime;
        this->Rebuild();
    }

    void Reset() override
    {
        this->time = 0.0f;
        this->Rebuild();
    }

    Pose* TryGetPose() noexcept override
    {
        return nullptr;
    }

    const Pose* TryGetPose() const noexcept override
    {
        return nullptr;
    }

    bool NeedsDynamicVertexUpload() const noexcept override
    {
        return true;
    }

    ModelVertexFrame VertexFrame() const noexcept override
    {
        return ModelVertexFrame{
            std::span<const glm::vec3>(
                this->positions.data(),
                this->positions.size()
            ),
            std::span<const glm::vec3>(
                this->normals.data(),
                this->normals.size()
            ),
            this->revision
        };
    }

    PhysicsInstance* TryGetPhysicsInstance() noexcept override
    {
        return nullptr;
    }

    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override
    {
        return nullptr;
    }

    std::string_view BackendName() const noexcept override
    {
        return "procedural-canary";
    }

private:
    void Rebuild()
    {
        for (std::size_t index = 0U; index < this->positions.size(); ++index)
        {
            this->positions[index] = glm::vec3(
                static_cast<float>(index) * 0.5f,
                0.25f * std::sin(this->time + static_cast<float>(index)),
                0.0f
            );
            this->normals[index] = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        ++this->revision;
    }

    std::array<glm::vec3, 3> positions{};
    std::array<glm::vec3, 3> normals{
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    float time = 0.0f;
    std::uint64_t revision = 0U;
};

class ProceduralOneBoneRuntime final : public IModelRuntimeDriver
{
public:
    ProceduralOneBoneRuntime()
        : skeleton(MakeSkeleton()),
          pose(this->skeleton),
          animator(this->pose)
    {
    }

    bool Initialize() override
    {
        this->pose.ResetToBindPose();
        return true;
    }

    void Update(float deltaTime) override
    {
        this->time += deltaTime;
        const glm::quat rotation = glm::angleAxis(
            this->time,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        this->pose.SetLocalTransform(
            0U,
            BoneTransform{
                glm::vec3(0.0f),
                rotation,
                glm::vec3(1.0f)
            }
        );
    }

    void Reset() override
    {
        this->time = 0.0f;
        this->pose.ResetToBindPose();
    }

    Pose* TryGetPose() noexcept override
    {
        return &this->pose;
    }

    const Pose* TryGetPose() const noexcept override
    {
        return &this->pose;
    }

    Animator* TryGetAnimator() noexcept override
    {
        return &this->animator;
    }

    const Animator* TryGetAnimator() const noexcept override
    {
        return &this->animator;
    }

    bool NeedsDynamicVertexUpload() const noexcept override
    {
        return false;
    }

    ModelVertexFrame VertexFrame() const noexcept override
    {
        return {};
    }

    PhysicsInstance* TryGetPhysicsInstance() noexcept override
    {
        return nullptr;
    }

    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override
    {
        return nullptr;
    }

    std::string_view BackendName() const noexcept override
    {
        return "procedural-canary";
    }

private:
    static Skeleton MakeSkeleton()
    {
        Bone root;
        root.name = "root";
        root.parentIndex = InvalidBoneIndex;
        root.bindLocalMatrix = glm::mat4(1.0f);
        root.inverseBindMatrix = glm::mat4(1.0f);
        std::vector<Bone> bones;
        bones.push_back(root);
        return Skeleton(std::move(bones));
    }

    Skeleton skeleton;
    Pose pose;
    Animator animator;
    float time = 0.0f;
};

class ProceduralRootMotionRuntime final : public IModelRuntimeDriver
{
public:
    bool Initialize() override
    {
        return true;
    }

    void Update(float deltaTime) override
    {
        this->pending = deltaTime > 0.0f
            ? RootMotionDelta{
                glm::vec3(0.5f * deltaTime, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
            }
            : RootMotionDelta{};
    }

    void Reset() override
    {
        this->pending = {};
    }

    Pose* TryGetPose() noexcept override
    {
        return nullptr;
    }

    const Pose* TryGetPose() const noexcept override
    {
        return nullptr;
    }

    RootMotionDelta ConsumeRootMotion() noexcept override
    {
        const RootMotionDelta result = this->pending;
        this->pending = {};
        ++this->consumeCount;
        return result;
    }

    std::size_t ConsumeCount() const noexcept
    {
        return this->consumeCount;
    }

    bool NeedsDynamicVertexUpload() const noexcept override
    {
        return false;
    }

    ModelVertexFrame VertexFrame() const noexcept override
    {
        return {};
    }

    PhysicsInstance* TryGetPhysicsInstance() noexcept override
    {
        return nullptr;
    }

    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override
    {
        return nullptr;
    }

    std::string_view BackendName() const noexcept override
    {
        return "procedural-canary";
    }

private:
    RootMotionDelta pending;
    std::size_t consumeCount = 0U;
};

class ProceduralMalformedGeometryRuntime final : public IModelRuntimeDriver
{
public:
    bool Initialize() override
    {
        return true;
    }

    void Update(float) override
    {
    }

    void Reset() override
    {
    }

    Pose* TryGetPose() noexcept override
    {
        return nullptr;
    }

    const Pose* TryGetPose() const noexcept override
    {
        return nullptr;
    }

    bool NeedsDynamicVertexUpload() const noexcept override
    {
        return true;
    }

    ModelVertexFrame VertexFrame() const noexcept override
    {
        return ModelVertexFrame{
            std::span<const glm::vec3>(
                this->positions.data(),
                this->positions.size()
            ),
            std::span<const glm::vec3>(
                this->normals.data(),
                this->normals.size()
            ),
            this->revision
        };
    }

    PhysicsInstance* TryGetPhysicsInstance() noexcept override
    {
        return nullptr;
    }

    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override
    {
        return nullptr;
    }

    std::string_view BackendName() const noexcept override
    {
        return "procedural-canary";
    }

private:
    std::array<glm::vec3, 3> positions{};
    std::vector<glm::vec3> normals;  // intentionally empty: malformed frame
    std::uint64_t revision = 1U;
};

class ProceduralTestBackend final : public IModelBackend
{
public:
    ModelBackendKind Kind() const noexcept override
    {
        return ModelBackendKind::WisteriaGeneric;
    }

    std::string_view Name() const noexcept override
    {
        return "procedural-canary";
    }

    std::unique_ptr<IModelRuntimeDriver> CreateRuntime(
        const ModelAsset& asset,
        const RuntimeCreationOptions& options
    ) const override
    {
        (void)options;
        if (asset.Name() == "procedural-vertex-canary")
            return std::make_unique<ProceduralVertexRuntime>();
        if (asset.Name() == "procedural-one-bone-canary")
            return std::make_unique<ProceduralOneBoneRuntime>();
        if (asset.Name() == "procedural-root-motion-canary")
            return std::make_unique<ProceduralRootMotionRuntime>();
        if (asset.Name() == "procedural-malformed-canary")
            return std::make_unique<ProceduralMalformedGeometryRuntime>();
        throw std::invalid_argument(
            "ProceduralTestBackend has no canary for asset: " +
            asset.Name()
        );
    }
};

void ConfigureProceduralCanary(ModelAsset& model)
{
    model.SetBackendKind(ModelBackendKind::WisteriaGeneric);
}
}  // namespace
