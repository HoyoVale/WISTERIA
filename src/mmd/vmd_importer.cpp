#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/vmd_importer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace wisteria
{
namespace
{
constexpr std::string_view VmdSignature = "Vocaloid Motion Data 0002";
constexpr std::size_t HeaderSignatureSize = 30U;
constexpr std::size_t HeaderModelNameSize = 20U;
constexpr std::size_t BoneNameSize = 15U;
constexpr std::size_t BoneRecordSize =
    BoneNameSize + sizeof(std::uint32_t) + 3U * sizeof(float) +
    4U * sizeof(float) + 64U;
constexpr std::size_t MorphRecordSize = 15U + sizeof(std::uint32_t) +
    sizeof(float);
constexpr std::size_t CameraRecordSize = 61U;
constexpr std::size_t LightRecordSize = 28U;
constexpr std::size_t SelfShadowRecordSize = 9U;
constexpr std::size_t IkNameSize = 20U;
constexpr std::size_t IkStateRecordSize = IkNameSize + 1U;

class VmdReader
{
public:
    explicit VmdReader(std::span<const std::uint8_t> bytes)
        : bytes(bytes)
    {
    }

    template<typename T>
    T Read()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        this->Require(sizeof(T));
        T value{};
        std::memcpy(&value, this->bytes.data() + this->offset, sizeof(T));
        this->offset += sizeof(T);
        return value;
    }

    std::span<const std::uint8_t> ReadBytes(std::size_t count)
    {
        this->Require(count);
        const auto result = this->bytes.subspan(this->offset, count);
        this->offset += count;
        return result;
    }

    std::size_t Remaining() const noexcept
    {
        return this->bytes.size() - this->offset;
    }

private:
    void Require(std::size_t count) const
    {
        if (count > this->Remaining())
            throw std::runtime_error("VMD file ended unexpectedly");
    }

    std::span<const std::uint8_t> bytes;
    std::size_t offset = 0;
};

bool SkipFixedRecordSection(
    VmdReader& reader,
    std::size_t recordSize,
    const char* sectionName
)
{
    if (reader.Remaining() == 0U)
        return false;
    const std::uint32_t count = reader.Read<std::uint32_t>();
    if (recordSize == 0U || count > reader.Remaining() / recordSize)
    {
        throw std::runtime_error(
            std::string("VMD ") + sectionName + " section is truncated"
        );
    }
    reader.ReadBytes(static_cast<std::size_t>(count) * recordSize);
    return true;
}

std::vector<std::uint8_t> ReadBinaryFile(
    const std::filesystem::path& filePath
)
{
    std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open VMD file: " + filePath.string());

    const std::streamoff size = stream.tellg();
    if (size < 0)
        throw std::runtime_error("Cannot determine VMD file size");
    if (static_cast<std::uintmax_t>(size) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error("VMD file is too large");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (!stream)
            throw std::runtime_error("Cannot read complete VMD file");
    }
    return bytes;
}

std::span<const std::uint8_t> TrimFixedString(
    std::span<const std::uint8_t> bytes
)
{
    const auto terminator = std::find(bytes.begin(), bytes.end(), 0U);
    return bytes.first(static_cast<std::size_t>(
        std::distance(bytes.begin(), terminator)
    ));
}

std::string DecodeShiftJis(std::span<const std::uint8_t> bytes)
{
    bytes = TrimFixedString(bytes);
    if (bytes.empty())
        return {};

#ifdef _WIN32
    const int inputLength = static_cast<int>(bytes.size());
    const int wideLength = MultiByteToWideChar(
        932,
        0,
        reinterpret_cast<const char*>(bytes.data()),
        inputLength,
        nullptr,
        0
    );
    if (wideLength <= 0)
        throw std::runtime_error("VMD contains invalid Shift-JIS text");

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            932,
            0,
            reinterpret_cast<const char*>(bytes.data()),
            inputLength,
            wide.data(),
            wideLength
        ) != wideLength)
    {
        throw std::runtime_error("VMD Shift-JIS conversion failed");
    }

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        wideLength,
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (utf8Length <= 0)
        throw std::runtime_error("VMD UTF-8 conversion failed");
    std::string result(static_cast<std::size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.data(),
            wideLength,
            result.data(),
            utf8Length,
            nullptr,
            nullptr
        ) != utf8Length)
    {
        throw std::runtime_error("VMD UTF-8 conversion failed");
    }
    return result;
#else
    iconv_t converter = iconv_open("UTF-8", "CP932");
    if (converter == reinterpret_cast<iconv_t>(-1))
        converter = iconv_open("UTF-8", "SHIFT-JIS");
    if (converter == reinterpret_cast<iconv_t>(-1))
        throw std::runtime_error("No Shift-JIS decoder is available");

    std::vector<char> output(bytes.size() * 4U + 4U, '\0');
    char* inputPointer = const_cast<char*>(
        reinterpret_cast<const char*>(bytes.data())
    );
    std::size_t inputRemaining = bytes.size();
    char* outputPointer = output.data();
    std::size_t outputRemaining = output.size();
    errno = 0;
    const std::size_t conversionResult = iconv(
        converter,
        &inputPointer,
        &inputRemaining,
        &outputPointer,
        &outputRemaining
    );
    const int conversionError = errno;
    iconv_close(converter);
    if (conversionResult == static_cast<std::size_t>(-1) ||
        inputRemaining != 0U)
    {
        throw std::runtime_error(
            "VMD contains invalid Shift-JIS text (iconv error " +
            std::to_string(conversionError) + ")"
        );
    }
    return std::string(output.data(), output.size() - outputRemaining);
#endif
}

std::string Utf8Stem(const std::filesystem::path& path)
{
    const std::u8string value = path.stem().u8string();
    return std::string(value.begin(), value.end());
}

struct VmdBoneFrame
{
    std::uint32_t frame = 0;
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::array<std::uint8_t, 64> interpolation{};
};

struct VmdIkStateFrame
{
    std::uint32_t frame = 0U;
    bool enabled = true;
};

struct VmdMorphFrame
{
    std::uint32_t frame = 0U;
    float weight = 0.0f;
};

bool IsFinite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFinite(const glm::quat& value) noexcept
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
        std::isfinite(value.y) && std::isfinite(value.z);
}

KeyframeInterpolation ReadInterpolation(
    const std::array<std::uint8_t, 64>& bytes,
    std::size_t offset
)
{
    constexpr float InverseMaximum = 1.0f / 127.0f;
    const float x1 = std::min(bytes[offset], std::uint8_t{127}) *
        InverseMaximum;
    const float y1 = std::min(bytes[offset + 4U], std::uint8_t{127}) *
        InverseMaximum;
    const float x2 = std::min(bytes[offset + 8U], std::uint8_t{127}) *
        InverseMaximum;
    const float y2 = std::min(bytes[offset + 12U], std::uint8_t{127}) *
        InverseMaximum;

    if (std::abs(x1 - y1) <= 0.000001f &&
        std::abs(x2 - y2) <= 0.000001f)
    {
        return {};
    }
    return KeyframeInterpolation{
        AnimationInterpolation::CubicBezier,
        glm::vec2(x1, y1),
        glm::vec2(x2, y2)
    };
}

std::string ValidatedClipName(
    std::string sourceName,
    const VmdImportOptions& options
)
{
    std::string result = options.clipName.empty()
        ? std::move(sourceName)
        : options.clipName;
    if (result.empty())
        result = "vmdAnimation";
    return result;
}
}

ImportedVmdAnimationData VmdImporter::Import(
    const std::filesystem::path& filePath,
    const Skeleton& skeleton,
    const VmdImportOptions& options,
    const MorphSet* morphSet
) const
{
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(filePath).lexically_normal();
    if (!std::filesystem::is_regular_file(absolutePath))
        throw std::invalid_argument("VMD file does not exist: " + filePath.string());

    const std::vector<std::uint8_t> bytes = ReadBinaryFile(absolutePath);
    return this->Import(
        bytes,
        skeleton,
        Utf8Stem(absolutePath),
        options,
        morphSet
    );
}

ImportedVmdAnimationData VmdImporter::Import(
    std::span<const std::uint8_t> bytes,
    const Skeleton& skeleton,
    std::string sourceName,
    const VmdImportOptions& options,
    const MorphSet* morphSet
) const
{
    if (!std::isfinite(options.framesPerSecond) ||
        options.framesPerSecond <= 0.0f)
    {
        throw std::invalid_argument("VMD frame rate must be finite and positive");
    }

    VmdReader reader(bytes);
    const auto signatureBytes = reader.ReadBytes(HeaderSignatureSize);
    const std::string signature(
        reinterpret_cast<const char*>(signatureBytes.data()),
        VmdSignature.size()
    );
    if (signature != VmdSignature)
        throw std::runtime_error("File is not Vocaloid Motion Data 0002");

    const std::string modelName = DecodeShiftJis(
        reader.ReadBytes(HeaderModelNameSize)
    );
    const std::uint32_t boneFrameCount = reader.Read<std::uint32_t>();
    if (static_cast<std::uint64_t>(boneFrameCount) * BoneRecordSize >
        reader.Remaining())
    {
        throw std::runtime_error("VMD bone-frame section is truncated");
    }

    std::unordered_map<std::string, std::vector<VmdBoneFrame>> framesByBone;
    framesByBone.reserve(std::min<std::size_t>(boneFrameCount, 1024U));
    for (std::uint32_t index = 0; index < boneFrameCount; ++index)
    {
        const std::string boneName = DecodeShiftJis(
            reader.ReadBytes(BoneNameSize)
        );
        VmdBoneFrame frame;
        frame.frame = reader.Read<std::uint32_t>();
        const float translationX = reader.Read<float>();
        const float translationY = reader.Read<float>();
        const float translationZ = reader.Read<float>();
        frame.translation = glm::vec3(
            translationX,
            translationY,
            translationZ
        );
        const float rotationX = reader.Read<float>();
        const float rotationY = reader.Read<float>();
        const float rotationZ = reader.Read<float>();
        const float rotationW = reader.Read<float>();
        frame.rotation = glm::quat(
            rotationW,
            rotationX,
            rotationY,
            rotationZ
        );
        const auto interpolation = reader.ReadBytes(frame.interpolation.size());
        std::copy(
            interpolation.begin(),
            interpolation.end(),
            frame.interpolation.begin()
        );

        if (boneName.empty())
            throw std::runtime_error("VMD contains an empty bone name");
        if (!IsFinite(frame.translation) || !IsFinite(frame.rotation))
            throw std::runtime_error("VMD contains a non-finite bone keyframe");
        if (glm::length(frame.rotation) <= 0.000001f)
            frame.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        else
            frame.rotation = glm::normalize(frame.rotation);
        framesByBone[boneName].push_back(std::move(frame));
    }

    std::unordered_map<std::string, std::vector<VmdMorphFrame>>
        morphFramesByName;
    bool hasMorphSection = false;
    if (reader.Remaining() != 0U)
    {
        hasMorphSection = true;
        const std::uint32_t morphFrameCount = reader.Read<std::uint32_t>();
        if (morphFrameCount > reader.Remaining() / MorphRecordSize)
            throw std::runtime_error("VMD morph section is truncated");
        morphFramesByName.reserve(std::min<std::size_t>(
            morphFrameCount,
            1024U
        ));
        for (std::uint32_t index = 0U; index < morphFrameCount; ++index)
        {
            const std::string morphName = DecodeShiftJis(
                reader.ReadBytes(15U)
            );
            VmdMorphFrame frame;
            frame.frame = reader.Read<std::uint32_t>();
            frame.weight = reader.Read<float>();
            if (morphName.empty())
                throw std::runtime_error("VMD contains an empty morph name");
            if (!std::isfinite(frame.weight))
                throw std::runtime_error("VMD contains a non-finite morph weight");
            morphFramesByName[morphName].push_back(frame);
        }
    }

    std::unordered_map<std::string, std::vector<VmdIkStateFrame>>
        ikFramesByBone;
    if (hasMorphSection &&
        SkipFixedRecordSection(reader, CameraRecordSize, "camera") &&
        SkipFixedRecordSection(reader, LightRecordSize, "light") &&
        SkipFixedRecordSection(reader, SelfShadowRecordSize, "self-shadow") &&
        reader.Remaining() != 0U)
    {
        const std::uint32_t modelFrameCount = reader.Read<std::uint32_t>();
        constexpr std::size_t MinimumModelFrameSize =
            sizeof(std::uint32_t) + sizeof(std::uint8_t) +
            sizeof(std::uint32_t);
        if (modelFrameCount > reader.Remaining() / MinimumModelFrameSize)
        {
            throw std::runtime_error(
                "VMD model/IK-state section is truncated"
            );
        }
        for (std::uint32_t modelFrameIndex = 0U;
             modelFrameIndex < modelFrameCount;
             ++modelFrameIndex)
        {
            const std::uint32_t frame = reader.Read<std::uint32_t>();
            reader.Read<std::uint8_t>(); // Model visibility is a future track.
            const std::uint32_t ikStateCount = reader.Read<std::uint32_t>();
            if (ikStateCount > reader.Remaining() / IkStateRecordSize)
            {
                throw std::runtime_error(
                    "VMD model/IK-state section is truncated"
                );
            }
            for (std::uint32_t stateIndex = 0U;
                 stateIndex < ikStateCount;
                 ++stateIndex)
            {
                const std::string ikName = DecodeShiftJis(
                    reader.ReadBytes(IkNameSize)
                );
                const std::uint8_t enabled = reader.Read<std::uint8_t>();
                if (ikName.empty())
                    throw std::runtime_error("VMD contains an empty IK name");
                if (enabled > 1U)
                    throw std::runtime_error("VMD IK state is invalid");
                ikFramesByBone[ikName].push_back(VmdIkStateFrame{
                    frame,
                    enabled != 0U
                });
            }
        }
    }

    std::vector<AnimationTrack> tracks;
    std::vector<MmdIkStateTrack> ikStateTracks;
    std::vector<MorphWeightTrack> morphWeightTracks;
    std::vector<std::string> unmatchedBoneNames;
    std::vector<std::string> unmatchedIkNames;
    std::vector<std::string> unmatchedMorphNames;
    tracks.reserve(framesByBone.size());
    ikStateTracks.reserve(ikFramesByBone.size());
    morphWeightTracks.reserve(morphFramesByName.size());
    unmatchedBoneNames.reserve(framesByBone.size());
    unmatchedIkNames.reserve(ikFramesByBone.size());
    unmatchedMorphNames.reserve(morphFramesByName.size());
    float duration = 0.0f;

    for (auto& [boneName, sourceFrames] : framesByBone)
    {
        const std::optional<BoneIndex> boneIndex = skeleton.FindBone(boneName);
        if (!boneIndex.has_value())
        {
            unmatchedBoneNames.push_back(boneName);
            continue;
        }

        std::stable_sort(
            sourceFrames.begin(),
            sourceFrames.end(),
            [](const VmdBoneFrame& left, const VmdBoneFrame& right)
            {
                return left.frame < right.frame;
            }
        );
        std::vector<VmdBoneFrame> uniqueFrames;
        uniqueFrames.reserve(sourceFrames.size());
        for (VmdBoneFrame& frame : sourceFrames)
        {
            if (!uniqueFrames.empty() &&
                uniqueFrames.back().frame == frame.frame)
            {
                uniqueFrames.back() = std::move(frame);
            }
            else
            {
                uniqueFrames.push_back(std::move(frame));
            }
        }

        const BoneTransform bindTransform = BoneTransform::FromMatrix(
            skeleton.BoneAt(*boneIndex).bindLocalMatrix
        );
        std::vector<VectorKeyframe> translationKeys;
        std::vector<QuaternionKeyframe> rotationKeys;
        translationKeys.reserve(uniqueFrames.size());
        rotationKeys.reserve(uniqueFrames.size());
        glm::quat previousRotation = bindTransform.rotation;

        for (const VmdBoneFrame& frame : uniqueFrames)
        {
            const float time = static_cast<float>(frame.frame) /
                options.framesPerSecond;
            if (!std::isfinite(time))
                throw std::runtime_error("VMD keyframe time is out of range");

            // Assimp's PMX path mirrors MMD's Z axis for this renderer.
            const glm::vec3 translationDelta(
                frame.translation.x,
                frame.translation.y,
                -frame.translation.z
            );
            const glm::quat rotationDelta(
                frame.rotation.w,
                -frame.rotation.x,
                -frame.rotation.y,
                frame.rotation.z
            );
            glm::quat localRotation = glm::normalize(
                bindTransform.rotation * rotationDelta
            );
            if (glm::dot(previousRotation, localRotation) < 0.0f)
                localRotation = -localRotation;
            previousRotation = localRotation;

            translationKeys.push_back(VectorKeyframe{
                time,
                bindTransform.translation + translationDelta,
                {
                    ReadInterpolation(frame.interpolation, 0U),
                    ReadInterpolation(frame.interpolation, 16U),
                    ReadInterpolation(frame.interpolation, 32U)
                }
            });
            rotationKeys.push_back(QuaternionKeyframe{
                time,
                localRotation,
                ReadInterpolation(frame.interpolation, 48U)
            });
            duration = std::max(duration, time);
        }

        tracks.emplace_back(
            *boneIndex,
            std::move(translationKeys),
            std::move(rotationKeys)
        );
    }

    for (auto& [boneName, sourceFrames] : ikFramesByBone)
    {
        const std::optional<BoneIndex> boneIndex = skeleton.FindBone(boneName);
        if (!boneIndex.has_value() ||
            !skeleton.BoneAt(*boneIndex).ikConstraint.has_value())
        {
            unmatchedIkNames.push_back(boneName);
            continue;
        }

        std::stable_sort(
            sourceFrames.begin(),
            sourceFrames.end(),
            [](const VmdIkStateFrame& left, const VmdIkStateFrame& right)
            {
                return left.frame < right.frame;
            }
        );
        std::vector<VmdIkStateFrame> uniqueFrames;
        uniqueFrames.reserve(sourceFrames.size());
        for (const VmdIkStateFrame& frame : sourceFrames)
        {
            if (!uniqueFrames.empty() &&
                uniqueFrames.back().frame == frame.frame)
            {
                uniqueFrames.back() = frame;
            }
            else
            {
                uniqueFrames.push_back(frame);
            }
        }

        std::vector<BoolKeyframe> keys;
        keys.reserve(uniqueFrames.size());
        for (const VmdIkStateFrame& frame : uniqueFrames)
        {
            const float time = static_cast<float>(frame.frame) /
                options.framesPerSecond;
            if (!std::isfinite(time))
                throw std::runtime_error("VMD IK keyframe time is out of range");
            keys.push_back(BoolKeyframe{time, frame.enabled});
            duration = std::max(duration, time);
        }
        ikStateTracks.emplace_back(*boneIndex, std::move(keys));
    }

    for (auto& [morphName, sourceFrames] : morphFramesByName)
    {
        const std::optional<MorphIndex> morphIndex = morphSet == nullptr
            ? std::nullopt
            : morphSet->FindMorph(morphName);
        if (!morphIndex.has_value())
        {
            unmatchedMorphNames.push_back(morphName);
            continue;
        }
        std::stable_sort(
            sourceFrames.begin(),
            sourceFrames.end(),
            [](const VmdMorphFrame& left, const VmdMorphFrame& right)
            {
                return left.frame < right.frame;
            }
        );
        std::vector<VmdMorphFrame> uniqueFrames;
        uniqueFrames.reserve(sourceFrames.size());
        for (const VmdMorphFrame& frame : sourceFrames)
        {
            if (!uniqueFrames.empty() &&
                uniqueFrames.back().frame == frame.frame)
            {
                uniqueFrames.back() = frame;
            }
            else
            {
                uniqueFrames.push_back(frame);
            }
        }
        std::vector<FloatKeyframe> keys;
        keys.reserve(uniqueFrames.size());
        for (const VmdMorphFrame& frame : uniqueFrames)
        {
            const float time = static_cast<float>(frame.frame) /
                options.framesPerSecond;
            if (!std::isfinite(time))
                throw std::runtime_error("VMD morph keyframe time is out of range");
            keys.push_back(FloatKeyframe{time, frame.weight});
            duration = std::max(duration, time);
        }
        morphWeightTracks.emplace_back(*morphIndex, std::move(keys));
    }

    if (tracks.empty() && ikStateTracks.empty() && morphWeightTracks.empty())
    {
        throw std::runtime_error(
            "VMD animation has no bone tracks matching the target Skeleton"
        );
    }
    std::sort(unmatchedBoneNames.begin(), unmatchedBoneNames.end());
    std::sort(unmatchedIkNames.begin(), unmatchedIkNames.end());
    std::sort(unmatchedMorphNames.begin(), unmatchedMorphNames.end());
    duration = std::max(duration, 1.0f / options.framesPerSecond);

    return ImportedVmdAnimationData{
        modelName,
        framesByBone.size(),
        morphFramesByName.size(),
        ikFramesByBone.size(),
        std::move(unmatchedBoneNames),
        std::move(unmatchedMorphNames),
        std::move(unmatchedIkNames),
        AnimationClip(
            ValidatedClipName(std::move(sourceName), options),
            duration,
            std::move(tracks),
            std::move(ikStateTracks),
            std::move(morphWeightTracks)
        )
    };
}
}  // namespace wisteria
