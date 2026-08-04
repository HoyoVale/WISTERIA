#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/pose.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/light.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <btBulletDynamicsCommon.h>
#include <Saba/Model/MMD/MMDCamera.h>
#include <Saba/Model/MMD/MMDPhysics.h>
#include <Saba/Model/MMD/PMXModel.h>
#include <Saba/Model/MMD/VMDAnimation.h>
#include <Saba/Model/MMD/VMDCameraAnimation.h>
#include <Saba/Model/MMD/VMDFile.h>

#include <cstddef>
#include <cmath>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace wisteria
{
namespace
{
// Marker instance so Scene knows Saba drives its own per-model Bullet world
// and must skip the shared StepFixed lifecycle.
class SabaOwnedPhysicsInstance final : public PhysicsInstance
{
public:
    bool OwnsSimulationStep() const noexcept override
    {
        return true;
    }

    void PrepareSimulation(float) override
    {
    }

    void FinishSimulation() override
    {
    }

    void ResetSimulation() override
    {
    }
};
}

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
    SabaPhysicsSettings physicsSettings;
    std::shared_ptr<saba::PMXModel> model;
    std::unique_ptr<saba::VMDAnimation> vmdAnimation;
    saba::VMDFile vmdFile;
    bool vmdLoaded = false;
    bool motionLooping = true;
    bool motionPaused = false;
    std::unique_ptr<saba::VMDCameraAnimation> cameraAnimation;
    std::optional<LightTrack> lightTrack;
    double vmdFrame = 0.0;
    double updateMilliseconds = 0.0;
    double uploadMilliseconds = 0.0;
    std::size_t profileFrameCount = 0U;
    std::unique_ptr<SabaOwnedPhysicsInstance> ownedPhysics;

    std::vector<Bone> bones;
    Skeleton skeleton;
    Pose pose;

    Impl(
        std::filesystem::path modelPath_,
        std::filesystem::path vmdPath_,
        SabaPhysicsSettings physicsSettings_
    )
        : modelPath(std::move(modelPath_)),
          vmdPath(std::move(vmdPath_)),
          physicsSettings(physicsSettings_),
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
    std::filesystem::path vmdPath,
    SabaPhysicsSettings physicsSettings
)
    : impl(std::make_unique<Impl>(
          std::move(modelPath),
          std::move(vmdPath),
          physicsSettings
      ))
{
}

SabaMmdRuntimeModel::~SabaMmdRuntimeModel() = default;

void SabaMmdRuntimeModel::SetPhysicsSettings(
    const SabaPhysicsSettings& settings
)
{
    this->impl->physicsSettings = settings;
    if (this->impl->model != nullptr)
    {
        if (saba::MMDPhysics* physics = this->impl->model->GetMMDPhysics())
        {
            physics->SetFPS(1.0f / settings.fixedTimeStep);
            physics->SetMaxSubStepCount(settings.maxSubSteps);
            const glm::vec3& gravity = settings.gravity;
            physics->GetDynamicsWorld()->setGravity(btVector3(
                gravity.x,
                gravity.y,
                gravity.z
            ));
        }
    }
}

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
    if (saba::MMDPhysics* physics = this->impl->model->GetMMDPhysics())
    {
        const float fps = 1.0f / this->impl->physicsSettings.fixedTimeStep;
        physics->SetFPS(fps);
        physics->SetMaxSubStepCount(this->impl->physicsSettings.maxSubSteps);
        const glm::vec3& gravity = this->impl->physicsSettings.gravity;
        physics->GetDynamicsWorld()->setGravity(btVector3(
            gravity.x,
            gravity.y,
            gravity.z
        ));
    }
    this->impl->ownedPhysics =
        std::make_unique<SabaOwnedPhysicsInstance>();

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
    if (!this->impl->motionPaused && this->impl->vmdAnimation != nullptr)
    {
        double nextFrame = this->impl->vmdFrame +
            static_cast<double>(deltaTime) * 30.0;
        const double maxFrame = static_cast<double>(
            this->impl->vmdAnimation->GetMaxKeyTime()
        );
        if (this->impl->motionLooping && maxFrame > 0.0)
            nextFrame = std::fmod(nextFrame, maxFrame);
        this->impl->vmdFrame = nextFrame;
    }
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

bool SabaMmdRuntimeModel::LoadMotion(
    const std::filesystem::path& vmdPath
)
{
    if (this->impl->model == nullptr)
        return false;
    saba::VMDFile vmd;
    const std::string narrowPath = ToNarrowUtf8(vmdPath);
    if (!saba::ReadVMDFile(&vmd, narrowPath.c_str()))
        return false;
    auto animation = std::make_unique<saba::VMDAnimation>();
    if (!animation->Create(this->impl->model))
        return false;
    if (!animation->Add(vmd))
        return false;
    this->impl->vmdAnimation = std::move(animation);
    this->impl->vmdFile = std::move(vmd);
    this->impl->vmdLoaded = true;
    this->impl->vmdFrame = 0.0;
    this->impl->motionPaused = false;
    return true;
}

void SabaMmdRuntimeModel::ClearMotion()
{
    this->impl->vmdAnimation.reset();
    this->impl->vmdFile = saba::VMDFile{};
    this->impl->vmdLoaded = false;
    this->impl->vmdFrame = 0.0;
    this->impl->motionPaused = false;
}

bool SabaMmdRuntimeModel::HasMotion() const noexcept
{
    return this->impl->vmdAnimation != nullptr;
}

void SabaMmdRuntimeModel::SetMotionLooping(bool looping)
{
    this->impl->motionLooping = looping;
}

bool SabaMmdRuntimeModel::IsMotionLooping() const noexcept
{
    return this->impl->motionLooping;
}

void SabaMmdRuntimeModel::PauseMotion()
{
    this->impl->motionPaused = true;
}

void SabaMmdRuntimeModel::ResumeMotion()
{
    this->impl->motionPaused = false;
}

bool SabaMmdRuntimeModel::IsMotionPaused() const noexcept
{
    return this->impl->motionPaused;
}

void SabaMmdRuntimeModel::RestartMotion(bool resetPhysics)
{
    this->impl->vmdFrame = 0.0;
    if (resetPhysics && this->impl->model != nullptr)
        this->impl->model->ResetPhysics();
}

double SabaMmdRuntimeModel::MotionFrame() const noexcept
{
    return this->impl->vmdFrame;
}

void SabaMmdRuntimeModel::SetMotionFrame(double frame)
{
    this->impl->vmdFrame = std::max(0.0, frame);
}

double SabaMmdRuntimeModel::MotionMaxFrame() const noexcept
{
    return this->impl->vmdAnimation != nullptr
        ? static_cast<double>(this->impl->vmdAnimation->GetMaxKeyTime())
        : 0.0;
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
    return this->impl->ownedPhysics.get();
}

void SabaMmdRuntimeModel::SetMmdIkEnabled(BoneIndex, bool)
{
    // TODO(phase 5): bridge VMD IK switches into saba::MMDIkSolver.
}

bool SabaMmdRuntimeModel::LoadCameraMotion(
    const std::filesystem::path& vmdPath
)
{
    saba::VMDFile vmd;
    const std::string narrowPath = ToNarrowUtf8(vmdPath);
    if (!saba::ReadVMDFile(&vmd, narrowPath.c_str()) ||
        vmd.m_cameras.empty())
    {
        return false;
    }
    auto animation = std::make_unique<saba::VMDCameraAnimation>();
    if (!animation->Create(vmd))
        return false;
    this->impl->cameraAnimation = std::move(animation);
    return true;
}

void SabaMmdRuntimeModel::ApplyCameraMotion(float frame, Camera& camera)
{
    if (this->impl->cameraAnimation == nullptr)
        return;
    this->impl->cameraAnimation->Evaluate(frame);
    const saba::MMDCamera& mmdCamera =
        this->impl->cameraAnimation->GetCamera();
    const saba::MMDLookAtCamera look(mmdCamera);
    CameraParam param;
    param.Position = look.m_eye;
    param.Target = look.m_center;
    param.Up = look.m_up;
    param.VerticalFovDegrees = glm::degrees(mmdCamera.m_fov);
    camera.SetParam(param);
}

void SabaMmdRuntimeModel::ApplyCameraTrack(
    const CameraTrack& track,
    float time,
    Camera& camera
)
{
    CameraKeyframe sample;
    if (!track.Sample(time, sample))
        return;
    saba::MMDCamera mmdCamera;
    mmdCamera.m_interest = sample.interest;
    mmdCamera.m_rotate = glm::radians(sample.rotation);
    mmdCamera.m_distance = sample.distance;
    mmdCamera.m_fov = glm::radians(sample.viewAngle);
    const saba::MMDLookAtCamera look(mmdCamera);
    CameraParam param;
    param.Position = look.m_eye;
    param.Target = look.m_center;
    param.Up = look.m_up;
    param.VerticalFovDegrees = sample.viewAngle;
    camera.SetParam(param);
}

bool SabaMmdRuntimeModel::LoadLightMotion(
    const std::filesystem::path& vmdPath
)
{
    saba::VMDFile vmd;
    const std::string narrowPath = ToNarrowUtf8(vmdPath);
    if (!saba::ReadVMDFile(&vmd, narrowPath.c_str()) ||
        vmd.m_lights.empty())
    {
        return false;
    }
    std::vector<LightKeyframe> keys;
    keys.reserve(vmd.m_lights.size());
    for (const saba::VMDLight& light : vmd.m_lights)
    {
        LightKeyframe key;
        key.time = static_cast<float>(light.m_frame);
        key.color = glm::clamp(
            light.m_color,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        key.position = glm::vec3(
            light.m_position.x,
            light.m_position.y,
            -light.m_position.z
        );
        keys.push_back(key);
    }
    this->impl->lightTrack.emplace(std::move(keys));
    return true;
}

void SabaMmdRuntimeModel::ApplyLightMotion(
    float frame,
    DirectionalLight& light
)
{
    if (this->impl->lightTrack.has_value())
        this->ApplyLightTrack(*this->impl->lightTrack, frame, light);
}

void SabaMmdRuntimeModel::ApplyLightTrack(
    const LightTrack& track,
    float time,
    DirectionalLight& light
)
{
    LightKeyframe sample;
    if (!track.Sample(time, sample))
        return;
    light.SetColor(glm::clamp(
        sample.color,
        glm::vec3(0.0f),
        glm::vec3(1.0f)
    ));
    const float positionLength = glm::length(sample.position);
    const glm::vec3 direction = positionLength > 0.000001f
        ? -sample.position / positionLength
        : glm::vec3(0.0f, -1.0f, 0.0f);
    light.SetDirection(direction);
}

MmdSkinningKind SabaMmdRuntimeModel::SkinningKind() const noexcept
{
    // saba::PMXModel blends BDEF/SDEF/QDEF per vertex; report the generic
    // CPU-skinned mode until a finer classification is needed.
    return MmdSkinningKind::LinearBlend;
}

PhysicsInstance* SabaMmdRuntimeModel::GetMmdPhysics() noexcept
{
    return this->impl->ownedPhysics.get();
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
}  // namespace wisteria
