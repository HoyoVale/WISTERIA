#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wisteria
{
// Integer timeline units (R1.2 contract §2). MotionFrameIndex is the main
// loop unit (VMD frames at 30Hz); TimelineTick is the physics fixed-step
// index (120Hz). Core C++ interfaces never accept fractional frames.
using TimelineTick = std::uint64_t;
using MotionFrameIndex = std::uint64_t;

// R1.2A strictly freezes the 30Hz-motion / 120Hz-physics profile. Other
// profiles, warmup frames, or looping are rejected with
// UnsupportedReplayProfile; the fields exist so the contract can be extended
// without breaking callers.
struct ReplayConfig
{
    std::uint32_t motionFps = 30;
    std::uint32_t physicsHz = 120;
    std::uint32_t warmupFrames = 0;
    bool loopMotion = false;
};

enum class SeekPolicy
{
    PreserveState,        // interactive preview: keep physics history
    ResetAtTarget,        // canonical reset at target pose, no stepping
    ReplayFromStart,      // deterministic: PrepareFrameZero + exact frames
    ReplayFromCheckpoint  // R1.2C; not implemented yet
};

enum class TimelineStatus
{
    Ok,
    NoPhysics,
    InvalidCheckpoint,
    UnsupportedReplayProfile,
    // Deterministic stepping state machine rejections.
    InvalidState,          // StepMotionFrameExact before PrepareFrameZero
    NonSequentialFrame,    // frame != expectedNextFrame
    DeterminismViolation,  // live physics settings/substeps/accumulator broke
                           // the frozen 30Hz/120Hz canonical boundary
    // R1.2B restore status codes (contract v4.1.1).
    SnapshotMismatch,      // schema/layout/config mismatch or tampered fields
    InvalidSnapshot,       // invalid values: non-finite, bad rotation basis,
                           // missing canonical claim
    Poisoned,              // write-phase failure; instance must be rebuilt
};

// Read-only diagnostics of the last canonical frame boundary. executedSubsteps
// is the raw Bullet stepSimulation return value; remainingAccumulator is
// Bullet's internal frame accumulator (m_localTime), which must be zero at
// every Canonical Frame Boundary.
struct PhysicsStepDiagnostics
{
    std::uint32_t executedSubsteps = 0;
    double remainingAccumulator = 0.0;
    // R1.2B: true while the instance is in Poisoned state. Read-only
    // diagnostics remain available; deterministic entries are rejected.
    bool poisoned = false;
};

// PMX rigid-body semantic mode, taken from the immutable model definition.
// It must never be inferred from Bullet's runtime invMass.
enum class PmxRigidBodyMode : std::uint8_t
{
    FollowBone = 0,        // PMX Mode 0 (Static / kinematic)
    Physics = 1,           // PMX Mode 1 (Dynamic)
    PhysicsWithBone = 2,   // PMX Mode 2 (DynamicAndBoneMerge)
};

// Bullet btTransform stores a 3x3 basis plus an origin, not a quaternion.
// Quaternion round-trips are not bit-reversible, so the snapshot carries the
// basis as 9 explicit floats (column-major).
struct RigidTransformSnapshot
{
    glm::vec3 position{0.0f};
    // Column-major 3x3 basis, serialized explicitly:
    // [c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z]
    std::array<float, 9> rotationBasis{};
};

// Neutral rigid-body state (R1.2A: read-only capture; R1.2B: restore).
// No Bullet/Saba types are exposed.
struct RigidBodySnapshot
{
    std::uint32_t index = 0;
    PmxRigidBodyMode mode = PmxRigidBodyMode::FollowBone;
    // PMX raw mass bit pattern from the immutable definition; used only for
    // fingerprints and per-body validation, never to decide runtime mode.
    float definitionMass = 0.0f;
    RigidTransformSnapshot worldTransform;
    RigidTransformSnapshot interpolationTransform;
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 interpolationLinearVelocity{0.0f};
    glm::vec3 interpolationAngularVelocity{0.0f};
    glm::vec3 totalForce{0.0f};
    glm::vec3 totalTorque{0.0f};
    std::int32_t activationState = 0;
    float deactivationTime = 0.0f;
};

// Neutral physics state at a Canonical Frame Boundary.
struct PhysicsSnapshot
{
    std::uint32_t schemaVersion = 2;
    std::uint64_t layoutFingerprint = 0;
    std::uint64_t physicsConfigurationFingerprint = 0;
    std::vector<RigidBodySnapshot> rigidBodies;
    MotionFrameIndex motionFrame = 0;  // VMD frame (30Hz motion boundary)
    TimelineTick physicsTick = 0;      // 120Hz tick (default = frame * 4)
    std::uint32_t jointCount = 0;
    // Claim written by the capture path when the state lies on a complete
    // Canonical Frame Boundary. Restore validates it and never modifies it.
    bool canonical = false;
};

// R1.2B state restore interface. Capture (read-only) lives in
// IDeterministicPhysicsObservation. Restore semantics follow
// docs/architecture/R1_2B_RESTORE_STATE_CONTRACT.md (v4.1.1).
class IPhysicsStateAccess
{
public:
    virtual ~IPhysicsStateAccess() = default;

    // Only canonical=true snapshots with matching layout/configuration
    // fingerprints and an animation precondition are accepted. On success
    // deterministicPrepared stays false (continuation is R1.2C).
    virtual TimelineStatus RestoreState(
        const PhysicsSnapshot& snapshot
    ) = 0;
};

// R1.2A deterministic frame-stepping contract. Upper layers never compose
// Saba's physics phases themselves; they only call these two entries. The
// runtime enforces a strict state machine: StepMotionFrameExact is only valid
// after a successful PrepareFrameZero and frames must arrive in ascending
// sequential order (1, 2, 3, ...). Use EvaluateTick(target,
// ReplayFromStart) for arbitrary target frames.
class IDeterministicFrameStepper
{
public:
    virtual ~IDeterministicFrameStepper() = default;

    // Evaluates frame 0 (animation/Morph/IK), synchronizes kinematic
    // targets, performs a no-step canonical reset, and publishes
    // Pose/Vertex. No physics substep is executed.
    virtual TimelineStatus PrepareFrameZero(
        const ReplayConfig& config
    ) = 0;

    // Advances exactly one 30Hz motion frame: set frame, evaluate
    // animation/Morph/IK, sync kinematic bodies, execute the fixed
    // physics substeps, write back dynamic bodies, update Pose/Vertex,
    // and clear accumulator/forces.
    virtual TimelineStatus StepMotionFrameExact(
        MotionFrameIndex frame,
        const ReplayConfig& config
    ) = 0;
};

// R1.2A read-only physics observation. Restore stays in R1.2B.
class IDeterministicPhysicsObservation
{
public:
    virtual ~IDeterministicPhysicsObservation() = default;

    virtual TimelineStatus CaptureState(
        PhysicsSnapshot& output
    ) const = 0;

    virtual TimelineStatus ReadStepDiagnostics(
        PhysicsStepDiagnostics& output
    ) const = 0;
};
}  // namespace wisteria
