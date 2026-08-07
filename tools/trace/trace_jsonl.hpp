#pragma once

#include "wisteria/mmd/physics/mmd_physics_trace.hpp"

#include <iosfwd>
#include <string>

namespace wisteria::trace
{
// R1.3 §6: JSONL writer/parser and the minimal diff tool. This is tooling,
// never the runtime: the runtime only fills MmdPhysicsTraceFrame and performs
// no file I/O.

// Writes one compact JSON line for a trace frame. Returns false on stream
// failure. Stable key ordering (nlohmann objects) keeps output comparable.
bool WriteTraceFrameJson(
    const MmdPhysicsTraceFrame& frame,
    std::ostream& output
);

// Parses one JSONL line. Returns false on malformed JSON or missing fields.
bool ReadTraceFrameJson(
    const std::string& line,
    MmdPhysicsTraceFrame& output
);

struct TraceDiffResult
{
    bool identical = true;
    bool hashMismatch = false;
    std::string profileA;
    std::string profileB;
    std::size_t comparedFrames = 0U;
    std::size_t lineCountA = 0U;
    std::size_t lineCountB = 0U;

    // Phase 0B extended locators.
    bool contactTopologyFound = false;
    MotionFrameIndex contactTopologyFrame = 0U;
    std::uint32_t contactTopologyBodyA = 0U;
    std::uint32_t contactTopologyBodyB = 0U;

    bool motionStateFound = false;
    MotionFrameIndex motionStateFrame = 0U;
    std::uint32_t motionStateBody = 0U;
    float motionStatePositionError = 0.0f;

    bool boneFound = false;
    MotionFrameIndex boneFrame = 0U;
    BoneIndex boneIndex = InvalidBoneIndex;
    float boneMaxMatrixDelta = 0.0f;

    bool firstFound = false;
    MotionFrameIndex firstFrame = 0U;
    std::uint32_t firstBody = 0U;
    float firstPositionError = 0.0f;
    float firstRotationErrorDeg = 0.0f;

    bool maxFound = false;
    MotionFrameIndex maxFrame = 0U;
    std::uint32_t maxBody = 0U;
    float maxPositionError = 0.0f;
    float maxRotationErrorDeg = 0.0f;

    bool jointDeltaFound = false;
    std::uint32_t jointIndex = 0U;
    float jointLinearDelta = 0.0f;
    float jointAngularDeltaDeg = 0.0f;
};

TraceDiffResult DiffTraceStreams(
    std::istream& left,
    std::istream& right
);

std::string FormatTraceDiff(const TraceDiffResult& result);
}  // namespace wisteria::trace
