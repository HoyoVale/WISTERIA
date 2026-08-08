#pragma once

#include <cstdint>
#include <span>
#include <optional>
#include <string_view>
#include <glm/vec3.hpp>
#include "wisteria/core/root_motion.hpp"
#include "wisteria/runtime/frame_snapshot.hpp"

namespace wisteria
{
class Animator;
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

// R1.6 Phase 0C: neutral per-material evaluated terminal state. The texture
// channels keep Saba's independent multiply + add factors because a single
// combined factor cannot represent the PMX material morph terminal state.
struct MaterialRuntimeOverride
{
    glm::vec4 diffuse{1.0f};         // RGBA, includes alpha
    glm::vec3 specular{1.0f};
    float shininess = 32.0f;
    glm::vec3 ambient{0.0f};
    glm::vec4 edgeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;

    glm::vec4 textureMultiply{1.0f};
    glm::vec4 textureAdd{0.0f};

    glm::vec4 sphereTextureMultiply{1.0f};
    glm::vec4 sphereTextureAdd{0.0f};

    glm::vec4 toonTextureMultiply{1.0f};
    glm::vec4 toonTextureAdd{0.0f};
};

// R1.6 Phase 0C: transient renderer-facing view. Zero-copy spans point into
// runtime-owned buffers and are valid until the next runtime state mutation
// (Update / Reset / exact step / seek / restore / morph override) or
// destruction. Renderer consumes it within one frame; it is never stored.
struct ModelRenderFrameView
{
    ModelVertexFrame geometry;
    std::span<const glm::vec2> uvs;
    std::span<const MaterialRuntimeOverride> materials;
    const Pose* pose = nullptr;
    const MorphState* morphState = nullptr;
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
    // Optional Pose channel. A vertex-only / physics-only / morph-only
    // runtime returns nullptr instead of faking a skeleton. GetPose() is a
    // convenience that throws std::logic_error when no Pose exists.
    virtual Pose* TryGetPose() noexcept = 0;
    virtual const Pose* TryGetPose() const noexcept = 0;
    Pose& GetPose();
    const Pose& GetPose() const;
    virtual bool NeedsDynamicVertexUpload() const noexcept = 0;
    virtual ModelVertexFrame VertexFrame() const noexcept = 0;
    virtual ModelFrameView ProduceFrameView() const;
    virtual ModelRenderFrameView ProduceRenderFrameView() const;
    virtual PhysicsInstance* TryGetPhysicsInstance() noexcept = 0;
    virtual const PhysicsInstance* TryGetPhysicsInstance() const noexcept = 0;
    virtual std::string_view BackendName() const noexcept = 0;

    // Optional morph state channel. Saba manages morphs internally and
    // returns nullptr; WisteriaGeneric returns its owned MorphState when the
    // asset has morphs. Neutral snapshot access goes through MorphCount /
    // DescribeMorph / ReadMorphState / MorphRevision, not through this
    // pointer.
    virtual MorphState* TryGetMorphState() noexcept
    {
        return nullptr;
    }

    virtual const MorphState* TryGetMorphState() const noexcept
    {
        return nullptr;
    }

    // Optional animator channel. WisteriaGeneric returns its owned Animator
    // when a skeleton exists (HasSkeleton → Animator exists); Saba returns
    // nullptr because it owns its motion timeline internally.
    virtual Animator* TryGetAnimator() noexcept
    {
        return nullptr;
    }

    virtual const Animator* TryGetAnimator() const noexcept
    {
        return nullptr;
    }

    // R1.5 single-consumer root motion. Returns the pending delta from the
    // last Update and clears it in the same call. No thread-safety claim:
    // runtimes stay creator-thread-affine. Default is identity.
    virtual RootMotionDelta ConsumeRootMotion() noexcept
    {
        return {};
    }

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
