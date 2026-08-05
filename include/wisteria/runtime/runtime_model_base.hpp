#pragma once

#include <cstdint>
#include <span>
#include <optional>
#include <string_view>
#include <glm/vec3.hpp>

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
    virtual PhysicsInstance* TryGetPhysicsInstance() noexcept = 0;
    virtual const PhysicsInstance* TryGetPhysicsInstance() const noexcept = 0;
    virtual std::string_view BackendName() const noexcept = 0;

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
};

// Compatibility base kept for existing callers. The default Mesh upload is a
// WISTERIA-owned adapter over ModelVertexFrame; concrete backends no longer
// need to know about render resources.
class RuntimeModelBase : public IModelRuntimeDriver
{
public:
    void UploadDynamicVertices(Mesh& mesh);
};
}  // namespace wisteria
