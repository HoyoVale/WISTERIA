#pragma once

#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/animation/bone.hpp"

#include <cstdint>

class Camera;
class CameraTrack;
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

    // Applies a VMD camera track to a camera.
    virtual void ApplyCameraTrack(
        const CameraTrack& track,
        float time,
        Camera& camera
    ) = 0;

    // Skinning strategy used by this implementation.
    virtual MmdSkinningKind SkinningKind() const noexcept = 0;

    // MMD physics adapter (Saba or WISTERIA compat).
    virtual PhysicsInstance* GetMmdPhysics() noexcept = 0;
};
