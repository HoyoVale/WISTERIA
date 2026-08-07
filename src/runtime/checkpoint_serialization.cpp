#include "wisteria/runtime/checkpoint_serialization.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace wisteria
{
namespace
{
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void FnvByte(std::uint64_t& state, std::uint8_t byte) noexcept
{
    state ^= byte;
    state *= kFnvPrime;
}

std::uint64_t FnvBytes(
    const std::uint8_t* data,
    std::size_t size
) noexcept
{
    std::uint64_t state = kFnvOffsetBasis;
    for (std::size_t index = 0U; index < size; ++index)
    {
        FnvByte(state, data[index]);
    }
    return state;
}

bool IsFinite(float value) noexcept
{
    return std::isfinite(value) != 0;
}

class Writer
{
public:
    void U8(std::uint8_t value)
    {
        bytes.push_back(value);
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }

    void I32(std::int32_t value)
    {
        U32(static_cast<std::uint32_t>(value));
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }

    void F32(float value)
    {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    void Bool(bool value)
    {
        U8(value ? 1U : 0U);
    }

    void String(std::string_view value)
    {
        U32(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void Raw(const std::uint8_t* data, std::size_t size)
    {
        bytes.insert(bytes.end(), data, data + size);
    }

    std::vector<std::uint8_t>& Buffer() noexcept
    {
        return bytes;
    }

private:
    std::vector<std::uint8_t> bytes;
};

class Reader
{
public:
    Reader(const std::uint8_t* data, std::size_t size) noexcept
        : data(data), size(size)
    {
    }

    bool Ok() const noexcept
    {
        return !failed;
    }

    bool ReadU8(std::uint8_t& value) noexcept
    {
        if (pos + 1U > size)
        {
            failed = true;
            return false;
        }
        value = data[pos];
        ++pos;
        return true;
    }

    bool ReadU32(std::uint32_t& value) noexcept
    {
        if (pos + 4U > size)
        {
            failed = true;
            return false;
        }
        value = 0U;
        for (int shift = 0; shift < 32; shift += 8)
        {
            value |= static_cast<std::uint32_t>(data[pos]) << shift;
            ++pos;
        }
        return true;
    }

    bool ReadI32(std::int32_t& value) noexcept
    {
        std::uint32_t bits = 0U;
        if (!ReadU32(bits))
            return false;
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    bool ReadU64(std::uint64_t& value) noexcept
    {
        if (pos + 8U > size)
        {
            failed = true;
            return false;
        }
        value = 0U;
        for (int shift = 0; shift < 64; shift += 8)
        {
            value |= static_cast<std::uint64_t>(data[pos]) << shift;
            ++pos;
        }
        return true;
    }

    bool ReadF32(float& value) noexcept
    {
        std::uint32_t bits = 0U;
        if (!ReadU32(bits))
            return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool ReadBool(bool& value) noexcept
    {
        std::uint8_t byte = 0U;
        if (!ReadU8(byte))
            return false;
        if (byte > 1U)
        {
            failed = true;
            return false;
        }
        value = byte != 0U;
        return true;
    }

    bool ReadBytes(std::uint8_t* output, std::size_t count) noexcept
    {
        if (count > size - pos)
        {
            failed = true;
            return false;
        }
        if (output != nullptr && count > 0U)
        {
            std::memcpy(output, data + pos, count);
        }
        pos += count;
        return true;
    }

    std::size_t Remaining() const noexcept
    {
        return size - pos;
    }

private:
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0U;
    bool failed = false;
};

constexpr std::uint8_t kWireMagic[4] = {'W', 'C', 'P', 'K'};

constexpr std::size_t kMinMorphEntryBytes = 8U;   // len(4) + weight(4)
constexpr std::size_t kMinIkEntryBytes = 5U;      // len(4) + enabled(1)
// Encoded RigidBodySnapshot size: index(4) + mode(1) + mass(4) +
// 2 * transform(position 12 + basis 36) + 6 * vec3(12) +
// activationState(4) + deactivationTime(4) == 185 bytes. The rotation
// basis is the frozen 9 explicit float32 (column-major 3x3), not 4.
constexpr std::size_t kMinBodyBytes = 185U;

void EncodeTransform(Writer& writer, const RigidTransformSnapshot& transform)
{
    writer.F32(transform.position.x);
    writer.F32(transform.position.y);
    writer.F32(transform.position.z);
    for (const float component : transform.rotationBasis)
        writer.F32(component);
}

bool DecodeTransform(Reader& reader, RigidTransformSnapshot& transform)
{
    if (!(reader.ReadF32(transform.position.x) &&
          reader.ReadF32(transform.position.y) &&
          reader.ReadF32(transform.position.z)))
    {
        return false;
    }
    // Basis must go through the little-endian codec like every other float;
    // memcpy-ing wire bytes into host floats would break on big-endian.
    for (float& component : transform.rotationBasis)
    {
        if (!reader.ReadF32(component))
            return false;
    }
    return true;
}

void EncodeVector(Writer& writer, const glm::vec3& value)
{
    writer.F32(value.x);
    writer.F32(value.y);
    writer.F32(value.z);
}

bool DecodeVector(Reader& reader, glm::vec3& value)
{
    return reader.ReadF32(value.x) &&
        reader.ReadF32(value.y) &&
        reader.ReadF32(value.z);
}

void EncodeReplayConfig(Writer& writer, const ReplayConfig& config)
{
    writer.U32(config.motionFps);
    writer.U32(config.physicsHz);
    writer.U32(config.warmupFrames);
    writer.Bool(config.loopMotion);
}

bool DecodeReplayConfig(Reader& reader, ReplayConfig& config)
{
    return reader.ReadU32(config.motionFps) &&
        reader.ReadU32(config.physicsHz) &&
        reader.ReadU32(config.warmupFrames) &&
        reader.ReadBool(config.loopMotion);
}

bool ReplayConfigsEqual(
    const ReplayConfig& left,
    const ReplayConfig& right
) noexcept
{
    return left.motionFps == right.motionFps &&
        left.physicsHz == right.physicsHz &&
        left.warmupFrames == right.warmupFrames &&
        left.loopMotion == right.loopMotion;
}

bool AssetIdentitiesEqual(
    const AssetIdentity& left,
    const AssetIdentity& right
) noexcept
{
    return left.pmxFileHash == right.pmxFileHash &&
        left.vmdFileHash == right.vmdFileHash &&
        left.hasMotion == right.hasMotion &&
        left.layoutFingerprint == right.layoutFingerprint &&
        left.physicsConfigurationFingerprint ==
            right.physicsConfigurationFingerprint;
}

bool OverridesEqual(
    const UserOverrideState& left,
    const UserOverrideState& right
) noexcept
{
    if (left.morphOverrides.size() != right.morphOverrides.size() ||
        left.ikOverrides.size() != right.ikOverrides.size() ||
        left.physicsEnabled != right.physicsEnabled ||
        left.loopMotion != right.loopMotion)
    {
        return false;
    }
    for (std::size_t index = 0U; index < left.morphOverrides.size(); ++index)
    {
        if (left.morphOverrides[index].first !=
                right.morphOverrides[index].first ||
            left.morphOverrides[index].second !=
                right.morphOverrides[index].second)
        {
            return false;
        }
    }
    for (std::size_t index = 0U; index < left.ikOverrides.size(); ++index)
    {
        if (left.ikOverrides[index].first !=
                right.ikOverrides[index].first ||
            left.ikOverrides[index].second !=
                right.ikOverrides[index].second)
        {
            return false;
        }
    }
    return true;
}

void EncodeOverrides(
    Writer& writer,
    const UserOverrideState& overrides
)
{
    writer.Bool(overrides.physicsEnabled);
    writer.Bool(overrides.loopMotion);
    writer.U32(static_cast<std::uint32_t>(overrides.morphOverrides.size()));
    for (const auto& [name, weight] : overrides.morphOverrides)
    {
        writer.String(name);
        writer.F32(weight);
    }
    writer.U32(static_cast<std::uint32_t>(overrides.ikOverrides.size()));
    for (const auto& [name, enabled] : overrides.ikOverrides)
    {
        writer.String(name);
        writer.Bool(enabled);
    }
}

bool DecodeOverrides(
    Reader& reader,
    const CheckpointSerializationOptions& options,
    UserOverrideState& overrides
)
{
    if (!reader.ReadBool(overrides.physicsEnabled) ||
        !reader.ReadBool(overrides.loopMotion))
    {
        return false;
    }
    std::uint32_t morphCount = 0U;
    if (!reader.ReadU32(morphCount) ||
        static_cast<std::uint64_t>(morphCount) > options.maxMorphOverrideCount ||
        static_cast<std::uint64_t>(morphCount) * kMinMorphEntryBytes >
            reader.Remaining())
    {
        return false;
    }
    overrides.morphOverrides.reserve(morphCount);
    for (std::uint32_t index = 0U; index < morphCount; ++index)
    {
        std::uint32_t length = 0U;
        if (!reader.ReadU32(length) ||
            static_cast<std::uint64_t>(length) > options.maxStringBytes ||
            length > reader.Remaining())
        {
            return false;
        }
        std::string name(length, '\0');
        if (!reader.ReadBytes(
                reinterpret_cast<std::uint8_t*>(name.data()),
                length))
        {
            return false;
        }
        float weight = 0.0f;
        if (!reader.ReadF32(weight) || !IsFinite(weight))
            return false;
        overrides.morphOverrides.emplace_back(
            std::move(name),
            weight
        );
    }
    std::uint32_t ikCount = 0U;
    if (!reader.ReadU32(ikCount) ||
        static_cast<std::uint64_t>(ikCount) > options.maxIkOverrideCount ||
        static_cast<std::uint64_t>(ikCount) * kMinIkEntryBytes >
            reader.Remaining())
    {
        return false;
    }
    overrides.ikOverrides.reserve(ikCount);
    for (std::uint32_t index = 0U; index < ikCount; ++index)
    {
        std::uint32_t length = 0U;
        if (!reader.ReadU32(length) ||
            static_cast<std::uint64_t>(length) > options.maxStringBytes ||
            length > reader.Remaining())
        {
            return false;
        }
        std::string name(length, '\0');
        if (!reader.ReadBytes(
                reinterpret_cast<std::uint8_t*>(name.data()),
                length))
        {
            return false;
        }
        bool enabled = false;
        if (!reader.ReadBool(enabled))
            return false;
        overrides.ikOverrides.emplace_back(std::move(name), enabled);
    }
    return true;
}

void EncodeAssetIdentity(
    Writer& writer,
    const AssetIdentity& identity
)
{
    writer.U64(identity.pmxFileHash);
    writer.U64(identity.vmdFileHash);
    writer.Bool(identity.hasMotion);
    writer.U64(identity.layoutFingerprint);
    writer.U64(identity.physicsConfigurationFingerprint);
}

bool DecodeAssetIdentity(
    Reader& reader,
    AssetIdentity& identity
)
{
    return reader.ReadU64(identity.pmxFileHash) &&
        reader.ReadU64(identity.vmdFileHash) &&
        reader.ReadBool(identity.hasMotion) &&
        reader.ReadU64(identity.layoutFingerprint) &&
        reader.ReadU64(identity.physicsConfigurationFingerprint);
}

void EncodeHashes(Writer& writer, const DeterminismHashes& hashes)
{
    writer.U64(hashes.exactHash);
    writer.U64(hashes.canonicalHash);
    writer.Bool(hashes.valid);
}

bool DecodeHashes(Reader& reader, DeterminismHashes& hashes)
{
    return reader.ReadU64(hashes.exactHash) &&
        reader.ReadU64(hashes.canonicalHash) &&
        reader.ReadBool(hashes.valid);
}

void EncodeFingerprint(
    Writer& writer,
    const DeterminismFingerprint& fingerprint
)
{
    writer.U32(fingerprint.schemaVersion);
    writer.U64(fingerprint.frame);
    EncodeAssetIdentity(writer, fingerprint.asset);
    EncodeReplayConfig(writer, fingerprint.config);
    EncodeOverrides(writer, fingerprint.overrides);
    EncodeHashes(writer, fingerprint.state.pose);
    EncodeHashes(writer, fingerprint.state.physics);
    EncodeHashes(writer, fingerprint.state.vertex);
}

bool DecodeFingerprint(
    Reader& reader,
    const CheckpointSerializationOptions& options,
    DeterminismFingerprint& fingerprint
)
{
    return reader.ReadU32(fingerprint.schemaVersion) &&
        reader.ReadU64(fingerprint.frame) &&
        DecodeAssetIdentity(reader, fingerprint.asset) &&
        DecodeReplayConfig(reader, fingerprint.config) &&
        DecodeOverrides(reader, options, fingerprint.overrides) &&
        DecodeHashes(reader, fingerprint.state.pose) &&
        DecodeHashes(reader, fingerprint.state.physics) &&
        DecodeHashes(reader, fingerprint.state.vertex);
}

void EncodeBody(Writer& writer, const RigidBodySnapshot& body)
{
    writer.U32(body.index);
    writer.U8(static_cast<std::uint8_t>(body.mode));
    writer.F32(body.definitionMass);
    EncodeTransform(writer, body.worldTransform);
    EncodeTransform(writer, body.interpolationTransform);
    EncodeVector(writer, body.linearVelocity);
    EncodeVector(writer, body.angularVelocity);
    EncodeVector(writer, body.interpolationLinearVelocity);
    EncodeVector(writer, body.interpolationAngularVelocity);
    EncodeVector(writer, body.totalForce);
    EncodeVector(writer, body.totalTorque);
    writer.I32(body.activationState);
    writer.F32(body.deactivationTime);
}

bool DecodeBody(Reader& reader, RigidBodySnapshot& body)
{
    std::uint8_t mode = 0U;
    if (!reader.ReadU32(body.index) ||
        !reader.ReadU8(mode) ||
        mode > static_cast<std::uint8_t>(PmxRigidBodyMode::PhysicsWithBone) ||
        !reader.ReadF32(body.definitionMass) ||
        !DecodeTransform(reader, body.worldTransform) ||
        !DecodeTransform(reader, body.interpolationTransform) ||
        !DecodeVector(reader, body.linearVelocity) ||
        !DecodeVector(reader, body.angularVelocity) ||
        !DecodeVector(reader, body.interpolationLinearVelocity) ||
        !DecodeVector(reader, body.interpolationAngularVelocity) ||
        !DecodeVector(reader, body.totalForce) ||
        !DecodeVector(reader, body.totalTorque) ||
        !reader.ReadI32(body.activationState) ||
        !reader.ReadF32(body.deactivationTime))
    {
        return false;
    }
    body.mode = static_cast<PmxRigidBodyMode>(mode);
    const float* floats[] = {
        &body.definitionMass,
        &body.linearVelocity.x,
        &body.linearVelocity.y,
        &body.linearVelocity.z,
        &body.angularVelocity.x,
        &body.angularVelocity.y,
        &body.angularVelocity.z,
        &body.interpolationLinearVelocity.x,
        &body.interpolationLinearVelocity.y,
        &body.interpolationLinearVelocity.z,
        &body.interpolationAngularVelocity.x,
        &body.interpolationAngularVelocity.y,
        &body.interpolationAngularVelocity.z,
        &body.totalForce.x,
        &body.totalForce.y,
        &body.totalForce.z,
        &body.totalTorque.x,
        &body.totalTorque.y,
        &body.totalTorque.z,
        &body.deactivationTime
    };
    for (const float* value : floats)
    {
        if (!IsFinite(*value))
            return false;
    }
    for (const float component : body.worldTransform.rotationBasis)
    {
        if (!IsFinite(component))
            return false;
    }
    for (const float component : body.interpolationTransform.rotationBasis)
    {
        if (!IsFinite(component))
            return false;
    }
    return true;
}

void EncodePayload(Writer& writer, const FrameCheckpoint& checkpoint)
{
    writer.U32(CheckpointPayloadSchemaMmdR12C);
    writer.U64(checkpoint.frame);
    writer.U64(checkpoint.physics.physicsTick);
    EncodeReplayConfig(writer, checkpoint.config);
    EncodeOverrides(writer, checkpoint.overrides);
    EncodeAssetIdentity(writer, checkpoint.fingerprint.asset);
    EncodeFingerprint(writer, checkpoint.fingerprint);

    const PhysicsSnapshot& physics = checkpoint.physics;
    writer.U32(physics.schemaVersion);
    writer.U64(physics.layoutFingerprint);
    writer.U64(physics.physicsConfigurationFingerprint);
    writer.U64(physics.motionFrame);
    writer.U64(physics.physicsTick);
    writer.U32(physics.jointCount);
    writer.Bool(physics.canonical);
    writer.U32(static_cast<std::uint32_t>(physics.rigidBodies.size()));
    for (const RigidBodySnapshot& body : physics.rigidBodies)
        EncodeBody(writer, body);
}

bool DecodePayload(
    Reader& reader,
    const CheckpointSerializationOptions& options,
    FrameCheckpoint& checkpoint
)
{
    std::uint32_t payloadSchema = 0U;
    std::uint64_t topLevelTick = 0U;
    AssetIdentity topLevelAsset;
    if (!reader.ReadU32(payloadSchema) ||
        payloadSchema != CheckpointPayloadSchemaMmdR12C ||
        !reader.ReadU64(checkpoint.frame) ||
        !reader.ReadU64(topLevelTick) ||
        !DecodeReplayConfig(reader, checkpoint.config) ||
        !DecodeOverrides(reader, options, checkpoint.overrides) ||
        !DecodeAssetIdentity(reader, topLevelAsset) ||
        !DecodeFingerprint(reader, options, checkpoint.fingerprint))
    {
        return false;
    }

    PhysicsSnapshot& physics = checkpoint.physics;
    if (!reader.ReadU32(physics.schemaVersion) ||
        !reader.ReadU64(physics.layoutFingerprint) ||
        !reader.ReadU64(physics.physicsConfigurationFingerprint) ||
        !reader.ReadU64(physics.motionFrame) ||
        !reader.ReadU64(physics.physicsTick) ||
        !reader.ReadU32(physics.jointCount) ||
        !reader.ReadBool(physics.canonical))
    {
        return false;
    }
    if (topLevelTick != physics.physicsTick ||
        !AssetIdentitiesEqual(
            topLevelAsset,
            checkpoint.fingerprint.asset
        ))
    {
        return false;
    }
    std::uint32_t bodyCount = 0U;
    if (!reader.ReadU32(bodyCount) ||
        static_cast<std::uint64_t>(bodyCount) > options.maxRigidBodyCount ||
        static_cast<std::uint64_t>(bodyCount) * kMinBodyBytes >
            reader.Remaining())
    {
        return false;
    }
    physics.rigidBodies.reserve(bodyCount);
    for (std::uint32_t index = 0U; index < bodyCount; ++index)
    {
        RigidBodySnapshot body;
        if (!DecodeBody(reader, body))
            return false;
        physics.rigidBodies.push_back(std::move(body));
    }
    if (reader.Remaining() != 0U)
        return false;
    return true;
}

bool CheckpointStructurallyConsistent(
    const FrameCheckpoint& checkpoint
) noexcept
{
    if (checkpoint.frame != checkpoint.fingerprint.frame ||
        checkpoint.frame != checkpoint.physics.motionFrame ||
        checkpoint.frame >
            std::numeric_limits<MotionFrameIndex>::max() / 4U ||
        checkpoint.physics.physicsTick != checkpoint.frame * 4U ||
        !checkpoint.physics.canonical ||
        checkpoint.physics.schemaVersion != 2U ||
        checkpoint.fingerprint.schemaVersion != 1U ||
        !ReplayConfigsEqual(
            checkpoint.config,
            checkpoint.fingerprint.config
        ) ||
        !OverridesEqual(
            checkpoint.overrides,
            checkpoint.fingerprint.overrides
        ) ||
        checkpoint.fingerprint.asset.layoutFingerprint !=
            checkpoint.physics.layoutFingerprint ||
        checkpoint.fingerprint.asset.physicsConfigurationFingerprint !=
            checkpoint.physics.physicsConfigurationFingerprint ||
        !checkpoint.fingerprint.state.pose.valid ||
        !checkpoint.fingerprint.state.physics.valid ||
        !checkpoint.fingerprint.state.vertex.valid)
    {
        return false;
    }
    return true;
}
}  // namespace

std::vector<std::uint8_t> SerializeCheckpoint(
    const FrameCheckpoint& checkpoint,
    const CheckpointSerializationOptions& options
)
{
    const std::uint64_t buildCompatibilityId =
        options.buildCompatibilityIdOverride.value_or(
            CurrentBuildCompatibilityId()
        );
    if (buildCompatibilityId == 0U)
    {
        throw std::invalid_argument(
            "buildCompatibilityId must be non-zero"
        );
    }
    Writer writer;
    writer.Raw(kWireMagic, sizeof(kWireMagic));
    writer.U32(CheckpointWireVersion);
    writer.U32(CheckpointPayloadKindMmdR12C);
    writer.U32(CheckpointPayloadSchemaMmdR12C);
    writer.U32(CheckpointBackendIdSabaMmd);
    writer.U32(CheckpointDeterministicProfileColdStepV1);
    writer.U64(buildCompatibilityId);
    const std::size_t payloadSizeOffset = writer.Buffer().size();
    writer.U64(0U);  // payload size placeholder
    const std::size_t checksumOffset = writer.Buffer().size();
    writer.U64(0U);  // checksum placeholder

    const std::size_t payloadOffset = writer.Buffer().size();
    EncodePayload(writer, checkpoint);
    const std::uint64_t payloadSize =
        static_cast<std::uint64_t>(writer.Buffer().size() - payloadOffset);

    std::vector<std::uint8_t>& buffer = writer.Buffer();
    for (int shift = 0; shift < 64; shift += 8)
    {
        buffer[payloadSizeOffset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((payloadSize >> shift) & 0xFFU);
    }
    std::fill(
        buffer.begin() + static_cast<std::ptrdiff_t>(checksumOffset),
        buffer.begin() + static_cast<std::ptrdiff_t>(checksumOffset + 8U),
        0U
    );
    const std::uint64_t checksum =
        FnvBytes(buffer.data(), buffer.size());
    for (int shift = 0; shift < 64; shift += 8)
    {
        buffer[checksumOffset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((checksum >> shift) & 0xFFU);
    }
    return buffer;
}

TimelineStatus DeserializeCheckpoint(
    const std::uint8_t* bytes,
    std::size_t size,
    const CheckpointSerializationOptions& options,
    FrameCheckpoint& output
)
{
    if ((bytes == nullptr && size != 0U) ||
        size < CheckpointWireHeaderSize)
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    Reader reader(bytes, size);

    std::uint8_t magic[4] = {0, 0, 0, 0};
    std::uint32_t wireVersion = 0U;
    std::uint32_t payloadKind = 0U;
    std::uint32_t payloadSchema = 0U;
    std::uint32_t backendId = 0U;
    std::uint32_t profileId = 0U;
    std::uint64_t buildId = 0U;
    std::uint64_t payloadSize = 0U;
    std::uint64_t checksum = 0U;
    if (!reader.ReadBytes(magic, sizeof(magic)) ||
        !reader.ReadU32(wireVersion) ||
        !reader.ReadU32(payloadKind) ||
        !reader.ReadU32(payloadSchema) ||
        !reader.ReadU32(backendId) ||
        !reader.ReadU32(profileId) ||
        !reader.ReadU64(buildId) ||
        !reader.ReadU64(payloadSize) ||
        !reader.ReadU64(checksum))
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (std::memcmp(magic, kWireMagic, sizeof(kWireMagic)) != 0 ||
        wireVersion != CheckpointWireVersion ||
        payloadKind != CheckpointPayloadKindMmdR12C ||
        payloadSchema != CheckpointPayloadSchemaMmdR12C ||
        backendId != CheckpointBackendIdSabaMmd ||
        profileId != CheckpointDeterministicProfileColdStepV1)
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    const std::uint64_t expectedBuildId =
        options.buildCompatibilityIdOverride.value_or(
            CurrentBuildCompatibilityId()
        );
    if (buildId != expectedBuildId)
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (payloadSize > options.maxPayloadBytes ||
        payloadSize != size - CheckpointWireHeaderSize)
    {
        return TimelineStatus::InvalidCheckpoint;
    }

    // Verify FNV-1a64 over the whole buffer with the checksum field zeroed.
    std::vector<std::uint8_t> checksumBuffer(bytes, bytes + size);
    std::fill(
        checksumBuffer.begin() +
            static_cast<std::ptrdiff_t>(CheckpointWireHeaderSize - 8U),
        checksumBuffer.begin() +
            static_cast<std::ptrdiff_t>(CheckpointWireHeaderSize),
        0U
    );
    if (FnvBytes(checksumBuffer.data(), checksumBuffer.size()) != checksum)
    {
        return TimelineStatus::InvalidCheckpoint;
    }

    FrameCheckpoint decoded;
    if (!DecodePayload(reader, options, decoded) ||
        !reader.Ok() ||
        !CheckpointStructurallyConsistent(decoded))
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    output = std::move(decoded);
    return TimelineStatus::Ok;
}
}  // namespace wisteria
