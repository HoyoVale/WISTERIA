#pragma once

#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/runtime/determinism.hpp"
#include "wisteria/animation/bone.hpp"
#include "wisteria/animation/morph.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <glm/vec3.hpp>

namespace wisteria
{
class PhysicsInstance;

struct MmdPhysicsRuntimeSettings
{
    float fixedTimeStep = 1.0f / 120.0f;
    int maxSubSteps = 10;
    glm::vec3 gravity{0.0f, -98.0f, 0.0f};
    bool enabled = true;
};

// Neutral MMD light sample (Scene-level track output). Uses the WISTERIA
// MMD coordinate convention: color is clamped to [0,1]; position already
// applies the z-axis flip from the raw VMD data. Availability is expressed
// by the outer std::optional, not by an in-struct flag.
struct LightTrackSample
{
    float frame = 0.0f;
    glm::vec3 color{1.0f};
    glm::vec3 position{0.5f, 1.0f, 0.5f};
};

// Neutral MMD camera sample (Scene-level track output). Uses the WISTERIA
// MMD coordinate convention: rotation and viewAngle are in degrees, interest
// and distance follow MMD semantics. perspective is retained for query and
// export; the current WISTERIA Camera application layer only supports
// perspective projection, so orthographic MMD cameras are marked unsupported.
struct CameraTrackSample
{
    float frame = 0.0f;
    glm::vec3 interest{0.0f};
    glm::vec3 rotation{0.0f};
    float distance = 0.0f;
    float viewAngle = 0.0f;
    // nullopt when the backend cannot authoritatively report the projection
    // mode (Saba's VMDCameraAnimation drops the VMD perspective flag).
    std::optional<bool> perspective;
};

enum class MmdSkinningKind : std::uint8_t
{
    LinearBlend,
    Sdef,
    DualQuaternion
};

// MMD-specific runtime abstraction. SabaMmdRuntimeModel and
// WisteriaMmdRuntimeModel both implement this interface.
class MmdRuntimeModel : public RuntimeModelBase
{
public:
    // VMD IK state switches. The engine may override the VMD's per-frame IK
    // state for individual controller bones; the override persists until a
    // later call changes it.
    virtual void SetMmdIkEnabled(BoneIndex bone, bool enabled) = 0;
    // Maps a bone name (UTF-8, matching PMX bone names) to this runtime's
    // BoneIndex space, or InvalidBoneIndex when the model has no such bone.
    virtual BoneIndex FindBoneIndex(const std::string& name) const = 0;

    // --- Single-motion control (thin adapter, no playlist) ---
    //
    // Saba evaluates one VMD animation at a time; these methods expose that
    // capability without committing to an orchestration/playlist design.
    virtual bool LoadMotion(const std::filesystem::path& vmdPath) = 0;
    virtual void ClearMotion() = 0;
    virtual bool HasMotion() const noexcept = 0;
    virtual void SetMotionLooping(bool looping) = 0;
    virtual bool IsMotionLooping() const noexcept = 0;
    virtual void PauseMotion() = 0;
    virtual void ResumeMotion() = 0;
    virtual bool IsMotionPaused() const noexcept = 0;
    virtual void RestartMotion(bool resetPhysics = true) = 0;
    virtual double MotionFrame() const noexcept = 0;
    virtual void SetMotionFrame(double frame) = 0;
    virtual double MotionMaxFrame() const noexcept = 0;

    // --- Camera animation (thin adapter over Saba VMDCameraAnimation) ---
    // LoadCameraMotion parses VMD camera frames; SampleCameraMotion returns a
    // neutral sample. WISTERIA's application layer converts the sample into
    // host camera objects (see wisteria/mmd/mmd_camera_conversion.hpp).
    virtual bool LoadCameraMotion(const std::filesystem::path& vmdPath) = 0;
    virtual std::optional<CameraTrackSample>
        SampleCameraMotion(float frame) const = 0;

    // --- Light animation (thin adapter over VMD light frames) ---
    // LoadLightMotion parses VMD light frames; SampleLightMotion returns a
    // neutral sample. WISTERIA's application layer converts the sample into
    // host light objects (see wisteria/mmd/mmd_light_conversion.hpp).
    virtual bool LoadLightMotion(const std::filesystem::path& vmdPath) = 0;
    virtual std::optional<LightTrackSample>
        SampleLightMotion(float frame) const = 0;

    // Skinning strategy used by this implementation.
    virtual MmdSkinningKind SkinningKind() const noexcept = 0;

    // MMD physics adapter (Saba or WISTERIA compat).
    virtual void SetMmdPhysicsSettings(
        const MmdPhysicsRuntimeSettings& settings
    ) = 0;
    virtual void ResetMmdPhysics() = 0;
    virtual PhysicsInstance* GetMmdPhysics() noexcept = 0;

    // R1.2A deterministic timeline entry (Saba implementation). Evaluates
    // the target motion frame under the requested seek policy; internals
    // delegate to IDeterministicFrameStepper and never compose Saba physics
    // phases directly. ReplayConfig is strictly validated (30Hz/120Hz,
    // no warmup, no looping, physics enabled). Backends that do not support
    // deterministic timelines inherit the UnsupportedReplayProfile default,
    // so future MMD runtimes and test doubles are not forced to implement it.
    virtual TimelineStatus EvaluateTick(
        MotionFrameIndex,
        SeekPolicy,
        const ReplayConfig& = {}
    )
    {
        return TimelineStatus::UnsupportedReplayProfile;
    }

    // Engine-level morph overrides survive VMD evaluation (R1.2 contract §5)
    // and are re-applied every frame. SetMorphWeight() itself remains an
    // instantaneous control with the pre-R1.2 semantics; only these explicit
    // entries create a persistent override. Backends without override
    // support return false / are no-ops.
    virtual bool SetMorphOverride(std::string_view name, float weight)
    {
        (void)name;
        (void)weight;
        return false;
    }
    virtual void ClearMorphOverride(std::string_view name)
    {
        (void)name;
    }
    virtual void ClearAllMorphOverrides()
    {
    }
};
}  // namespace wisteria
