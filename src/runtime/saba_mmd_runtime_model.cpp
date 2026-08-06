#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/pose.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/light.hpp"

#include <btBulletDynamicsCommon.h>
#include <Saba/Model/MMD/MMDCamera.h>
#include <Saba/Model/MMD/MMDPhysics.h>
#include <Saba/Model/MMD/MMDMorph.h>
#include <Saba/Model/MMD/PMXModel.h>
#include <Saba/Model/MMD/VMDAnimation.h>
#include <Saba/Model/MMD/VMDCameraAnimation.h>
#include <Saba/Model/MMD/VMDFile.h>

#include <cstddef>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <chrono>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
void ApplyIkEnable(
    const std::shared_ptr<saba::PMXModel>& model,
    const std::string& boneName,
    bool enabled
)
{
    if (model == nullptr)
        return;
    saba::MMDIKManager* ikManager = model->GetIKManager();
    if (ikManager == nullptr)
        return;
    saba::MMDIkSolver* solver = ikManager->GetMMDIKSolver(boneName);
    if (solver != nullptr)
        solver->Enable(enabled);
}

bool IsValidBoneIndex(
    BoneIndex bone,
    const std::vector<Bone>& bones
) noexcept
{
    return bone != InvalidBoneIndex &&
        static_cast<std::size_t>(bone) < bones.size();
}

std::string ToNarrowUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(u8.data()),
        u8.size()
    );
}

glm::vec3 ToGlmVec3(const btVector3& value)
{
    return glm::vec3(
        static_cast<float>(value.x()),
        static_cast<float>(value.y()),
        static_cast<float>(value.z())
    );
}

btVector3 ToBtVector3(const glm::vec3& value)
{
    return btVector3(value.x, value.y, value.z);
}

// R1.2B FNV-1a64 streaming helper for layout/configuration fingerprints.
// Explicit little-endian byte writes keep the fingerprint stable across the
// same build regardless of host endianness.
struct FnvHasher
{
    std::uint64_t state = 14695981039346656037ULL;

    void Byte(std::uint8_t byte)
    {
        state ^= byte;
        state *= 1099511628211ULL;
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            Byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    void I32(std::int32_t value)
    {
        U32(static_cast<std::uint32_t>(value));
    }

    void F32(float value)
    {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    void Vec3(const glm::vec3& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
    }

    void Mat4(const glm::mat4& value)
    {
        for (glm::length_t column = 0; column < 4; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
            {
                F32(value[column][row]);
            }
        }
    }

    void BtVector3(const btVector3& value)
    {
        F32(static_cast<float>(value.x()));
        F32(static_cast<float>(value.y()));
        F32(static_cast<float>(value.z()));
    }

    void BtTransform(const btTransform& value)
    {
        BtVector3(value.getOrigin());
        const btMatrix3x3& basis = value.getBasis();
        for (int column = 0; column < 3; ++column)
        {
            BtVector3(basis.getColumn(column));
        }
    }
};

void FillTransformSnapshot(
    RigidTransformSnapshot& output,
    const btTransform& transform
)
{
    output.position = ToGlmVec3(transform.getOrigin());
    const btMatrix3x3& basis = transform.getBasis();
    for (int column = 0; column < 3; ++column)
    {
        const btVector3 col = basis.getColumn(column);
        output.rotationBasis[static_cast<std::size_t>(column) * 3U + 0U] =
            static_cast<float>(col.x());
        output.rotationBasis[static_cast<std::size_t>(column) * 3U + 1U] =
            static_cast<float>(col.y());
        output.rotationBasis[static_cast<std::size_t>(column) * 3U + 2U] =
            static_cast<float>(col.z());
    }
}

void FillBulletTransform(
    btTransform& output,
    const RigidTransformSnapshot& snapshot
)
{
    // btMatrix3x3(v0, v1, v2) treats the vectors as ROWS. The snapshot
    // stores explicit column-major components, so build rows explicitly to
    // avoid a silent transpose.
    const float c0x = snapshot.rotationBasis[0];
    const float c0y = snapshot.rotationBasis[1];
    const float c0z = snapshot.rotationBasis[2];
    const float c1x = snapshot.rotationBasis[3];
    const float c1y = snapshot.rotationBasis[4];
    const float c1z = snapshot.rotationBasis[5];
    const float c2x = snapshot.rotationBasis[6];
    const float c2y = snapshot.rotationBasis[7];
    const float c2z = snapshot.rotationBasis[8];
    const btMatrix3x3 basis(
        c0x, c1x, c2x,
        c0y, c1y, c2y,
        c0z, c1z, c2z
    );
    output.setBasis(basis);
    output.setOrigin(ToBtVector3(snapshot.position));
}

bool IsPositiveZero(float value) noexcept
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits == 0U;
}

bool IsFiniteBasis(const RigidTransformSnapshot& transform) noexcept
{
    for (float component : transform.rotationBasis)
    {
        if (!std::isfinite(component))
            return false;
    }
    return std::isfinite(transform.position.x) &&
        std::isfinite(transform.position.y) &&
        std::isfinite(transform.position.z);
}

bool IsValidRotationBasis(const RigidTransformSnapshot& transform) noexcept
{
    if (!IsFiniteBasis(transform))
        return false;
    constexpr float kTolerance = 1.0e-3f;
    glm::vec3 columns[3];
    for (int column = 0; column < 3; ++column)
    {
        const std::size_t base = static_cast<std::size_t>(column) * 3U;
        columns[column] = glm::vec3(
            transform.rotationBasis[base + 0U],
            transform.rotationBasis[base + 1U],
            transform.rotationBasis[base + 2U]
        );
        if (std::abs(glm::length(columns[column]) - 1.0f) > kTolerance)
            return false;
    }
    for (int left = 0; left < 3; ++left)
    {
        for (int right = left + 1; right < 3; ++right)
        {
            if (std::abs(glm::dot(columns[left], columns[right])) >
                kTolerance)
            {
                return false;
            }
        }
    }
    const glm::mat3 matrix(
        columns[0],
        columns[1],
        columns[2]
    );
    if (std::abs(glm::determinant(matrix) - 1.0f) > kTolerance)
        return false;
    return true;
}

bool SameTransformBitwise(
    const btTransform& left,
    const RigidTransformSnapshot& right
) noexcept
{
    const btVector3 origin = left.getOrigin();
    if (origin.x() != right.position.x ||
        origin.y() != right.position.y ||
        origin.z() != right.position.z)
    {
        return false;
    }
    const btMatrix3x3& basis = left.getBasis();
    for (int column = 0; column < 3; ++column)
    {
        const btVector3 col = basis.getColumn(column);
        const std::size_t base = static_cast<std::size_t>(column) * 3U;
        if (col.x() != right.rotationBasis[base + 0U] ||
            col.y() != right.rotationBasis[base + 1U] ||
            col.z() != right.rotationBasis[base + 2U])
        {
            return false;
        }
    }
    return true;
}

void HashShapeDimensions(FnvHasher& hasher, const btCollisionShape* shape)
{
    hasher.I32(shape != nullptr ? shape->getShapeType() : -1);
    if (shape == nullptr)
        return;
    if (const auto* sphere = dynamic_cast<const btSphereShape*>(shape))
    {
        hasher.F32(static_cast<float>(sphere->getRadius()));
    }
    else if (const auto* box = dynamic_cast<const btBoxShape*>(shape))
    {
        const btVector3 halfExtents = box->getHalfExtentsWithMargin();
        hasher.F32(static_cast<float>(halfExtents.x()));
        hasher.F32(static_cast<float>(halfExtents.y()));
        hasher.F32(static_cast<float>(halfExtents.z()));
    }
    else if (const auto* capsule = dynamic_cast<const btCapsuleShape*>(shape))
    {
        hasher.F32(static_cast<float>(capsule->getRadius()));
        hasher.F32(static_cast<float>(capsule->getHalfHeight()));
    }
}
}  // namespace

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
    std::vector<std::string> sabaBoneNames;
    std::unique_ptr<Skeleton> skeleton;
    std::unique_ptr<Pose> pose;
    std::uint64_t vertexRevision = 0U;
    std::uint64_t morphRevision = 0U;
    const ModelAsset* asset = nullptr;
    std::unordered_map<BoneIndex, bool> mmdIkOverrides;
    // Engine-level named morph overrides, re-applied after every VMD
    // evaluation so deterministic replay preserves user configuration.
    std::unordered_map<std::string, float> userMorphOverrides;
    // R1.2A deterministic observation state (last Canonical Frame Boundary).
    std::uint32_t lastExecutedSubsteps = 0U;
    float lastRemainingAccumulator = 0.0f;
    MotionFrameIndex lastMotionFrame = 0U;
    TimelineTick lastPhysicsTick = 0U;
    bool lastBoundaryCanonical = false;
    // Deterministic stepping state machine (P0-3 fix): StepMotionFrameExact
    // is only valid after PrepareFrameZero and must advance one frame at a
    // time. EvaluateTick(ReplayFromStart) drives this same machine.
    bool deterministicPrepared = false;
    MotionFrameIndex expectedNextFrame = 0U;
    // R1.2B restore state: write-phase failure poisons the instance until a
    // recovery entry (PrepareFrameZero / EvaluateTick(0, ResetAtTarget))
    // succeeds. faultInjectionPhase is only armed by test hooks.
    bool poisoned = false;
    int faultInjectionPhase = 0;

    Impl(
        std::filesystem::path modelPath_,
        std::filesystem::path vmdPath_,
        SabaPhysicsSettings physicsSettings_
    )
        : modelPath(std::move(modelPath_)),
          vmdPath(std::move(vmdPath_)),
          physicsSettings(physicsSettings_)
    {
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

void SabaMmdRuntimeModel::SetAsset(const ModelAsset* asset) noexcept
{
    this->impl->asset = asset;
}

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
            this->ApplyPhysicsActivation();
        }
    }
}

void SabaMmdRuntimeModel::SetMmdPhysicsSettings(
    const MmdPhysicsRuntimeSettings& settings
)
{
    this->SetPhysicsSettings(settings);
}

void SabaMmdRuntimeModel::ResetMmdPhysics()
{
    if (this->impl->model != nullptr)
        this->impl->model->ResetPhysics();
}

void SabaMmdRuntimeModel::ApplyPhysicsActivation()
{
    if (this->impl->model == nullptr)
        return;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr)
        return;
    const bool enabled = this->impl->physicsSettings.enabled;
    for (auto& rigidBody : *manager->GetRigidBodys())
        rigidBody->SetActivation(enabled);
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

    // Materialize Saba's node hierarchy as WISTERIA-owned immutable Skeleton
    // plus a per-instance Pose. Saba remains the evaluator, but Scene/export
    // code now observes the authoritative result through engine types.
    this->impl->mmdIkOverrides.clear();
    this->impl->bones.clear();
    this->impl->sabaBoneNames.clear();
    if (saba::MMDNodeManager* nodes = this->impl->model->GetNodeManager())
    {
        const std::size_t count = nodes->GetNodeCount();
        this->impl->bones.reserve(count);
        this->impl->sabaBoneNames.reserve(count);
        std::unordered_map<std::string, std::size_t> usedNames;
        for (std::size_t index = 0U; index < count; ++index)
        {
            saba::MMDNode* node = nodes->GetMMDNode(index);
            Bone bone;
            std::string originalName = node != nullptr ? node->GetName() : "";
            this->impl->sabaBoneNames.push_back(originalName);
            std::string engineName = originalName.empty()
                ? "bone_" + std::to_string(index)
                : originalName;
            const std::size_t occurrence = usedNames[engineName]++;
            if (occurrence > 0U)
                engineName += "#" + std::to_string(index);
            bone.name = std::move(engineName);
            if (node != nullptr)
            {
                if (saba::MMDNode* parent = node->GetParent())
                    bone.parentIndex = static_cast<BoneIndex>(parent->GetIndex());
                bone.bindLocalMatrix =
                    glm::translate(glm::mat4(1.0f), node->GetInitialTranslate()) *
                    glm::mat4_cast(node->GetInitialRotate()) *
                    glm::scale(glm::mat4(1.0f), node->GetInitialScale());
                bone.inverseBindMatrix = node->GetInverseInitTransform();
                bone.sourceOrder = static_cast<std::uint32_t>(index);
            }
            this->impl->bones.push_back(std::move(bone));
        }
    }
    if (this->impl->bones.empty())
    {
        Bone root;
        root.name = "root";
        this->impl->bones.push_back(std::move(root));
        this->impl->sabaBoneNames.emplace_back("root");
    }
    this->impl->skeleton = std::make_unique<Skeleton>(this->impl->bones);
    this->impl->pose = std::make_unique<Pose>(*this->impl->skeleton);
    this->SyncPoseFromSaba();

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
        this->ApplyPhysicsActivation();
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
    // Mirrors saba::MMDModel::UpdateAllAnimation step by step so engine-level
    // IK overrides can be injected between the VMD evaluation and the IK
    // solves inside UpdateNodeAnimation. Calling UpdateAllAnimation directly
    // would re-apply the VMD's IK switches after our override.
    this->impl->model->BeginAnimation();
    if (this->impl->vmdAnimation != nullptr)
    {
        this->impl->vmdAnimation->Evaluate(
            static_cast<float>(this->impl->vmdFrame)
        );
    }
    // Engine-level morph overrides survive VMD evaluation (contract §5:
    // VMD animation -> user Morph override -> morph expansion).
    this->ApplyUserMorphOverrides();
    this->impl->model->UpdateMorphAnimation();
    // VMD evaluation writes morph weights directly into saba's MMDMorph
    // objects, bypassing WISTERIA's SetMorphWeight(). The morph revision must
    // advance every evaluation so persisted MorphSnapshots never freeze.
    ++this->impl->morphRevision;
    this->ApplyMmdIkOverrides();
    this->impl->model->UpdateNodeAnimation(false);
    if (this->impl->physicsSettings.enabled)
        this->impl->model->UpdatePhysicsAnimation(deltaTime);
    this->impl->model->UpdateNodeAnimation(true);
    this->impl->model->EndAnimation();
    this->impl->model->Update();
    this->SyncPoseFromSaba();
    ++this->impl->vertexRevision;
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
    this->impl->lastBoundaryCanonical = false;
    this->impl->deterministicPrepared = false;
    this->impl->expectedNextFrame = 0U;
    this->impl->poisoned = false;
    this->impl->faultInjectionPhase = 0;
}

TimelineStatus SabaMmdRuntimeModel::ValidateReplayConfig(
    const ReplayConfig& config
)
{
    // R1.2A freezes exactly 30Hz motion / 120Hz physics with no warmup and
    // no looping. Anything else is rejected up front so the replay algorithm
    // never depends on undefined warmup/loop semantics. Physics must be
    // enabled: a disabled world cannot claim a 120Hz canonical boundary.
    if (config.motionFps != 30U ||
        config.physicsHz != 120U ||
        config.warmupFrames != 0U ||
        config.loopMotion ||
        !this->impl->physicsSettings.enabled)
    {
        return TimelineStatus::UnsupportedReplayProfile;
    }
    // ReplayConfig must match the live Bullet configuration, not just the
    // frozen defaults. Otherwise StepFrameExact could step at 1/60 or run
    // fewer than four substeps while metadata claims 30Hz/120Hz.
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysics* physics = manager->GetMMDPhysics();
    constexpr float kFpsTolerance = 1.0e-4f;
    if (std::abs(
            this->impl->physicsSettings.fixedTimeStep - 1.0f / 120.0f
        ) > kFpsTolerance ||
        this->impl->physicsSettings.maxSubSteps < 4 ||
        std::abs(physics->GetFPS() - 120.0f) > kFpsTolerance ||
        physics->GetMaxSubStepCount() < 4)
    {
        return TimelineStatus::UnsupportedReplayProfile;
    }
    return TimelineStatus::Ok;
}

void SabaMmdRuntimeModel::ApplyUserMorphOverrides()
{
    if (this->impl->model == nullptr ||
        this->impl->userMorphOverrides.empty())
    {
        return;
    }
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    if (manager == nullptr)
        return;
    for (const auto& [name, weight] : this->impl->userMorphOverrides)
    {
        saba::MMDMorph* morph = manager->GetMorph(name);
        if (morph != nullptr)
            morph->SetWeight(weight);
    }
}

TimelineStatus SabaMmdRuntimeModel::ResetCanonicalNoStep()
{
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysics* physics = manager->GetMMDPhysics();
    if (physics == nullptr)
        return TimelineStatus::NoPhysics;

    auto* rigidBodies = manager->GetRigidBodys();
    // Bind kinematic motion states (they read the current animated node
    // transforms) and re-seat the active motion states at the animated pose.
    for (auto& rigidBody : *rigidBodies)
    {
        rigidBody->SetActivation(false);
        rigidBody->ResetTransform();
    }
    // Synchronize the actual Bullet body transforms to the kinematic target
    // without executing any physics step. This is the "sync Kinematic
    // target" phase of the contract, done explicitly because Saba's own
    // ResetPhysics hides a 1/60 step we must not run.
    for (auto& rigidBody : *rigidBodies)
    {
        btRigidBody* body = rigidBody->GetRigidBody();
        if (body == nullptr || body->getMotionState() == nullptr)
            continue;
        btTransform worldTransform;
        body->getMotionState()->getWorldTransform(worldTransform);
        body->setCenterOfMassTransform(worldTransform);
        body->setInterpolationWorldTransform(worldTransform);
        body->setInterpolationLinearVelocity(btVector3(0, 0, 0));
        body->setInterpolationAngularVelocity(btVector3(0, 0, 0));
        // Teleported bodies need their broadphase proxy AABB refreshed;
        // clearing the pair cache alone does not update the proxy volume.
        if (btDiscreteDynamicsWorld* world = physics->GetDynamicsWorld())
        {
            world->updateSingleAabb(body);
        }
    }
    // Zero velocities/forces, clean broadphase pairs, and reset the frame
    // accumulator to reach a Canonical Frame Boundary.
    for (auto& rigidBody : *rigidBodies)
    {
        rigidBody->Reset(physics);
    }
    // Restore the true body mode: SetActivation(false) temporarily turns
    // dynamic bodies kinematic so their target pose can be read, but a
    // Canonical Frame Boundary must describe dynamic bodies as dynamic again
    // (consistent with invMass and the PhysicsSnapshot). Static PMX bodies
    // stay kinematic internally. Also normalize activation history so a
    // boundary never inherits sleeping state from a long previous timeline.
    for (auto& rigidBody : *rigidBodies)
    {
        // Bullet 3.25's activate(true) still routes through the guarded
        // setActivationState, so DISABLE_DEACTIVATION would survive a plain
        // reset. NormalizeCanonicalActivation uses forceActivationState to
        // make ACTIVE_TAG unconditional (R1.2B contract §5 Phase 5).
        rigidBody->SelectMotionStateForMode(
            rigidBody->GetRigidBodyType()
        );
        rigidBody->SyncActiveMotionStateToBodyTransform();
        rigidBody->NormalizeCanonicalActivation(
            rigidBody->GetRigidBodyType()
        );
    }
    physics->ResetSimulationTime();
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::EvaluateFrameCanonical(
    MotionFrameIndex frame,
    const ReplayConfig& config
)
{
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
        return TimelineStatus::NoPhysics;

    this->impl->vmdFrame = static_cast<double>(frame);
    this->impl->model->BeginAnimation();
    if (this->impl->vmdAnimation != nullptr)
    {
        this->impl->vmdAnimation->Evaluate(static_cast<float>(frame));
    }
    this->ApplyUserMorphOverrides();
    this->impl->model->UpdateMorphAnimation();
    ++this->impl->morphRevision;
    this->ApplyMmdIkOverrides();
    this->impl->model->UpdateNodeAnimation(false);
    const TimelineStatus resetStatus = this->ResetCanonicalNoStep();
    if (resetStatus != TimelineStatus::Ok)
        return resetStatus;
    this->impl->model->UpdateNodeAnimation(true);
    this->impl->model->EndAnimation();
    this->impl->model->Update();
    this->SyncPoseFromSaba();
    ++this->impl->vertexRevision;
    this->impl->lastExecutedSubsteps = 0U;
    this->impl->lastRemainingAccumulator =
        manager->GetMMDPhysics()->GetSimulationTime();
    if (this->impl->lastRemainingAccumulator != 0.0f)
    {
        this->impl->lastBoundaryCanonical = false;
        return TimelineStatus::DeterminismViolation;
    }
    this->impl->lastMotionFrame = frame;
    this->impl->lastPhysicsTick =
        frame * (config.physicsHz / config.motionFps);
    this->impl->lastBoundaryCanonical = true;
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::StepFrameExact(
    MotionFrameIndex frame,
    const ReplayConfig& config
)
{
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
        return TimelineStatus::NoPhysics;

    this->impl->vmdFrame = static_cast<double>(frame);
    this->impl->model->BeginAnimation();
    if (this->impl->vmdAnimation != nullptr)
    {
        this->impl->vmdAnimation->Evaluate(static_cast<float>(frame));
    }
    this->ApplyUserMorphOverrides();
    this->impl->model->UpdateMorphAnimation();
    ++this->impl->morphRevision;
    this->ApplyMmdIkOverrides();
    this->impl->model->UpdateNodeAnimation(false);
    int executedSubsteps = 0;
    if (this->impl->physicsSettings.enabled)
    {
        // The four-substep experiment proved stepSimulation(1/30, 10, 1/120)
        // executes exactly 4 substeps with a zero accumulator; this existing
        // full Update phase is therefore the deterministic reference path.
        executedSubsteps = this->impl->model->UpdatePhysicsAnimation(
            1.0f / 30.0f
        );
    }
    this->impl->model->UpdateNodeAnimation(true);
    this->impl->model->EndAnimation();
    this->impl->model->Update();
    this->SyncPoseFromSaba();
    ++this->impl->vertexRevision;
    this->impl->lastExecutedSubsteps =
        static_cast<std::uint32_t>(std::max(0, executedSubsteps));
    this->impl->lastRemainingAccumulator =
        manager->GetMMDPhysics()->GetSimulationTime();
    // The four-substep experiment holds on the default build, but the
    // production path must verify the boundary on every frame rather than
    // trusting a one-time probe. A failed boundary is never marked canonical.
    if (this->impl->lastExecutedSubsteps != 4U ||
        this->impl->lastRemainingAccumulator != 0.0f)
    {
        this->impl->lastBoundaryCanonical = false;
        return TimelineStatus::DeterminismViolation;
    }
    this->impl->lastMotionFrame = frame;
    this->impl->lastPhysicsTick =
        frame * (config.physicsHz / config.motionFps);
    this->impl->lastBoundaryCanonical = true;
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::EvaluateTick(
    MotionFrameIndex target,
    SeekPolicy policy,
    const ReplayConfig& config
)
{
    if (this->impl->poisoned)
    {
        // EvaluateTick(0, ResetAtTarget) is a documented recovery entry;
        // every other policy is rejected while Poisoned.
        if (policy != SeekPolicy::ResetAtTarget || target != 0U)
            return TimelineStatus::Poisoned;
    }
    this->impl->deterministicPrepared = false;
    this->impl->expectedNextFrame = 0U;
    const TimelineStatus profileStatus = this->ValidateReplayConfig(config);
    if (profileStatus != TimelineStatus::Ok)
        return profileStatus;

    switch (policy)
    {
    case SeekPolicy::PreserveState:
        // Interactive preview: move the VMD frame and run one full Update(0).
        // Physics history is intentionally preserved; this is not a
        // Canonical Frame Boundary.
        this->impl->vmdFrame = static_cast<double>(target);
        this->impl->lastBoundaryCanonical = false;
        this->Update(0.0f);
        return TimelineStatus::Ok;
    case SeekPolicy::ResetAtTarget:
    {
        const TimelineStatus status =
            this->EvaluateFrameCanonical(target, config);
        if (status == TimelineStatus::Ok)
        {
            this->impl->poisoned = false;
            this->impl->faultInjectionPhase = 0;
        }
        return status;
    }
    case SeekPolicy::ReplayFromStart:
    {
        TimelineStatus status = this->PrepareFrameZero(config);
        if (status != TimelineStatus::Ok)
            return status;
        for (MotionFrameIndex frame = 1U; frame <= target; ++frame)
        {
            status = this->StepMotionFrameExact(frame, config);
            if (status != TimelineStatus::Ok)
                return status;
        }
        return TimelineStatus::Ok;
    }
    case SeekPolicy::ReplayFromCheckpoint:
        // R1.2C: explicit FrameCheckpoint restore + canonicalization.
        return TimelineStatus::InvalidCheckpoint;
    }
    return TimelineStatus::UnsupportedReplayProfile;
}

TimelineStatus SabaMmdRuntimeModel::PrepareFrameZero(
    const ReplayConfig& config
)
{
    // PrepareFrameZero is a documented Poisoned recovery entry.
    const TimelineStatus profileStatus = this->ValidateReplayConfig(config);
    if (profileStatus != TimelineStatus::Ok)
        return profileStatus;
    const TimelineStatus status = this->EvaluateFrameCanonical(0U, config);
    if (status != TimelineStatus::Ok)
    {
        this->impl->deterministicPrepared = false;
        this->impl->expectedNextFrame = 0U;
        return status;
    }
    this->impl->deterministicPrepared = true;
    this->impl->expectedNextFrame = 1U;
    this->impl->poisoned = false;
    this->impl->faultInjectionPhase = 0;
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::StepMotionFrameExact(
    MotionFrameIndex frame,
    const ReplayConfig& config
)
{
    if (this->impl->poisoned)
        return TimelineStatus::Poisoned;
    if (!this->impl->deterministicPrepared)
        return TimelineStatus::InvalidState;
    if (frame != this->impl->expectedNextFrame)
        return TimelineStatus::NonSequentialFrame;
    const TimelineStatus profileStatus = this->ValidateReplayConfig(config);
    if (profileStatus != TimelineStatus::Ok)
        return profileStatus;
    const TimelineStatus status = this->StepFrameExact(frame, config);
    if (status != TimelineStatus::Ok)
    {
        this->impl->deterministicPrepared = false;
        this->impl->expectedNextFrame = 0U;
        return status;
    }
    ++this->impl->expectedNextFrame;
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::CaptureState(
    PhysicsSnapshot& output
) const
{
    if (this->impl->poisoned)
        return TimelineStatus::Poisoned;
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
        return TimelineStatus::NoPhysics;

    auto* rigidBodies = manager->GetRigidBodys();
    output.rigidBodies.clear();
    output.rigidBodies.reserve(rigidBodies->size());
    for (std::size_t index = 0U; index < rigidBodies->size(); ++index)
    {
        btRigidBody* body = (*rigidBodies)[index]->GetRigidBody();
        if (body == nullptr)
            continue;
        RigidBodySnapshot snapshot;
        snapshot.index = static_cast<std::uint32_t>(index);
        snapshot.mode = static_cast<PmxRigidBodyMode>(
            (*rigidBodies)[index]->GetRigidBodyType()
        );
        snapshot.definitionMass =
            (*rigidBodies)[index]->GetDefinitionMass();
        const btTransform transform = body->getCenterOfMassTransform();
        FillTransformSnapshot(snapshot.worldTransform, transform);
        const btTransform interpolation =
            body->getInterpolationWorldTransform();
        FillTransformSnapshot(
            snapshot.interpolationTransform,
            interpolation
        );
        snapshot.linearVelocity = ToGlmVec3(body->getLinearVelocity());
        snapshot.angularVelocity = ToGlmVec3(body->getAngularVelocity());
        snapshot.interpolationLinearVelocity =
            ToGlmVec3(body->getInterpolationLinearVelocity());
        snapshot.interpolationAngularVelocity =
            ToGlmVec3(body->getInterpolationAngularVelocity());
        snapshot.totalForce = ToGlmVec3(body->getTotalForce());
        snapshot.totalTorque = ToGlmVec3(body->getTotalTorque());
        snapshot.activationState = body->getActivationState();
        snapshot.deactivationTime =
            static_cast<float>(body->getDeactivationTime());
        output.rigidBodies.push_back(snapshot);
    }
    output.jointCount = static_cast<std::uint32_t>(
        manager->GetJoints()->size()
    );
    output.motionFrame = this->impl->lastMotionFrame;
    output.physicsTick = this->impl->lastPhysicsTick;
    output.canonical = this->impl->lastBoundaryCanonical;
    output.schemaVersion = 2U;
    this->ComputeLayoutFingerprint(output.layoutFingerprint);
    this->ComputeConfigurationFingerprint(
        output.physicsConfigurationFingerprint
    );
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::ReadStepDiagnostics(
    PhysicsStepDiagnostics& output
) const
{
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
        return TimelineStatus::NoPhysics;
    output.executedSubsteps = this->impl->lastExecutedSubsteps;
    output.remainingAccumulator = static_cast<double>(
        manager->GetMMDPhysics()->GetSimulationTime()
    );
    output.poisoned = this->impl->poisoned;
    return TimelineStatus::Ok;
}

bool SabaMmdRuntimeModel::IsPoisoned() const noexcept
{
    return this->impl->poisoned;
}

void SabaMmdRuntimeModel::EnterPoisoned() noexcept
{
    this->impl->poisoned = true;
    this->impl->lastBoundaryCanonical = false;
    this->impl->deterministicPrepared = false;
    this->impl->expectedNextFrame = 0U;
}

TimelineStatus SabaMmdRuntimeModel::CapturePhysicsSnapshot(
    PhysicsSnapshot& output
) const
{
    return this->CaptureState(output);
}

TimelineStatus SabaMmdRuntimeModel::RestorePhysicsSnapshot(
    const PhysicsSnapshot& snapshot
)
{
    return this->RestoreState(snapshot);
}

TimelineStatus SabaMmdRuntimeModel::RestoreState(
    const PhysicsSnapshot& snapshot
)
{
    if (this->IsPoisoned())
        return TimelineStatus::Poisoned;
#if defined(BT_USE_DOUBLE_PRECISION)
    // R1.2B v4.1.1 freezes the scalar schema to 32-bit; a double-precision
    // Bullet build cannot provide bit-exact snapshot restore.
    return TimelineStatus::UnsupportedReplayProfile;
#endif
    const TimelineStatus validation =
        this->ValidateSnapshotForRestore(snapshot);
    if (validation != TimelineStatus::Ok)
        return validation;
    try
    {
        return this->RestorePhases(snapshot);
    }
    catch (...)
    {
        this->EnterPoisoned();
        return TimelineStatus::Poisoned;
    }
}

TimelineStatus SabaMmdRuntimeModel::ValidateSnapshotForRestore(
    const PhysicsSnapshot& snapshot
) const
{
    if (this->impl->model == nullptr)
    {
        return TimelineStatus::NoPhysics;
    }
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
    {
        return TimelineStatus::NoPhysics;
    }
    if (!this->impl->physicsSettings.enabled)
    {
        return TimelineStatus::UnsupportedReplayProfile;
    }

    if (snapshot.schemaVersion != 2U)
    {
        return TimelineStatus::SnapshotMismatch;
    }
    if (!snapshot.canonical)
    {
        return TimelineStatus::InvalidSnapshot;
    }

    auto* rigidBodies = manager->GetRigidBodys();
    // Count/index checks must precede every body[i] access.
    if (snapshot.rigidBodies.size() != rigidBodies->size())
    {
        return TimelineStatus::SnapshotMismatch;
    }
    if (snapshot.jointCount != manager->GetJoints()->size())
    {
        return TimelineStatus::SnapshotMismatch;
    }
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        if (snapshot.rigidBodies[index].index != index)
        {
            return TimelineStatus::SnapshotMismatch;
        }
    }

    std::uint64_t currentLayout = 0U;
    this->ComputeLayoutFingerprint(currentLayout);
    if (currentLayout != snapshot.layoutFingerprint)
    {
        return TimelineStatus::SnapshotMismatch;
    }
    std::uint64_t currentConfig = 0U;
    this->ComputeConfigurationFingerprint(currentConfig);
    if (currentConfig != snapshot.physicsConfigurationFingerprint)
    {
        return TimelineStatus::SnapshotMismatch;
    }

    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& bodySnapshot =
            snapshot.rigidBodies[index];
        const auto& rigidBody = (*rigidBodies)[index];
        if (static_cast<int>(bodySnapshot.mode) !=
            rigidBody->GetRigidBodyType())
        {
            return TimelineStatus::SnapshotMismatch;
        }
        std::uint32_t snapshotMassBits = 0U;
        std::uint32_t currentMassBits = 0U;
        std::memcpy(
            &snapshotMassBits,
            &bodySnapshot.definitionMass,
            sizeof(snapshotMassBits)
        );
        const float currentMass = rigidBody->GetDefinitionMass();
        std::memcpy(&currentMassBits, &currentMass, sizeof(currentMassBits));
        if (snapshotMassBits != currentMassBits)
        {
            return TimelineStatus::SnapshotMismatch;
        }

        if (!std::isfinite(bodySnapshot.definitionMass) ||
            bodySnapshot.definitionMass < 0.0f)
        {
            return TimelineStatus::InvalidSnapshot;
        }
        if (!IsFiniteBasis(bodySnapshot.worldTransform) ||
            !IsFiniteBasis(bodySnapshot.interpolationTransform))
        {
            return TimelineStatus::InvalidSnapshot;
        }
        if (!IsValidRotationBasis(bodySnapshot.worldTransform) ||
            !IsValidRotationBasis(bodySnapshot.interpolationTransform))
        {
            return TimelineStatus::InvalidSnapshot;
        }
        const glm::vec3* vectors[] = {
            &bodySnapshot.linearVelocity,
            &bodySnapshot.angularVelocity,
            &bodySnapshot.interpolationLinearVelocity,
            &bodySnapshot.interpolationAngularVelocity,
            &bodySnapshot.totalForce,
            &bodySnapshot.totalTorque
        };
        for (const glm::vec3* vector : vectors)
        {
            if (!std::isfinite(vector->x) ||
                !std::isfinite(vector->y) ||
                !std::isfinite(vector->z))
            {
                return TimelineStatus::InvalidSnapshot;
            }
        }
        if (!std::isfinite(bodySnapshot.deactivationTime))
        {
            return TimelineStatus::InvalidSnapshot;
        }

        // Canonical zeros use +0.0f bit patterns.
        if (!IsPositiveZero(bodySnapshot.totalForce.x) ||
            !IsPositiveZero(bodySnapshot.totalForce.y) ||
            !IsPositiveZero(bodySnapshot.totalForce.z) ||
            !IsPositiveZero(bodySnapshot.totalTorque.x) ||
            !IsPositiveZero(bodySnapshot.totalTorque.y) ||
            !IsPositiveZero(bodySnapshot.totalTorque.z))
        {
            return TimelineStatus::InvalidSnapshot;
        }
        if (bodySnapshot.mode != PmxRigidBodyMode::FollowBone)
        {
            if (bodySnapshot.activationState != ACTIVE_TAG ||
                !IsPositiveZero(bodySnapshot.deactivationTime))
            {
                return TimelineStatus::InvalidSnapshot;
            }
        }
    }

    if (snapshot.motionFrame >
        std::numeric_limits<std::uint64_t>::max() / 4U)
    {
        return TimelineStatus::InvalidSnapshot;
    }
    if (snapshot.physicsTick != snapshot.motionFrame * 4U)
    {
        return TimelineStatus::InvalidSnapshot;
    }

    // Animation precondition: the caller must have evaluated animation at
    // the snapshot's motion frame; FollowBone transforms must already match.
    if (this->impl->vmdFrame != static_cast<double>(snapshot.motionFrame))
    {
        return TimelineStatus::InvalidState;
    }
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& bodySnapshot =
            snapshot.rigidBodies[index];
        if (bodySnapshot.mode != PmxRigidBodyMode::FollowBone)
            continue;
        btRigidBody* body = (*rigidBodies)[index]->GetRigidBody();
        if (body == nullptr)
            continue;
        if (!SameTransformBitwise(
                body->getCenterOfMassTransform(),
                bodySnapshot.worldTransform
            ))
        {
            return TimelineStatus::InvalidState;
        }
    }
    return TimelineStatus::Ok;
}

TimelineStatus SabaMmdRuntimeModel::RestorePhases(
    const PhysicsSnapshot& snapshot
)
{
    auto* manager = this->impl->model->GetPhysicsManager();
    auto* physics = manager->GetMMDPhysics();
    auto* rigidBodies = manager->GetRigidBodys();
    const auto throwIfInjected = [this](int phase)
    {
        if (this->impl->faultInjectionPhase == phase)
            throw std::runtime_error("R1.2B restore fault injection");
    };

    // Phase 1: motion-state selection + world/interpolation transforms.
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& bodySnapshot =
            snapshot.rigidBodies[index];
        const auto& rigidBody = (*rigidBodies)[index];
        btRigidBody* body = rigidBody->GetRigidBody();
        if (body == nullptr)
            continue;
        rigidBody->SelectMotionStateForMode(
            static_cast<int>(bodySnapshot.mode)
        );
        btTransform worldTransform;
        FillBulletTransform(worldTransform, bodySnapshot.worldTransform);
        btTransform interpolationTransform;
        FillBulletTransform(
            interpolationTransform,
            bodySnapshot.interpolationTransform
        );
        body->setCenterOfMassTransform(worldTransform);
        body->setInterpolationWorldTransform(interpolationTransform);
        if (bodySnapshot.mode != PmxRigidBodyMode::FollowBone)
        {
            // Keep the active motion state in sync so Phase 6's
            // ReflectGlobalTransform reads the restored pose, not a stale
            // pre-restore transform.
            rigidBody->SyncActiveMotionStateToBodyTransform();
        }
    }
    throwIfInjected(1);

    // Phase 2: velocities (regular + interpolation).
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& bodySnapshot =
            snapshot.rigidBodies[index];
        btRigidBody* body = (*rigidBodies)[index]->GetRigidBody();
        if (body == nullptr)
            continue;
        body->setLinearVelocity(ToBtVector3(bodySnapshot.linearVelocity));
        body->setAngularVelocity(ToBtVector3(bodySnapshot.angularVelocity));
        body->setInterpolationLinearVelocity(
            ToBtVector3(bodySnapshot.interpolationLinearVelocity)
        );
        body->setInterpolationAngularVelocity(
            ToBtVector3(bodySnapshot.interpolationAngularVelocity)
        );
    }
    throwIfInjected(2);

    // Phase 3: canonical forces are +0; clear the target instance.
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        btRigidBody* body = (*rigidBodies)[index]->GetRigidBody();
        if (body != nullptr)
            body->clearForces();
    }
    throwIfInjected(3);

    // Phase 4: deterministic collision-world rebuild + solver history clear.
    physics->RebuildCollisionWorldDeterministic();
    physics->ClearSolverHistoryDeterministic();
    throwIfInjected(4);

    // Phase 5: canonical activation + time boundary.
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& bodySnapshot =
            snapshot.rigidBodies[index];
        (*rigidBodies)[index]->NormalizeCanonicalActivation(
            static_cast<int>(bodySnapshot.mode)
        );
    }
    physics->ResetSimulationTime();
    this->impl->lastExecutedSubsteps = 0U;
    this->impl->lastRemainingAccumulator = 0.0f;
    throwIfInjected(5);

    // Phase 6: inertia tensor, bone write-back, mesh and pose sync.
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& bodySnapshot =
            snapshot.rigidBodies[index];
        const auto& rigidBody = (*rigidBodies)[index];
        btRigidBody* body = rigidBody->GetRigidBody();
        if (body != nullptr)
        {
            // Bullet already refreshes this inside setCenterOfMassTransform;
            // calling it explicitly keeps the contract independent of that
            // implementation detail.
            body->updateInertiaTensor();
        }
        if (bodySnapshot.mode != PmxRigidBodyMode::FollowBone)
        {
            rigidBody->ReflectGlobalTransform();
        }
        rigidBody->CalcLocalTransform();
    }
    if (saba::MMDNodeManager* nodes =
            this->impl->model->GetNodeManager())
    {
        for (std::size_t index = 0U; index < nodes->GetNodeCount(); ++index)
        {
            saba::MMDNode* node = nodes->GetMMDNode(index);
            if (node != nullptr && node->GetParent() == nullptr)
            {
                node->UpdateGlobalTransform();
            }
        }
    }
    this->impl->model->Update();
    this->SyncPoseFromSaba();
    throwIfInjected(6);

    this->impl->lastBoundaryCanonical = true;
    this->impl->lastMotionFrame = snapshot.motionFrame;
    this->impl->lastPhysicsTick = snapshot.physicsTick;
    // R1.2B does not restore the deterministic stepping state machine;
    // continuation is R1.2C checkpoint semantics.
    this->impl->deterministicPrepared = false;
    this->impl->expectedNextFrame = 0U;
    return TimelineStatus::Ok;
}

void SabaMmdRuntimeModel::ComputeLayoutFingerprint(
    std::uint64_t& fingerprint
) const
{
    FnvHasher hasher;
    hasher.U32(2U);  // schemaVersion
    hasher.U32(1U);  // layoutVersion
    if (this->impl->model == nullptr)
    {
        fingerprint = hasher.state;
        return;
    }
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr)
    {
        fingerprint = hasher.state;
        return;
    }
    auto* rigidBodies = manager->GetRigidBodys();
    auto* joints = manager->GetJoints();
    hasher.U32(static_cast<std::uint32_t>(rigidBodies->size()));
    hasher.U32(static_cast<std::uint32_t>(joints->size()));

    for (std::size_t index = 0U; index < rigidBodies->size(); ++index)
    {
        const auto& rigidBody = (*rigidBodies)[index];
        btRigidBody* body = rigidBody->GetRigidBody();
        hasher.U32(static_cast<std::uint32_t>(index));
        hasher.I32(rigidBody->GetRigidBodyType());
        hasher.F32(rigidBody->GetDefinitionMass());
        HashShapeDimensions(
            hasher,
            body != nullptr ? body->getCollisionShape() : nullptr
        );
        hasher.I32(rigidBody->GetBoneIndex());
        hasher.Mat4(rigidBody->GetOffsetMatrix());
        hasher.U32(rigidBody->GetGroup());
        hasher.U32(rigidBody->GetGroupMask());
        if (body != nullptr)
        {
            hasher.F32(static_cast<float>(body->getLinearDamping()));
            hasher.F32(static_cast<float>(body->getAngularDamping()));
            hasher.F32(static_cast<float>(body->getRestitution()));
            hasher.F32(static_cast<float>(body->getFriction()));
        }
        else
        {
            hasher.F32(0.0f);
            hasher.F32(0.0f);
            hasher.F32(0.0f);
            hasher.F32(0.0f);
        }
    }

    std::unordered_map<const btRigidBody*, std::uint32_t> indexMap;
    for (std::size_t index = 0U; index < rigidBodies->size(); ++index)
    {
        indexMap[(*rigidBodies)[index]->GetRigidBody()] =
            static_cast<std::uint32_t>(index);
    }
    for (std::size_t index = 0U; index < joints->size(); ++index)
    {
        btTypedConstraint* constraint =
            (*joints)[index]->GetConstraint();
        if (constraint == nullptr)
        {
            hasher.I32(-1);
            continue;
        }
        hasher.I32(constraint->getConstraintType());
        const btRigidBody* bodyA = &constraint->getRigidBodyA();
        const btRigidBody* bodyB = &constraint->getRigidBodyB();
        const auto bodyAIterator = indexMap.find(bodyA);
        const auto bodyBIterator = indexMap.find(bodyB);
        hasher.U32(
            bodyAIterator != indexMap.end()
                ? bodyAIterator->second
                : 0xFFFFFFFFU
        );
        hasher.U32(
            bodyBIterator != indexMap.end()
                ? bodyBIterator->second
                : 0xFFFFFFFFU
        );
        if (const auto* dof =
                dynamic_cast<const btGeneric6DofConstraint*>(constraint))
        {
            hasher.BtTransform(dof->getFrameOffsetA());
            hasher.BtTransform(dof->getFrameOffsetB());
            btVector3 linearLower;
            btVector3 linearUpper;
            btVector3 angularLower;
            btVector3 angularUpper;
            dof->getLinearLowerLimit(linearLower);
            dof->getLinearUpperLimit(linearUpper);
            dof->getAngularLowerLimit(angularLower);
            dof->getAngularUpperLimit(angularUpper);
            hasher.BtVector3(linearLower);
            hasher.BtVector3(linearUpper);
            hasher.BtVector3(angularLower);
            hasher.BtVector3(angularUpper);
            if (const auto* spring = dynamic_cast<
                    const btGeneric6DofSpringConstraint*>(constraint))
            {
                for (int axis = 0; axis < 6; ++axis)
                {
                    hasher.U32(spring->isSpringEnabled(axis) ? 1U : 0U);
                    hasher.F32(
                        static_cast<float>(spring->getStiffness(axis))
                    );
                    hasher.F32(
                        static_cast<float>(spring->getDamping(axis))
                    );
                }
            }
        }
    }
    fingerprint = hasher.state;
}

void SabaMmdRuntimeModel::ComputeConfigurationFingerprint(
    std::uint64_t& fingerprint
) const
{
    FnvHasher hasher;
    hasher.U32(sizeof(btScalar));  // scalar precision (4 in R1.2B)
    hasher.U32(1U);                // physics ABI version
    hasher.U32(1U);                // broadphase: btDbvtBroadphase
    hasher.U32(1U);                // solver: btSequentialImpulseConstraintSolver
    if (this->impl->model == nullptr)
    {
        fingerprint = hasher.state;
        return;
    }
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
    {
        fingerprint = hasher.state;
        return;
    }
    saba::MMDPhysics* physics = manager->GetMMDPhysics();
    btDiscreteDynamicsWorld* world = physics->GetDynamicsWorld();
    if (world != nullptr)
    {
        hasher.BtVector3(world->getGravity());
        const btContactSolverInfo& info = world->getSolverInfo();
        hasher.I32(info.m_numIterations);
        hasher.I32(info.m_solverMode);
        hasher.F32(static_cast<float>(info.m_erp));
        hasher.F32(static_cast<float>(info.m_erp2));
        hasher.U32(info.m_splitImpulse ? 1U : 0U);
        hasher.F32(static_cast<float>(info.m_splitImpulsePenetrationThreshold));
        hasher.F32(static_cast<float>(info.m_splitImpulseTurnErp));
        hasher.F32(static_cast<float>(info.m_globalCfm));
        hasher.F32(static_cast<float>(info.m_frictionERP));
        hasher.F32(static_cast<float>(info.m_frictionCFM));
        hasher.F32(static_cast<float>(info.m_restitution));
        hasher.F32(static_cast<float>(info.m_restitutionVelocityThreshold));
        hasher.F32(static_cast<float>(info.m_linearSlop));
        hasher.F32(static_cast<float>(info.m_warmstartingFactor));
        hasher.F32(static_cast<float>(info.m_damping));
        hasher.F32(static_cast<float>(info.m_maxErrorReduction));
        hasher.F32(static_cast<float>(info.m_sor));
        hasher.I32(info.m_numNonContactInnerIterations);
    }
    hasher.F32(this->impl->physicsSettings.fixedTimeStep);
    hasher.I32(this->impl->physicsSettings.maxSubSteps);
    hasher.F32(physics->GetFPS());
    hasher.I32(physics->GetMaxSubStepCount());

    auto* rigidBodies = manager->GetRigidBodys();
    for (std::size_t index = 0U; index < rigidBodies->size(); ++index)
    {
        btRigidBody* body = (*rigidBodies)[index]->GetRigidBody();
        const btCollisionShape* shape =
            body != nullptr ? body->getCollisionShape() : nullptr;
        hasher.U32(static_cast<std::uint32_t>(index));
        hasher.F32(
            shape != nullptr ? static_cast<float>(shape->getMargin()) : 0.0f
        );
        HashShapeDimensions(hasher, shape);
    }

    // Instance policies (Saba path constants; contract requires them hashed).
    hasher.U32(0U);  // linked-body collision policy (none)
    hasher.U32(1U);  // deactivation policy (Saba DISABLE_DEACTIVATION default)
    hasher.U32(1U);  // compatibility profile (saba-compatible)
    hasher.U32(1U);  // physics-layout conversion version
    hasher.F32(1.0f);  // model scale
    fingerprint = hasher.state;
}

#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
TimelineStatus SabaMmdRuntimeModel::StepRestoredPhysicsForProbe(
    std::uint32_t exactSubsteps
)
{
    if (this->IsPoisoned())
        return TimelineStatus::Poisoned;
    if (this->impl->model == nullptr)
        return TimelineStatus::NoPhysics;
    saba::MMDPhysicsManager* manager = this->impl->model->GetPhysicsManager();
    if (manager == nullptr || manager->GetMMDPhysics() == nullptr)
        return TimelineStatus::NoPhysics;
    if (!this->impl->physicsSettings.enabled)
        return TimelineStatus::UnsupportedReplayProfile;
    const float delta = static_cast<float>(exactSubsteps) / 120.0f;
    const int executed =
        this->impl->model->UpdatePhysicsAnimation(delta);
    if (executed != static_cast<int>(exactSubsteps))
        return TimelineStatus::DeterminismViolation;
    this->impl->lastExecutedSubsteps = exactSubsteps;
    // The probe advances Bullet without re-evaluating animation, so it is
    // not a complete Canonical Frame Boundary.
    this->impl->lastBoundaryCanonical = false;
    return TimelineStatus::Ok;
}

void SabaMmdRuntimeModel::SetFaultInjectionPhase(int phase) noexcept
{
    this->impl->faultInjectionPhase = phase;
}

int SabaMmdRuntimeModel::FaultInjectionPhase() const noexcept
{
    return this->impl->faultInjectionPhase;
}
#endif

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
    ++this->impl->morphRevision;
    return true;
}

void SabaMmdRuntimeModel::ClearMotion()
{
    this->impl->vmdAnimation.reset();
    this->impl->vmdFile = saba::VMDFile{};
    this->impl->vmdLoaded = false;
    this->impl->vmdFrame = 0.0;
    this->impl->motionPaused = false;
    ++this->impl->morphRevision;
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
    ++this->impl->morphRevision;
}

double SabaMmdRuntimeModel::MotionFrame() const noexcept
{
    return this->impl->vmdFrame;
}

void SabaMmdRuntimeModel::SetMotionFrame(double frame)
{
    this->impl->vmdFrame = std::max(0.0, frame);
    ++this->impl->morphRevision;
}

double SabaMmdRuntimeModel::MotionMaxFrame() const noexcept
{
    return this->impl->vmdAnimation != nullptr
        ? static_cast<double>(this->impl->vmdAnimation->GetMaxKeyTime())
        : 0.0;
}

Pose& SabaMmdRuntimeModel::GetPose()
{
    if (this->impl->pose == nullptr)
        throw std::logic_error("Saba runtime has no initialized pose");
    return *this->impl->pose;
}

const Pose& SabaMmdRuntimeModel::GetPose() const
{
    if (this->impl->pose == nullptr)
        throw std::logic_error("Saba runtime has no initialized pose");
    return *this->impl->pose;
}

bool SabaMmdRuntimeModel::NeedsDynamicVertexUpload() const noexcept
{
    return true;
}

ModelVertexFrame SabaMmdRuntimeModel::VertexFrame() const noexcept
{
    if (this->impl->model == nullptr)
        return {};
    const std::size_t count = this->impl->model->GetVertexCount();
    return ModelVertexFrame{
        std::span<const glm::vec3>(
            this->impl->model->GetUpdatePositions(),
            count
        ),
        std::span<const glm::vec3>(
            this->impl->model->GetUpdateNormals(),
            count
        ),
        this->impl->vertexRevision
    };
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

const PhysicsInstance* SabaMmdRuntimeModel::TryGetPhysicsInstance() const noexcept
{
    return this->impl->ownedPhysics.get();
}

std::string_view SabaMmdRuntimeModel::BackendName() const noexcept
{
    return "saba-mmd";
}

ModelRuntimeCapabilities SabaMmdRuntimeModel::Capabilities() const
{
    ModelRuntimeCapabilities capabilities;
    capabilities.physics.supportsFixedTimeStep = true;
    capabilities.physics.supportsMaxSubSteps = true;
    capabilities.physics.supportsGravityOverride = true;
    capabilities.physics.supportsEnabledSwitch = true;
    capabilities.physics.supportsReset = true;
    // R1.2A adds read-only deterministic state capture; restore stays
    // disabled until R1.2B, and advanced Bullet tuning stays disabled.
    capabilities.physics.supportsSnapshotCapture = true;
#if defined(BT_USE_DOUBLE_PRECISION)
    // R1.2B v4.1.1: snapshot restore is frozen to 32-bit btScalar.
    capabilities.physics.supportsSnapshotRestore = false;
    capabilities.physics.supportsCanonicalRestore = false;
#else
    capabilities.physics.supportsSnapshotRestore = true;
    capabilities.physics.supportsCanonicalRestore = true;
#endif
    return capabilities;
}

ModelPhysicsRuntimeInfo SabaMmdRuntimeModel::PhysicsInfo() const
{
    ModelPhysicsRuntimeInfo info;
    info.available = this->impl->model != nullptr &&
        this->impl->model->GetPhysicsManager() != nullptr;
    info.ownsSimulationStep = this->TryGetPhysicsInstance() != nullptr &&
        this->TryGetPhysicsInstance()->OwnsSimulationStep();
    info.enabled = this->impl->physicsSettings.enabled;
    info.fixedTimeStep = this->impl->physicsSettings.fixedTimeStep;
    info.maxSubSteps = this->impl->physicsSettings.maxSubSteps;
    info.gravity = this->impl->physicsSettings.gravity;
    return info;
}

bool SabaMmdRuntimeModel::SetMorphWeight(
    std::string_view name,
    float weight
)
{
    if (this->impl->model == nullptr || !std::isfinite(weight))
        return false;
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    if (manager == nullptr)
        return false;
    saba::MMDMorph* morph = manager->GetMorph(std::string(name));
    if (morph == nullptr)
        return false;
    morph->SetWeight(weight);
    ++this->impl->morphRevision;
    return true;
}

bool SabaMmdRuntimeModel::SetMorphOverride(
    std::string_view name,
    float weight
)
{
    if (this->impl->model == nullptr || !std::isfinite(weight))
        return false;
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    if (manager == nullptr)
        return false;
    saba::MMDMorph* morph = manager->GetMorph(std::string(name));
    if (morph == nullptr)
        return false;
    this->impl->userMorphOverrides[std::string(name)] = weight;
    morph->SetWeight(weight);
    ++this->impl->morphRevision;
    return true;
}

void SabaMmdRuntimeModel::ClearMorphOverride(std::string_view name)
{
    this->impl->userMorphOverrides.erase(std::string(name));
}

void SabaMmdRuntimeModel::ClearAllMorphOverrides()
{
    this->impl->userMorphOverrides.clear();
}

std::optional<float> SabaMmdRuntimeModel::MorphWeight(
    std::string_view name
) const
{
    if (this->impl->model == nullptr)
        return std::nullopt;
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    if (manager == nullptr)
        return std::nullopt;
    saba::MMDMorph* morph = manager->GetMorph(std::string(name));
    return morph != nullptr
        ? std::optional<float>(morph->GetWeight())
        : std::nullopt;
}

std::size_t SabaMmdRuntimeModel::MorphCount() const noexcept
{
    if (this->impl->model == nullptr)
        return 0U;
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    return manager != nullptr ? manager->GetMorphCount() : 0U;
}

bool SabaMmdRuntimeModel::DescribeMorph(
    std::size_t index,
    MorphDescriptor& output
) const
{
    if (this->impl->model == nullptr)
        return false;
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    if (manager == nullptr || index >= manager->GetMorphCount())
        return false;
    saba::MMDMorph* morph = manager->GetMorph(index);
    if (morph == nullptr)
        return false;
    output.name = morph->GetName();
    output.kind = MorphKind::Vertex;

    // Saba's MMDMorph does not expose PMX morph kinds; resolve the kind from
    // the WISTERIA ModelAsset morph definitions by name when available.
    if (this->impl->asset != nullptr &&
        this->impl->asset->HasMorphs())
    {
        const MorphSet& morphSet = this->impl->asset->GetMorphSet();
        const std::optional<MorphIndex> morphIndex =
            morphSet.FindMorph(output.name);
        if (morphIndex.has_value())
        {
            output.kind = morphSet.DefinitionAt(*morphIndex).kind;
        }
    }
    return true;
}

bool SabaMmdRuntimeModel::ReadMorphState(
    std::size_t index,
    MorphRuntimeState& output
) const
{
    if (this->impl->model == nullptr)
        return false;
    saba::MMDMorphManager* manager = this->impl->model->GetMorphManager();
    if (manager == nullptr || index >= manager->GetMorphCount())
        return false;
    saba::MMDMorph* morph = manager->GetMorph(index);
    if (morph == nullptr)
        return false;
    output.rawWeight = morph->GetWeight();
    // Saba does not expose an authoritative evaluated weight separately;
    // leave effectiveWeight unset rather than synthesizing it.
    output.effectiveWeight.reset();
    return true;
}

std::uint64_t SabaMmdRuntimeModel::MorphRevision() const noexcept
{
    return this->impl->morphRevision;
}

void SabaMmdRuntimeModel::SetMmdIkEnabled(BoneIndex bone, bool enabled)
{
    if (!IsValidBoneIndex(bone, this->impl->bones))
        return;
    this->impl->mmdIkOverrides[bone] = enabled;
    ApplyIkEnable(
        this->impl->model,
        this->impl->sabaBoneNames[bone],
        enabled
    );
}

BoneIndex SabaMmdRuntimeModel::FindBoneIndex(const std::string& name) const
{
    for (std::size_t index = 0U; index < this->impl->bones.size(); ++index)
    {
        if (this->impl->sabaBoneNames[index] == name)
            return static_cast<BoneIndex>(index);
    }
    return InvalidBoneIndex;
}

void SabaMmdRuntimeModel::ApplyMmdIkOverrides() noexcept
{
    for (const auto& [bone, enabled] : this->impl->mmdIkOverrides)
    {
        if (!IsValidBoneIndex(bone, this->impl->bones))
            continue;
        ApplyIkEnable(
            this->impl->model,
            this->impl->sabaBoneNames[bone],
            enabled
        );
    }
}

void SabaMmdRuntimeModel::SyncPoseFromSaba()
{
    if (this->impl->model == nullptr || this->impl->pose == nullptr)
        return;
    saba::MMDNodeManager* nodes = this->impl->model->GetNodeManager();
    if (nodes == nullptr || nodes->GetNodeCount() != this->impl->pose->BoneCount())
        return;
    std::vector<glm::mat4> localMatrices;
    localMatrices.reserve(nodes->GetNodeCount());
    for (std::size_t index = 0U; index < nodes->GetNodeCount(); ++index)
    {
        saba::MMDNode* node = nodes->GetMMDNode(index);
        localMatrices.push_back(
            node != nullptr ? node->GetLocalTransform() : glm::mat4(1.0f)
        );
    }
    this->impl->pose->SetLocalMatrices(localMatrices);
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

std::optional<CameraTrackSample>
SabaMmdRuntimeModel::SampleCameraMotion(float frame) const
{
    if (this->impl->cameraAnimation == nullptr)
        return std::nullopt;
    this->impl->cameraAnimation->Evaluate(frame);
    const saba::MMDCamera& mmdCamera =
        this->impl->cameraAnimation->GetCamera();
    return CameraTrackSample{
        frame,
        mmdCamera.m_interest,
        glm::degrees(mmdCamera.m_rotate),
        mmdCamera.m_distance,
        glm::degrees(mmdCamera.m_fov),
        std::nullopt
    };
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

std::optional<LightTrackSample>
SabaMmdRuntimeModel::SampleLightMotion(float frame) const
{
    if (!this->impl->lightTrack.has_value())
        return std::nullopt;
    LightKeyframe sample;
    if (!this->impl->lightTrack->Sample(frame, sample))
        return std::nullopt;
    return LightTrackSample{
        frame,
        glm::clamp(sample.color, glm::vec3(0.0f), glm::vec3(1.0f)),
        sample.position
    };
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
