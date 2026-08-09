#pragma once

#include "wisteria/runtime/checkpoint.hpp"
#include "wisteria/runtime/generic_checkpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wisteria
{
// R1.4 Phase 0A: WISTERIA Checkpoint Envelope + MMD R1.2C payload v1 wire
// codec (contract §2C). Binary, little-endian, IEEE-754 raw bits; JSON is
// not a canonical checkpoint format. Portable bytes != portable
// deterministic semantics: build compatibility is verified before any
// mutation.

inline constexpr std::uint32_t CheckpointWireVersion = 1U;
inline constexpr std::uint32_t CheckpointPayloadKindMmdR12C = 1U;
inline constexpr std::uint32_t CheckpointPayloadSchemaMmdR12C = 1U;
inline constexpr std::uint32_t CheckpointBackendIdSabaMmd = 1U;
inline constexpr std::uint32_t CheckpointDeterministicProfileColdStepV1 = 1U;

// R1.8 Generic R1.8 payload (same R1.4 envelope, new kind).
inline constexpr std::uint32_t CheckpointPayloadKindGenericR18 = 2U;
inline constexpr std::uint32_t CheckpointPayloadSchemaGenericR18 = 1U;
inline constexpr std::uint32_t CheckpointBackendIdWisteriaGeneric = 2U;
inline constexpr std::uint32_t CheckpointDeterministicProfileGenericV1 = 2U;

inline constexpr std::uint64_t CheckpointWireHeaderSize = 48U;

// Engine-owned build/runtime compatibility identity (contract §2C).
// These revisions are frozen per deterministic release; changing any of
// them invalidates every previously serialized checkpoint. Production
// serializers write CurrentBuildCompatibilityId() and deserializers compare
// against it; callers cannot substitute their own identity.
// R1.2C integrity revision: deterministic Cold Boundary execution changed
// (step-start world canonicalization, canonical broadphase, pool reset), so
// old checkpoints are NOT compatible with this build.
inline constexpr std::uint64_t DeterministicCompatibilityRevision = 2U;
inline constexpr std::uint64_t SabaCompatibilityRevision = 2U;
inline constexpr std::uint64_t BulletCompatibilityRevision = 2U;
inline constexpr std::uint64_t CheckpointPayloadImplementationRevision = 1U;

// Compile-time build platform class: OS + architecture + compiler family +
// compiler version folded with FNV-1a64. Windows/MSVC and Linux/GCC builds
// therefore receive different classes until cross-platform exact
// determinism is proven (contract §2C: portable bytes != portable
// deterministic semantics). Big-endian or non-64-bit toolchains are also
// distinguished through their architecture tag.
inline constexpr std::uint64_t BuildPlatformCompatibilityClass()
{
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t state = kOffsetBasis;

#if defined(_WIN32)
    constexpr const char* kOs = "windows";
#elif defined(__linux__)
    constexpr const char* kOs = "linux";
#elif defined(__APPLE__)
    constexpr const char* kOs = "apple";
#else
    constexpr const char* kOs = "unknown-os";
#endif

#if defined(_M_X64) || defined(__x86_64__)
    constexpr const char* kArch = "x86-64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    constexpr const char* kArch = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    constexpr const char* kArch = "x86";
#else
    constexpr const char* kArch = "unknown-arch";
#endif

#if defined(_MSC_VER)
    constexpr const char* kCompiler = "msvc";
#elif defined(__clang__)
    constexpr const char* kCompiler = "clang";
#elif defined(__GNUC__)
    constexpr const char* kCompiler = "gcc";
#else
    constexpr const char* kCompiler = "unknown-compiler";
#endif

#if defined(_MSC_FULL_VER)
    constexpr std::uint64_t kCompilerVersion = _MSC_FULL_VER;
#elif defined(_MSC_VER)
    constexpr std::uint64_t kCompilerVersion = _MSC_VER;
#elif defined(__clang__)
    constexpr std::uint64_t kCompilerVersion =
        static_cast<std::uint64_t>(__clang_major__) * 10000U +
        static_cast<std::uint64_t>(__clang_minor__) * 100U +
        static_cast<std::uint64_t>(__clang_patchlevel__);
#elif defined(__GNUC__)
    constexpr std::uint64_t kCompilerVersion =
        static_cast<std::uint64_t>(__GNUC__) * 10000U +
        static_cast<std::uint64_t>(__GNUC_MINOR__) * 100U +
        static_cast<std::uint64_t>(__GNUC_PATCHLEVEL__);
#else
    constexpr std::uint64_t kCompilerVersion = 0U;
#endif

    const auto fold = [&state, kPrime](const char* text)
    {
        while (*text != '\0')
        {
            state ^= static_cast<std::uint8_t>(*text);
            state *= kPrime;
            ++text;
        }
    };
    const auto foldU64 = [&state, kPrime](std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
        {
            state ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
            state *= kPrime;
        }
    };
    fold("os:"); fold(kOs);
    fold("arch:"); fold(kArch);
    fold("compiler:"); fold(kCompiler);
    foldU64(kCompilerVersion);
    return state;
}

inline constexpr std::uint64_t CurrentBuildCompatibilityId()
{
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t state = kOffsetBasis;
    const std::uint64_t parts[] = {
        DeterministicCompatibilityRevision,
        SabaCompatibilityRevision,
        BulletCompatibilityRevision,
        CheckpointPayloadImplementationRevision,
        BuildPlatformCompatibilityClass()
    };
    for (const std::uint64_t part : parts)
    {
        for (int shift = 0; shift < 64; shift += 8)
        {
            state ^= static_cast<std::uint8_t>((part >> shift) & 0xFFU);
            state *= kPrime;
        }
    }
    return state;
}

static_assert(
    CurrentBuildCompatibilityId() != 0U,
    "build compatibility identity must be non-zero"
);
static_assert(
    BuildPlatformCompatibilityClass() != 0U,
    "build platform compatibility class must be non-zero"
);

struct CheckpointSerializationOptions
{
    // Test/internal-only build identity override. Production callers must
    // leave this empty; the codec then writes and compares the engine-owned
    // CurrentBuildCompatibilityId(). An override of 0 is invalid.
    std::optional<std::uint64_t> buildCompatibilityIdOverride;

    // Untrusted-input guards (contract §2C): never allocate beyond these.
    std::uint64_t maxPayloadBytes = 256U * 1024U * 1024U;
    std::uint64_t maxMorphOverrideCount = 1U << 20;
    std::uint64_t maxIkOverrideCount = 1U << 20;
    std::uint64_t maxRigidBodyCount = 1U << 20;
    std::uint64_t maxStringBytes = 1U << 20;
};

// Serializes a checkpoint into the wire format, writing the engine-owned
// CurrentBuildCompatibilityId() (or the test-only override). A zero build
// identity is invalid and throws std::invalid_argument.
std::vector<std::uint8_t> SerializeCheckpoint(
    const FrameCheckpoint& checkpoint,
    const CheckpointSerializationOptions& options = {}
);

// Decodes untrusted bytes. Returns TimelineStatus::Ok and fills output on
// success; output is left unchanged on failure. Structural duplicate-field
// consistency is verified here; full semantic validation happens when the
// decoded checkpoint is passed to the runtime restore path.
TimelineStatus DeserializeCheckpoint(
    const std::uint8_t* bytes,
    std::size_t size,
    const CheckpointSerializationOptions& options,
    FrameCheckpoint& output
);

// R1.9 Final Fix: engine-owned envelope-header probe. The stable ABI must
// not parse raw header offsets itself; malformed envelopes are Invalid and
// valid envelopes with unknown future payload kinds are UnknownPayloadKind
// (so the ABI can answer UNSUPPORTED instead of guessing).
enum class CheckpointEnvelopeProbe : std::uint8_t
{
    Invalid,
    MmdR12C,
    GenericR18,
    UnknownPayloadKind
};

CheckpointEnvelopeProbe ProbeCheckpointEnvelope(
    const std::uint8_t* bytes,
    std::size_t size
) noexcept;

// R1.8: Generic payload kind 2 codec. Reuses the R1.4 wire envelope
// (magic / version / kind / schema / backend / profile / build identity /
// size / checksum); only the payload body is backend-specific.
std::vector<std::uint8_t> SerializeGenericCheckpoint(
    const GenericRuntimeCheckpoint& checkpoint,
    const CheckpointSerializationOptions& options = {}
);

TimelineStatus DeserializeGenericCheckpoint(
    const std::uint8_t* bytes,
    std::size_t size,
    const CheckpointSerializationOptions& options,
    GenericRuntimeCheckpoint& output
);
}  // namespace wisteria
