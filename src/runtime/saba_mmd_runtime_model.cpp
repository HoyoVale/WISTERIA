#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

#include "wisteria/animation/pose.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <Saba/Model/MMD/PMXModel.h>
#include <Saba/Model/MMD/VMDAnimation.h>
#include <Saba/Model/MMD/VMDFile.h>

#include <cstddef>
#include <chrono>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::string ToNarrowUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(u8.data()),
        u8.size()
    );
}
}

struct SabaMmdRuntimeModel::Impl
{
    std::filesystem::path modelPath;
    std::filesystem::path vmdPath;
    std::shared_ptr<saba::PMXModel> model;
    std::unique_ptr<saba::VMDAnimation> vmdAnimation;
    saba::VMDFile vmdFile;
    bool vmdLoaded = false;
    double vmdFrame = 0.0;
    double updateMilliseconds = 0.0;
    double uploadMilliseconds = 0.0;
    std::size_t profileFrameCount = 0U;

    std::vector<Bone> bones;
    Skeleton skeleton;
    Pose pose;

    Impl(
        std::filesystem::path modelPath_,
        std::filesystem::path vmdPath_
    )
        : modelPath(std::move(modelPath_)),
          vmdPath(std::move(vmdPath_)),
          skeleton(BuildSingleBoneSkeleton()),
          pose(skeleton)
    {
    }

    static Skeleton BuildSingleBoneSkeleton()
    {
        Bone root;
        root.name = "root";
        std::vector<Bone> single{std::move(root)};
        return Skeleton(std::move(single));
    }
};

SabaMmdRuntimeModel::SabaMmdRuntimeModel(
    std::filesystem::path modelPath,
    std::filesystem::path vmdPath
)
    : impl(std::make_unique<Impl>(
          std::move(modelPath),
          std::move(vmdPath)
      ))
{
}

SabaMmdRuntimeModel::~SabaMmdRuntimeModel() = default;

bool SabaMmdRuntimeModel::Initialize()
{
    if (this->impl->model != nullptr)
        return true;

    this->impl->model = std::make_shared<saba::PMXModel>();
    const std::string modelPath = ToNarrowUtf8(this->impl->modelPath);
    const std::string modelDirectory = ToNarrowUtf8(
        this->impl->modelPath.parent_path()
    );
    if (!this->impl->model->Load(modelPath, modelDirectory))
    {
        this->impl->model.reset();
        return false;
    }
    // Saba's viewer calls InitializeAnimation right after loading the model;
    // it resets node animation state and rebuilds the physics reset pose.
    // Skipping it leaves physics and VMD evaluation on inconsistent baselines.
    this->impl->model->InitializeAnimation();

    if (!this->impl->vmdPath.empty())
    {
        const std::string vmdPath = ToNarrowUtf8(this->impl->vmdPath);
        if (!saba::ReadVMDFile(&this->impl->vmdFile, vmdPath.c_str()))
            return false;
        this->impl->vmdAnimation = std::make_unique<saba::VMDAnimation>();
        if (!this->impl->vmdAnimation->Create(this->impl->model))
            return false;
        if (!this->impl->vmdAnimation->Add(this->impl->vmdFile))
            return false;
        this->impl->vmdLoaded = true;
    }
    return true;
}

void SabaMmdRuntimeModel::Update(float deltaTime)
{
    if (this->impl->model == nullptr)
        return;
    const auto updateStart = std::chrono::steady_clock::now();
    this->impl->vmdFrame += static_cast<double>(deltaTime) * 30.0;
    this->impl->model->BeginAnimation();
    this->impl->model->UpdateAllAnimation(
        this->impl->vmdAnimation.get(),
        static_cast<float>(this->impl->vmdFrame),
        deltaTime
    );
    this->impl->model->EndAnimation();
    this->impl->model->Update();
    const auto updateEnd = std::chrono::steady_clock::now();
    this->impl->updateMilliseconds += std::chrono::duration<double, std::milli>(
        updateEnd - updateStart
    ).count();
    ++this->impl->profileFrameCount;
}

void SabaMmdRuntimeModel::Reset()
{
    this->impl->vmdFrame = 0.0;
    if (this->impl->model != nullptr)
        this->impl->model->ResetPhysics();
}

Pose& SabaMmdRuntimeModel::GetPose()
{
    return this->impl->pose;
}

bool SabaMmdRuntimeModel::NeedsDynamicVertexUpload() const noexcept
{
    return true;
}

void SabaMmdRuntimeModel::UploadDynamicVertices(Mesh& mesh)
{
    if (this->impl->model == nullptr)
        return;
    const auto uploadStart = std::chrono::steady_clock::now();
    const std::size_t vertexCount = this->impl->model->GetVertexCount();
    const glm::vec3* updatePositions =
        this->impl->model->GetUpdatePositions();
    const glm::vec3* updateNormals =
        this->impl->model->GetUpdateNormals();
    const std::span<const std::uint32_t> sourceIndices =
        mesh.SourceVertexIndices();
    if (sourceIndices.empty())
    {
        mesh.UploadDynamicVertices(
            std::span<const glm::vec3>(updatePositions, vertexCount),
            std::span<const glm::vec3>(updateNormals, vertexCount)
        );
    }
    else
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        positions.reserve(sourceIndices.size());
        normals.reserve(sourceIndices.size());
        for (const std::uint32_t globalIndex : sourceIndices)
        {
            if (globalIndex < vertexCount)
            {
                positions.push_back(updatePositions[globalIndex]);
                normals.push_back(updateNormals[globalIndex]);
            }
        }
        mesh.UploadDynamicVertices(positions, normals);
    }
    const auto uploadEnd = std::chrono::steady_clock::now();
    this->impl->uploadMilliseconds += std::chrono::duration<double, std::milli>(
        uploadEnd - uploadStart
    ).count();
}

SabaMmdRuntimeModel::ProfileSnapshot SabaMmdRuntimeModel::Profile() const
{
    ProfileSnapshot snapshot;
    snapshot.frameCount = this->impl->profileFrameCount;
    if (snapshot.frameCount > 0U)
    {
        snapshot.averageUpdateMilliseconds =
            this->impl->updateMilliseconds /
            static_cast<double>(snapshot.frameCount);
        snapshot.averageUploadMilliseconds =
            this->impl->uploadMilliseconds /
            (static_cast<double>(snapshot.frameCount) * 24.0);
    }
    return snapshot;
}

PhysicsInstance* SabaMmdRuntimeModel::TryGetPhysicsInstance() noexcept
{
    return nullptr;
}

void SabaMmdRuntimeModel::SetMmdIkEnabled(BoneIndex, bool)
{
    // TODO(phase 5): bridge VMD IK switches into saba::MMDIkSolver.
}

void SabaMmdRuntimeModel::ApplyCameraTrack(
    const CameraTrack&,
    float,
    Camera&
)
{
    // TODO(phase 3): apply VMD camera through SabaMmdRuntimeModel.
}

MmdSkinningKind SabaMmdRuntimeModel::SkinningKind() const noexcept
{
    // saba::PMXModel blends BDEF/SDEF/QDEF per vertex; report the generic
    // CPU-skinned mode until a finer classification is needed.
    return MmdSkinningKind::LinearBlend;
}

PhysicsInstance* SabaMmdRuntimeModel::GetMmdPhysics() noexcept
{
    return nullptr;
}

SabaMmdRuntimeModel::VertexDiagnostics
SabaMmdRuntimeModel::DiagnoseVertices() const
{
    VertexDiagnostics diagnostics;
    if (this->impl->model == nullptr)
        return diagnostics;

    const std::size_t vertexCount = this->impl->model->GetVertexCount();
    diagnostics.vertexCount = vertexCount;
    if (vertexCount == 0U)
        return diagnostics;

    const glm::vec3* positions = this->impl->model->GetUpdatePositions();
    const glm::vec3* bindPositions = this->impl->model->GetPositions();
    diagnostics.minimumPosition = positions[0];
    diagnostics.maximumPosition = positions[0];
    for (std::size_t index = 0U; index < vertexCount; ++index)
    {
        const glm::vec3& position = positions[index];
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            diagnostics.finite = false;
            break;
        }
        diagnostics.minimumPosition = glm::min(
            diagnostics.minimumPosition,
            position
        );
        diagnostics.maximumPosition = glm::max(
            diagnostics.maximumPosition,
            position
        );
        diagnostics.maximumDisplacementFromBind = std::max(
            diagnostics.maximumDisplacementFromBind,
            glm::distance(position, bindPositions[index])
        );
    }
    return diagnostics;
}

std::span<const glm::vec3> SabaMmdRuntimeModel::BindPositions() const
{
    if (this->impl->model == nullptr)
        return {};
    const std::size_t vertexCount = this->impl->model->GetVertexCount();
    return std::span<const glm::vec3>(
        this->impl->model->GetPositions(),
        vertexCount
    );
}

std::vector<std::uint32_t> SabaMmdRuntimeModel::Indices() const
{
    std::vector<std::uint32_t> result;
    if (this->impl->model == nullptr)
        return {};
    const std::size_t elementSize =
        this->impl->model->GetIndexElementSize();
    const std::size_t indexCount =
        this->impl->model->GetIndexCount();
    const std::uint8_t* source = static_cast<const std::uint8_t*>(
        this->impl->model->GetIndices()
    );
    if (source == nullptr)
        return result;
    result.reserve(indexCount);
    for (std::size_t index = 0U; index < indexCount; ++index)
    {
        std::uint32_t value = 0U;
        if (elementSize == 1U)
            value = source[index];
        else if (elementSize == 2U)
            value = reinterpret_cast<const std::uint16_t*>(
                source
            )[index];
        else if (elementSize == 4U)
            value = reinterpret_cast<const std::uint32_t*>(
                source
            )[index];
        else
            break;
        result.push_back(value);
    }
    return result;
}

std::span<const glm::vec3> SabaMmdRuntimeModel::UpdatePositions() const
{
    if (this->impl->model == nullptr)
        return {};
    const std::size_t vertexCount = this->impl->model->GetVertexCount();
    return std::span<const glm::vec3>(
        this->impl->model->GetUpdatePositions(),
        vertexCount
    );
}
