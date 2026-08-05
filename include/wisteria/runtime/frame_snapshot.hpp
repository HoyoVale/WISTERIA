#pragma once

#include "wisteria/animation/morph.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace wisteria
{
// Per-channel capture mask for explicit snapshot capture. Callers request
// exactly the channels they need; there is no hidden geometry threshold.
enum class CaptureMask : std::uint8_t
{
    None = 0,
    Pose = 1 << 0,
    Morphs = 1 << 1,
    Geometry = 1 << 2,
    All = Pose | Morphs | Geometry
};

inline CaptureMask operator|(CaptureMask left, CaptureMask right)
{
    return static_cast<CaptureMask>(
        static_cast<std::uint8_t>(left) |
        static_cast<std::uint8_t>(right)
    );
}

inline bool HasFlag(CaptureMask mask, CaptureMask flag)
{
    return (static_cast<std::uint8_t>(mask) &
            static_cast<std::uint8_t>(flag)) != 0;
}

// Asset-level skeleton metadata. Immutable; generated once per asset, not
// part of per-frame state.
struct BoneDescriptor
{
    std::string name;
    int32_t parentIndex = -1;
    glm::mat4 bindLocalTransform{1.0f};
};

struct SkeletonSnapshot
{
    std::vector<BoneDescriptor> bones;
};

// Per-frame pose state. Matrices are model-space (not Scene world space);
// world-space bone matrices require multiplying by the Entity Transform.
struct PoseSnapshot
{
    std::vector<glm::mat4> localTransforms;
    std::vector<glm::mat4> globalTransforms;
    std::vector<glm::mat4> skinningTransforms;
    std::uint64_t poseRevision = 0U;
    bool captured = false;
};

// Per-frame morph state. rawWeight is the weight set on the morph itself
// (Group morphs record their own weight). effectiveWeight is only present
// when the backend can provide an authoritative evaluated weight; it must
// never be synthesized by WISTERIA using an independent algorithm.
struct MorphEntrySnapshot
{
    std::string name;
    MorphKind kind = MorphKind::Vertex;
    float rawWeight = 0.0f;
    std::optional<float> effectiveWeight;
};

// Neutral morph descriptor: identity and kind, resolved from the backend's
// morph space (e.g. saba::MMDMorphManager).
struct MorphDescriptor
{
    std::string name;
    MorphKind kind = MorphKind::Vertex;
};

// Runtime morph state read from the backend. rawWeight is the weight set on
// the morph itself; effectiveWeight is only set when the backend provides an
// authoritative evaluated weight.
struct MorphRuntimeState
{
    float rawWeight = 0.0f;
    std::optional<float> effectiveWeight;
};

struct MorphSnapshot
{
    std::vector<MorphEntrySnapshot> entries;
    std::uint64_t morphRevision = 0U;
    bool captured = false;
};

// Persistent deformed-vertex copy in canonical/source vertex order. Mesh
// topology, indices, UVs and SourceVertexIndices live in ModelAsset and are
// not duplicated here.
struct DeformedVertexSnapshot
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::uint64_t sourceRevision = 0U;
    bool captured = false;
};

// Frame metadata. updateSerial increments on every Update call;
// snapshotRevision only increments when a CaptureSnapshot rewrites a
// requested channel (i.e. it versions the persisted snapshot content, not
// the model's observable state directly).
struct ModelFrameMetadata
{
    std::uint64_t updateSerial = 0U;
    std::uint64_t snapshotRevision = 0U;
    double motionFrame = 0.0;
    bool motionPaused = false;
    bool motionLooping = false;
    bool valid = false;
};

// WISTERIA-owned persistent frame state. Owned by ModelInstance; valid until
// the next CaptureSnapshot overwrites it or the ModelInstance is destroyed.
// Contains no Saba/Bullet/OpenGL types.
struct ModelFrameSnapshot
{
    ModelFrameMetadata metadata;
    PoseSnapshot pose;
    MorphSnapshot morphs;
    DeformedVertexSnapshot geometry;
};

// Backend physics capability advertisement. Callers must never assume all
// model backends support the same parameters; these flags state what the
// current backend actually exposes.
struct PhysicsBackendCapabilities
{
    bool supportsFixedTimeStep = false;
    bool supportsMaxSubSteps = false;
    bool supportsGravityOverride = false;
    bool supportsEnabledSwitch = false;
    bool supportsReset = false;
    bool supportsSolverTuning = false;   // R1.2 前恒 false
    bool supportsPerBodyOverrides = false;  // R1.2 前恒 false
    bool supportsCcd = false;            // R1.2 前恒 false
    bool supportsCollisionMargin = false; // R1.2 前恒 false
    bool supportsJointOverrides = false;  // R1.2 前恒 false
    bool supportsSnapshot = false;        // R1.2 前恒 false
};

// Runtime capability description (not per-frame state).
struct ModelRuntimeCapabilities
{
    PhysicsBackendCapabilities physics;
};

// Current physics configuration + availability (not per-frame state).
struct ModelPhysicsRuntimeInfo
{
    bool available = false;            // backend created a physics world
    bool ownsSimulationStep = false;   // backend drives its own simulation
    bool enabled = false;
    float fixedTimeStep = 1.0f / 120.0f;
    int maxSubSteps = 10;
    glm::vec3 gravity{0.0f, -98.0f, 0.0f};
};
}  // namespace wisteria
