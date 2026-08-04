#pragma once

#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/animation/bone.hpp"

#include <cstdint>
#include <filesystem>

class Camera;
class CameraTrack;
class DirectionalLight;
class LightTrack;
class PhysicsInstance;

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
    // VMD IK state switches.
    virtual void SetMmdIkEnabled(BoneIndex bone, bool enabled) = 0;

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
    // LoadCameraMotion reads camera frames from a VMD file; ApplyCameraMotion
    // evaluates them at a MMD frame time. ApplyCameraTrack remains for
    // programmatic tracks built by the caller.
    virtual bool LoadCameraMotion(const std::filesystem::path& vmdPath) = 0;
    virtual void ApplyCameraMotion(float frame, Camera& camera) = 0;
    virtual void ApplyCameraTrack(
        const CameraTrack& track,
        float time,
        Camera& camera
    ) = 0;

    // --- Light animation (thin adapter over VMD light frames) ---
    virtual bool LoadLightMotion(const std::filesystem::path& vmdPath) = 0;
    virtual void ApplyLightMotion(float frame, DirectionalLight& light) = 0;
    virtual void ApplyLightTrack(
        const LightTrack& track,
        float time,
        DirectionalLight& light
    ) = 0;

    // Skinning strategy used by this implementation.
    virtual MmdSkinningKind SkinningKind() const noexcept = 0;

    // MMD physics adapter (Saba or WISTERIA compat).
    virtual PhysicsInstance* GetMmdPhysics() noexcept = 0;
};
