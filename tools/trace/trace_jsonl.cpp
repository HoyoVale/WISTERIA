#include "trace_jsonl.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <utility>

#include <glm/glm.hpp>

namespace wisteria::trace
{
namespace
{
constexpr float Epsilon = 1.0e-6f;

const char* ModeName(PmxRigidBodyMode mode)
{
    switch (mode)
    {
        case PmxRigidBodyMode::FollowBone:
            return "FollowBone";
        case PmxRigidBodyMode::Physics:
            return "Physics";
        case PmxRigidBodyMode::PhysicsWithBone:
            return "PhysicsWithBone";
    }
    return "Unknown";
}

bool ParseMode(const std::string& name, PmxRigidBodyMode& mode)
{
    if (name == "FollowBone")
        mode = PmxRigidBodyMode::FollowBone;
    else if (name == "Physics")
        mode = PmxRigidBodyMode::Physics;
    else if (name == "PhysicsWithBone")
        mode = PmxRigidBodyMode::PhysicsWithBone;
    else
        return false;
    return true;
}

nlohmann::json TransformJson(const MmdPhysicsTraceTransform& transform)
{
    return nlohmann::json{
        {"position",
         nlohmann::json::array({
             transform.position.x,
             transform.position.y,
             transform.position.z
         })},
        {"rotationBasis",
         nlohmann::json::array({
             transform.rotationBasis[0],
             transform.rotationBasis[1],
             transform.rotationBasis[2],
             transform.rotationBasis[3],
             transform.rotationBasis[4],
             transform.rotationBasis[5],
             transform.rotationBasis[6],
             transform.rotationBasis[7],
             transform.rotationBasis[8]
         })}
    };
}

bool ParseTransform(
    const nlohmann::json& object,
    MmdPhysicsTraceTransform& transform
)
{
    if (!object.contains("position") || !object.contains("rotationBasis"))
        return false;
    const auto& position = object.at("position");
    const auto& basis = object.at("rotationBasis");
    if (!position.is_array() || position.size() != 3U ||
        !basis.is_array() || basis.size() != 9U)
    {
        return false;
    }
    for (std::size_t index = 0U; index < 3U; ++index)
        transform.position[static_cast<int>(index)] =
            position[index].get<float>();
    for (std::size_t index = 0U; index < 9U; ++index)
        transform.rotationBasis[index] = basis[index].get<float>();
    return true;
}

nlohmann::json MatrixJson(const std::array<float, 16>& matrix)
{
    return nlohmann::json::array({
        matrix[0], matrix[1], matrix[2], matrix[3],
        matrix[4], matrix[5], matrix[6], matrix[7],
        matrix[8], matrix[9], matrix[10], matrix[11],
        matrix[12], matrix[13], matrix[14], matrix[15]
    });
}

bool ParseMatrix(
    const nlohmann::json& value,
    std::array<float, 16>& matrix
)
{
    if (!value.is_array() || value.size() != 16U)
        return false;
    for (std::size_t index = 0U; index < 16U; ++index)
        matrix[index] = value[index].get<float>();
    return true;
}

float PositionError(
    const MmdPhysicsTraceBody& left,
    const MmdPhysicsTraceBody& right
)
{
    const glm::vec3 delta = left.worldTransform.position -
        right.worldTransform.position;
    return glm::length(delta);
}

float RotationErrorDeg(
    const MmdPhysicsTraceTransform& left,
    const MmdPhysicsTraceTransform& right
)
{
    // R = L^T * R; angle = acos((trace(R) - 1) / 2). Columns are normalized
    // first so that comparing a basis to itself always yields 0 even when
    // the stored matrix is not perfectly orthonormal.
    const auto& a = left.rotationBasis;
    const auto& b = right.rotationBasis;
    // Bitwise-identical bases must always report zero rotation error:
    // normalization + acos is not exact under floating point and could turn
    // identical data into a ~0.04 degree false divergence.
    if (a == b)
        return 0.0f;
    std::array<glm::vec3, 3> columnsA{};
    std::array<glm::vec3, 3> columnsB{};
    for (int column = 0; column < 3; ++column)
    {
        columnsA[static_cast<std::size_t>(column)] = glm::vec3(
            a[static_cast<std::size_t>(column) * 3U + 0U],
            a[static_cast<std::size_t>(column) * 3U + 1U],
            a[static_cast<std::size_t>(column) * 3U + 2U]
        );
        columnsB[static_cast<std::size_t>(column)] = glm::vec3(
            b[static_cast<std::size_t>(column) * 3U + 0U],
            b[static_cast<std::size_t>(column) * 3U + 1U],
            b[static_cast<std::size_t>(column) * 3U + 2U]
        );
        const float lengthA = glm::length(columnsA[static_cast<std::size_t>(column)]);
        const float lengthB = glm::length(columnsB[static_cast<std::size_t>(column)]);
        // A degenerate (zero-length) column makes the stored basis an invalid
        // rotation; rotation error is not meaningful there and a self-compare
        // must never report a false divergence. Position error still catches
        // real differences.
        if (lengthA <= 1.0e-12f || lengthB <= 1.0e-12f)
            return 0.0f;
        columnsA[static_cast<std::size_t>(column)] /= lengthA;
        columnsB[static_cast<std::size_t>(column)] /= lengthB;
    }
    // R = A^T * B (column-major): R[colB][rowA] = dot(colA_rowA, colB).
    float trace = 0.0f;
    for (int column = 0; column < 3; ++column)
    {
        trace += glm::dot(
            columnsA[static_cast<std::size_t>(column)],
            columnsB[static_cast<std::size_t>(column)]
        );
    }
    const float clamped = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
    return glm::degrees(std::acos(clamped));
}

float RotationErrorDeg(
    const MmdPhysicsTraceBody& left,
    const MmdPhysicsTraceBody& right
)
{
    return RotationErrorDeg(
        left.worldTransform,
        right.worldTransform
    );
}

bool PairLess(
    const MmdPhysicsTraceContactPair& left,
    const MmdPhysicsTraceContactPair& right
)
{
    if (left.bodyA != right.bodyA)
        return left.bodyA < right.bodyA;
    return left.bodyB < right.bodyB;
}

const MmdPhysicsTraceBody* FindBody(
    const MmdPhysicsTraceFrame& frame,
    std::uint32_t index
)
{
    for (const MmdPhysicsTraceBody& body : frame.bodies)
    {
        if (body.index == index)
            return &body;
    }
    return nullptr;
}

const MmdPhysicsTraceBone* FindBone(
    const MmdPhysicsTraceFrame& frame,
    BoneIndex index
)
{
    for (const MmdPhysicsTraceBone& bone : frame.bones)
    {
        if (bone.index == index)
            return &bone;
    }
    return nullptr;
}
}  // namespace

bool WriteTraceFrameJson(
    const MmdPhysicsTraceFrame& frame,
    std::ostream& output
)
{
    nlohmann::json object;
    object["traceSchemaVersion"] = frame.traceSchemaVersion;
    object["backendIdentity"] = frame.backendIdentity;
    object["presetIdentity"] = frame.presetIdentity;
    object["effectiveConfigurationHash"] =
        frame.effectiveConfigurationHash;
    object["executionProfile"] = frame.executionProfile;
    object["modelHash"] = frame.modelHash;
    object["hasMotion"] = frame.hasMotion;
    object["motionHash"] = frame.motionHash;
    object["frame"] = frame.frame;
    object["physicsTick"] = frame.physicsTick;
    object["canonical"] = frame.canonical;
    object["stateHashes"] = {
        {"pose",
         {{"hash", frame.poseHash.hex}, {"valid", frame.poseHash.valid}}},
        {"physics",
         {{"hash", frame.physicsHash.hex},
          {"valid", frame.physicsHash.valid}}},
        {"vertex",
         {{"hash", frame.vertexHash.hex},
          {"valid", frame.vertexHash.valid}}}
    };

    object["bodies"] = nlohmann::json::array();
    for (const MmdPhysicsTraceBody& body : frame.bodies)
    {
        object["bodies"].push_back({
            {"index", body.index},
            {"mode", ModeName(body.mode)},
            {"worldTransform", TransformJson(body.worldTransform)},
            {"interpolationWorldTransform",
             TransformJson(body.interpolationWorldTransform)},
            {"motionStateTransform",
             TransformJson(body.motionStateTransform)},
            {"motionStateAvailable", body.motionStateAvailable},
            {"linearVelocity",
             nlohmann::json::array({
                 body.linearVelocity.x,
                 body.linearVelocity.y,
                 body.linearVelocity.z
             })},
            {"angularVelocity",
             nlohmann::json::array({
                 body.angularVelocity.x,
                 body.angularVelocity.y,
                 body.angularVelocity.z
             })}
        });
    }

    object["bones"] = nlohmann::json::array();
    for (const MmdPhysicsTraceBone& bone : frame.bones)
    {
        object["bones"].push_back({
            {"index", bone.index},
            {"local", MatrixJson(bone.localMatrix)},
            {"global", MatrixJson(bone.globalMatrix)}
        });
    }

    object["joints"] = nlohmann::json::array();
    for (const MmdPhysicsTraceJoint& joint : frame.joints)
    {
        object["joints"].push_back({
            {"index", joint.index},
            {"rawLinearError", joint.rawLinearError},
            {"linearViolation", joint.linearViolation},
            {"rawAngularErrorDeg", joint.rawAngularErrorDeg},
            {"angularViolationDeg", joint.angularViolationDeg}
        });
    }

    object["contactPairs"] = nlohmann::json::array();
    for (const MmdPhysicsTraceContactPair& pair : frame.contactPairs)
    {
        object["contactPairs"].push_back({
            {"bodyA", pair.bodyA},
            {"bodyB", pair.bodyB},
            {"pointCount", pair.pointCount},
            {"maxPenetration", pair.maxPenetration},
            {"normalImpulse", pair.normalImpulse}
        });
    }

    object["events"] = nlohmann::json::array();
    for (const std::string& event : frame.events)
        object["events"].push_back(event);

    output << object.dump() << '\n';
    return static_cast<bool>(output);
}

bool ReadTraceFrameJson(
    const std::string& line,
    MmdPhysicsTraceFrame& output
)
{
    try
    {
        const nlohmann::json object = nlohmann::json::parse(line);
        MmdPhysicsTraceFrame frame;
        frame.traceSchemaVersion =
            object.at("traceSchemaVersion").get<std::uint32_t>();
        frame.backendIdentity = object.at("backendIdentity").get<std::string>();
        frame.presetIdentity = object.at("presetIdentity").get<std::string>();
        frame.effectiveConfigurationHash =
            object.at("effectiveConfigurationHash").get<std::string>();
        frame.executionProfile =
            object.at("executionProfile").get<std::string>();
        frame.modelHash = object.at("modelHash").get<std::string>();
        frame.hasMotion = object.at("hasMotion").get<bool>();
        frame.motionHash = object.at("motionHash").get<std::string>();
        frame.frame = object.at("frame").get<MotionFrameIndex>();
        frame.physicsTick = object.at("physicsTick").get<TimelineTick>();
        frame.canonical = object.at("canonical").get<bool>();

        const auto& stateHashes = object.at("stateHashes");
        frame.poseHash.hex =
            stateHashes.at("pose").at("hash").get<std::string>();
        frame.poseHash.valid =
            stateHashes.at("pose").at("valid").get<bool>();
        frame.physicsHash.hex =
            stateHashes.at("physics").at("hash").get<std::string>();
        frame.physicsHash.valid =
            stateHashes.at("physics").at("valid").get<bool>();
        frame.vertexHash.hex =
            stateHashes.at("vertex").at("hash").get<std::string>();
        frame.vertexHash.valid =
            stateHashes.at("vertex").at("valid").get<bool>();

        for (const auto& entry : object.at("bodies"))
        {
            MmdPhysicsTraceBody body;
            body.index = entry.at("index").get<std::uint32_t>();
            if (!ParseMode(entry.at("mode").get<std::string>(), body.mode))
                return false;
            if (!ParseTransform(
                    entry.at("worldTransform"),
                    body.worldTransform) ||
                !ParseTransform(
                    entry.at("interpolationWorldTransform"),
                    body.interpolationWorldTransform) ||
                !ParseTransform(
                    entry.at("motionStateTransform"),
                    body.motionStateTransform))
            {
                return false;
            }
            body.motionStateAvailable =
                entry.at("motionStateAvailable").get<bool>();
            const auto& linear = entry.at("linearVelocity");
            const auto& angular = entry.at("angularVelocity");
            body.linearVelocity = glm::vec3(
                linear[0].get<float>(),
                linear[1].get<float>(),
                linear[2].get<float>()
            );
            body.angularVelocity = glm::vec3(
                angular[0].get<float>(),
                angular[1].get<float>(),
                angular[2].get<float>()
            );
            frame.bodies.push_back(std::move(body));
        }

        for (const auto& entry : object.at("bones"))
        {
            MmdPhysicsTraceBone bone;
            bone.index = entry.at("index").get<BoneIndex>();
            if (!ParseMatrix(entry.at("local"), bone.localMatrix) ||
                !ParseMatrix(entry.at("global"), bone.globalMatrix))
            {
                return false;
            }
            frame.bones.push_back(std::move(bone));
        }

        for (const auto& entry : object.at("joints"))
        {
            MmdPhysicsTraceJoint joint;
            joint.index = entry.at("index").get<std::uint32_t>();
            joint.rawLinearError =
                entry.at("rawLinearError").get<float>();
            joint.linearViolation =
                entry.at("linearViolation").get<float>();
            joint.rawAngularErrorDeg =
                entry.at("rawAngularErrorDeg").get<float>();
            joint.angularViolationDeg =
                entry.at("angularViolationDeg").get<float>();
            frame.joints.push_back(std::move(joint));
        }

        for (const auto& entry : object.at("contactPairs"))
        {
            MmdPhysicsTraceContactPair pair;
            pair.bodyA = entry.at("bodyA").get<std::uint32_t>();
            pair.bodyB = entry.at("bodyB").get<std::uint32_t>();
            pair.pointCount = entry.at("pointCount").get<int>();
            pair.maxPenetration =
                entry.at("maxPenetration").get<float>();
            pair.normalImpulse =
                entry.at("normalImpulse").get<float>();
            frame.contactPairs.push_back(std::move(pair));
        }

        for (const auto& entry : object.at("events"))
            frame.events.push_back(entry.get<std::string>());

        output = std::move(frame);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

TraceDiffResult DiffTraceStreams(
    std::istream& left,
    std::istream& right
)
{
    TraceDiffResult result;
    std::string lineA;
    std::string lineB;

    auto updateDivergence = [&result](
        MotionFrameIndex frame,
        std::uint32_t body,
        float positionError,
        float rotationErrorDeg)
    {
        if (!result.firstFound)
        {
            result.firstFound = true;
            result.firstFrame = frame;
            result.firstBody = body;
            result.firstPositionError = positionError;
            result.firstRotationErrorDeg = rotationErrorDeg;
        }
        if (!result.maxFound ||
            positionError > result.maxPositionError)
        {
            result.maxFound = true;
            result.maxFrame = frame;
            result.maxBody = body;
            result.maxPositionError = positionError;
            result.maxRotationErrorDeg = rotationErrorDeg;
        }
    };

    while (std::getline(left, lineA))
    {
        if (lineA.empty())
            continue;
        ++result.lineCountA;
        MmdPhysicsTraceFrame frameA;
        if (!ReadTraceFrameJson(lineA, frameA))
        {
            result.identical = false;
            break;
        }

        bool haveFrameB = false;
        MmdPhysicsTraceFrame frameB;
        while (std::getline(right, lineB))
        {
            if (lineB.empty())
                continue;
            ++result.lineCountB;
            if (ReadTraceFrameJson(lineB, frameB))
            {
                haveFrameB = true;
                break;
            }
            result.identical = false;
        }
        if (!haveFrameB)
        {
            result.identical = false;
            break;
        }
        if (frameA.frame != frameB.frame)
        {
            result.identical = false;
            break;
        }
        ++result.comparedFrames;

        if (frameA.poseHash.hex != frameB.poseHash.hex ||
            frameA.physicsHash.hex != frameB.physicsHash.hex ||
            frameA.vertexHash.hex != frameB.vertexHash.hex)
        {
            result.identical = false;
            result.hashMismatch = true;
        }

        for (const MmdPhysicsTraceBody& bodyA : frameA.bodies)
        {
            const MmdPhysicsTraceBody* bodyB =
                FindBody(frameB, bodyA.index);
            const float positionError =
                bodyB != nullptr ? PositionError(bodyA, *bodyB) : 1.0e30f;
            const float rotationErrorDeg =
                bodyB != nullptr ? RotationErrorDeg(bodyA, *bodyB) : 180.0f;
            if (positionError > Epsilon || rotationErrorDeg > Epsilon)
            {
                result.identical = false;
                updateDivergence(
                    frameA.frame,
                    bodyA.index,
                    positionError,
                    rotationErrorDeg
                );
            }
        }
        for (const MmdPhysicsTraceBody& bodyB : frameB.bodies)
        {
            if (FindBody(frameA, bodyB.index) == nullptr)
            {
                result.identical = false;
                updateDivergence(
                    frameA.frame,
                    bodyB.index,
                    1.0e30f,
                    180.0f
                );
            }
        }

        for (const MmdPhysicsTraceJoint& jointA : frameA.joints)
        {
            for (const MmdPhysicsTraceJoint& jointB : frameB.joints)
            {
                if (jointB.index != jointA.index)
                    continue;
                const float linearDelta =
                    std::abs(jointA.rawLinearError - jointB.rawLinearError);
                const float angularDelta = std::abs(
                    jointA.rawAngularErrorDeg -
                    jointB.rawAngularErrorDeg
                );
                if (linearDelta > Epsilon || angularDelta > Epsilon)
                {
                    result.identical = false;
                    if (!result.jointDeltaFound ||
                        linearDelta + angularDelta >
                            result.jointLinearDelta +
                                result.jointAngularDeltaDeg)
                    {
                        result.jointDeltaFound = true;
                        result.jointIndex = jointA.index;
                        result.jointLinearDelta = linearDelta;
                        result.jointAngularDeltaDeg = angularDelta;
                    }
                }
                break;
            }
        }
        // Joint presence must be symmetric: joints present in only one side
        // are divergences too (source joint index is the identity).
        for (const MmdPhysicsTraceJoint& jointA : frameA.joints)
        {
            bool foundInB = false;
            for (const MmdPhysicsTraceJoint& jointB : frameB.joints)
            {
                if (jointB.index == jointA.index)
                {
                    foundInB = true;
                    break;
                }
            }
            if (!foundInB)
            {
                result.identical = false;
                if (!result.jointDeltaFound)
                {
                    result.jointDeltaFound = true;
                    result.jointIndex = jointA.index;
                    result.jointLinearDelta = 1.0e30f;
                    result.jointAngularDeltaDeg = 1.0e30f;
                }
            }
        }
        for (const MmdPhysicsTraceJoint& jointB : frameB.joints)
        {
            bool foundInA = false;
            for (const MmdPhysicsTraceJoint& jointA : frameA.joints)
            {
                if (jointA.index == jointB.index)
                {
                    foundInA = true;
                    break;
                }
            }
            if (!foundInA)
            {
                result.identical = false;
                if (!result.jointDeltaFound)
                {
                    result.jointDeltaFound = true;
                    result.jointIndex = jointB.index;
                    result.jointLinearDelta = 1.0e30f;
                    result.jointAngularDeltaDeg = 1.0e30f;
                }
            }
        }

        // Contact-topology divergence: pairs are sorted by (bodyA, bodyB);
        // walk both sets and report the first pair present in one side only.
        if (!result.contactTopologyFound)
        {
            std::size_t indexA = 0U;
            std::size_t indexB = 0U;
            while (indexA < frameA.contactPairs.size() ||
                   indexB < frameB.contactPairs.size())
            {
                const bool endA = indexA >= frameA.contactPairs.size();
                const bool endB = indexB >= frameB.contactPairs.size();
                if (endB ||
                    (!endA &&
                     PairLess(
                         frameA.contactPairs[indexA],
                         frameB.contactPairs[indexB]
                     )))
                {
                    result.contactTopologyFound = true;
                    result.contactTopologyFrame = frameA.frame;
                    result.contactTopologyBodyA =
                        frameA.contactPairs[indexA].bodyA;
                    result.contactTopologyBodyB =
                        frameA.contactPairs[indexA].bodyB;
                    result.identical = false;
                    break;
                }
                if (endA ||
                    PairLess(
                        frameB.contactPairs[indexB],
                        frameA.contactPairs[indexA]
                    ))
                {
                    result.contactTopologyFound = true;
                    result.contactTopologyFrame = frameA.frame;
                    result.contactTopologyBodyA =
                        frameB.contactPairs[indexB].bodyA;
                    result.contactTopologyBodyB =
                        frameB.contactPairs[indexB].bodyB;
                    result.identical = false;
                    break;
                }
                ++indexA;
                ++indexB;
            }
        }

        // Motion-state divergence: only when both sides can authoritatively
        // read the motion-state transform.
        for (const MmdPhysicsTraceBody& bodyA : frameA.bodies)
        {
            const MmdPhysicsTraceBody* bodyB =
                FindBody(frameB, bodyA.index);
            if (bodyB == nullptr ||
                !bodyA.motionStateAvailable ||
                !bodyB->motionStateAvailable)
            {
                continue;
            }
            const float positionError = glm::length(
                bodyA.motionStateTransform.position -
                bodyB->motionStateTransform.position
            );
            const float rotationErrorDeg = RotationErrorDeg(
                bodyA.motionStateTransform,
                bodyB->motionStateTransform
            );
            if (positionError > Epsilon || rotationErrorDeg > Epsilon)
            {
                result.identical = false;
                if (!result.motionStateFound)
                {
                    result.motionStateFound = true;
                    result.motionStateFrame = frameA.frame;
                    result.motionStateBody = bodyA.index;
                    result.motionStatePositionError = positionError;
                }
            }
        }

        // Bone divergence: local/global matrix max delta by source bone
        // index.
        for (const MmdPhysicsTraceBone& boneA : frameA.bones)
        {
            const MmdPhysicsTraceBone* boneB =
                FindBone(frameB, boneA.index);
            float maxDelta = 0.0f;
            if (boneB != nullptr)
            {
                for (std::size_t element = 0U; element < 16U; ++element)
                {
                    maxDelta = std::max(
                        maxDelta,
                        std::abs(
                            boneA.localMatrix[element] -
                            boneB->localMatrix[element]
                        )
                    );
                    maxDelta = std::max(
                        maxDelta,
                        std::abs(
                            boneA.globalMatrix[element] -
                            boneB->globalMatrix[element]
                        )
                    );
                }
            }
            else
            {
                maxDelta = 1.0e30f;
            }
            if (maxDelta > Epsilon)
            {
                result.identical = false;
                if (!result.boneFound)
                {
                    result.boneFound = true;
                    result.boneFrame = frameA.frame;
                    result.boneIndex = boneA.index;
                    result.boneMaxMatrixDelta = maxDelta;
                }
            }
        }
        // Bone presence must be symmetric: bones present only in B are
        // divergences too (source bone index is the identity).
        for (const MmdPhysicsTraceBone& boneB : frameB.bones)
        {
            if (FindBone(frameA, boneB.index) == nullptr)
            {
                result.identical = false;
                if (!result.boneFound)
                {
                    result.boneFound = true;
                    result.boneFrame = frameA.frame;
                    result.boneIndex = boneB.index;
                    result.boneMaxMatrixDelta = 1.0e30f;
                }
            }
        }
    }

    while (std::getline(right, lineB))
    {
        if (!lineB.empty())
            ++result.lineCountB;
    }
    if (result.lineCountA != result.lineCountB)
        result.identical = false;
    return result;
}

std::string FormatTraceDiff(const TraceDiffResult& result)
{
    std::string text;
    text.reserve(256);
    if (!result.profileA.empty() || !result.profileB.empty())
    {
        text += "Profile A / Profile B\n";
    }
    if (result.identical)
    {
        text += "Identical: " + std::to_string(result.comparedFrames) +
            " frames\n";
        return text;
    }
    if (result.hashMismatch && !result.firstFound)
        text += "Hash mismatch at frame comparison\n";
    if (result.firstFound)
    {
        text += "First divergence: frame=" +
            std::to_string(result.firstFrame) +
            " body=" + std::to_string(result.firstBody) +
            " positionError=" + std::to_string(result.firstPositionError) +
            " rotationErrorDeg=" +
            std::to_string(result.firstRotationErrorDeg) + "\n";
    }
    if (result.maxFound)
    {
        text += "Maximum divergence: frame=" +
            std::to_string(result.maxFrame) +
            " body=" + std::to_string(result.maxBody) +
            " positionError=" + std::to_string(result.maxPositionError) +
            " rotationErrorDeg=" +
            std::to_string(result.maxRotationErrorDeg) + "\n";
    }
    if (result.contactTopologyFound)
    {
        text += "First contact-topology divergence: frame=" +
            std::to_string(result.contactTopologyFrame) +
            " pair=(" +
            std::to_string(result.contactTopologyBodyA) + "," +
            std::to_string(result.contactTopologyBodyB) + ")\n";
    }
    if (result.motionStateFound)
    {
        text += "First motion-state divergence: frame=" +
            std::to_string(result.motionStateFrame) +
            " body=" + std::to_string(result.motionStateBody) +
            " positionError=" +
            std::to_string(result.motionStatePositionError) + "\n";
    }
    if (result.boneFound)
    {
        text += "First bone divergence: frame=" +
            std::to_string(result.boneFrame) +
            " bone=" + std::to_string(result.boneIndex) +
            " maxMatrixDelta=" +
            std::to_string(result.boneMaxMatrixDelta) + "\n";
    }
    if (result.jointDeltaFound)
    {
        text += "Joint error delta: joint=" +
            std::to_string(result.jointIndex) +
            " linear=" + std::to_string(result.jointLinearDelta) +
            " angular=" + std::to_string(result.jointAngularDeltaDeg) + "\n";
    }
    text += "comparedFrames=" + std::to_string(result.comparedFrames) +
        " linesA=" + std::to_string(result.lineCountA) +
        " linesB=" + std::to_string(result.lineCountB) + "\n";
    return text;
}
}  // namespace wisteria::trace
