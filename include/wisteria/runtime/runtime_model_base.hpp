#pragma once

#include <cstdint>
#include <span>
#include <optional>
#include <string_view>
#include <glm/vec3.hpp>
#include "wisteria/runtime/frame_snapshot.hpp"

namespace wisteria
{
class Pose;
class Mesh;
class PhysicsInstance;

// Format-neutral, read-only view of one runtime's deformed geometry. The
// backend owns the arrays; WISTERIA decides when and where they are uploaded.
struct ModelVertexFrame
{
    std::span<const glm::vec3> positions;
    std::span<const glm::vec3> normals;
    std::uint64_t revision = 0U;
};

// Zero-copy transient view of one runtime evaluation. Spans point into the
// backend's internal buffers and are only valid until the next
// Update/Reset/destroy. Never expose to long-lived C API handles.
struct ModelFrameView
{
    ModelVertexFrame geometry;
    const Pose* pose = nullptr;
    std::uint64_t updateSerial = 0U;
};

// Engine-facing contract for any model runtime. Scene/Entity/Renderer depend
// on this interface rather than on Saba, glTF, VRM, or another backend.
class IModelRuntimeDriver
{
public:
    virtual ~IModelRuntimeDriver() = default;

    virtual bool Initialize() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Reset() = 0;
    virtual Pose& GetPose() = 0;
    virtual const Pose& GetPose() const = 0;
    virtual bool NeedsDynamicVertexUpload() const noexcept = 0;
    virtual ModelVertexFrame VertexFrame() const noexcept = 0;
    virtual ModelFrameView ProduceFrameView() const;
    virtual PhysicsInstance* TryGetPhysicsInstance() noexcept = 0;
    virtual const PhysicsInstance* TryGetPhysicsInstance() const noexcept = 0;
    virtual std::string_view BackendName() const noexcept = 0;

    // Runtime capability and physics configuration description. Not per-frame
    // state; backends advertise exactly what they expose.
    virtual ModelRuntimeCapabilities Capabilities() const;
    virtual ModelPhysicsRuntimeInfo PhysicsInfo() const;

    // Optional common capabilities. Backends that do not support named
    // morphs return false/nullopt without leaking format-specific types.
    virtual bool SetMorphWeight(std::string_view name, float weight)
    {
        (void)name;
        (void)weight;
        return false;
    }
    virtual std::optional<float> MorphWeight(
        std::string_view name
    ) const
    {
        (void)name;
        return std::nullopt;
    }

    // Optional neutral morph enumeration/state. Backends without morphs
    // return 0 / false. This keeps ModelInstance independent of MMD types.
    virtual std::size_t MorphCount() const noexcept
    {
        return 0U;
    }

    virtual bool DescribeMorph(
        std::size_t,
        MorphDescriptor&
    ) const
    {
        return false;
    }

    virtual bool ReadMorphState(
        std::size_t,
        MorphRuntimeState&
    ) const
    {
        return false;
    }

    virtual std::uint64_t MorphRevision() const noexcept
    {
        return 0U;
    }

    // Optional playback metadata for backends that drive animation timelines.
    // Static backends leave the defaults.
    virtual double MotionFrame() const noexcept
    {
        return 0.0;
    }

    virtual bool IsMotionPaused() const noexcept
    {
        return false;
    }

    virtual bool IsMotionLooping() const noexcept
    {
        return false;
    }
};

// Compatibility base kept for existing callers. The default Mesh upload was
// a WISTERIA-owned adapter over ModelVertexFrame; R1.1E removed it so dynamic
// geometry upload flows exclusively through ModelInstance. Backends produce
// ModelVertexFrame/ModelFrameView and never touch render resources.
class RuntimeModelBase : public IModelRuntimeDriver
{
public:
};
}  // namespace wisteria
