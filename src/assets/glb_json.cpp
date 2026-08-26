#include "assets/glb_json.hpp"

#include <array>
#include <cstring>

namespace wisteria::assets
{
namespace
{
constexpr std::uint32_t GlbMagic = 0x46546C67U;  // "glTF"
constexpr std::uint32_t JsonChunkType = 0x4E4F534AU;  // "JSON"

std::uint32_t ReadUint32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) noexcept
{
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
}

std::optional<nlohmann::json> ParseGlbJson(
    const std::vector<std::uint8_t>& bytes,
    std::string& error
)
{
    error.clear();
    if (bytes.size() < 12U)
    {
        error = "GLB is smaller than the 12-byte header";
        return std::nullopt;
    }

    const std::uint32_t magic = ReadUint32(bytes, 0U);
    const std::uint32_t version = ReadUint32(bytes, 4U);
    const std::uint32_t declaredLength = ReadUint32(bytes, 8U);
    if (magic != GlbMagic)
    {
        error = "GLB magic is invalid";
        return std::nullopt;
    }
    if (version != 2U)
    {
        error = "GLB version must be 2";
        return std::nullopt;
    }
    if (declaredLength != bytes.size())
    {
        error = "GLB length header does not match the byte count";
        return std::nullopt;
    }

    std::size_t offset = 12U;
    std::optional<std::size_t> jsonChunkOffset;
    std::optional<std::size_t> jsonChunkLength;
    while (offset + 8U <= bytes.size())
    {
        const std::uint32_t chunkLength = ReadUint32(bytes, offset);
        const std::uint32_t chunkType = ReadUint32(bytes, offset + 4U);
        offset += 8U;
        if (chunkLength > bytes.size() - offset)
        {
            error = "GLB chunk extends past the end of the file";
            return std::nullopt;
        }
        if (chunkType == JsonChunkType)
        {
            if (jsonChunkOffset.has_value())
            {
                error = "GLB contains more than one JSON chunk";
                return std::nullopt;
            }
            jsonChunkOffset = offset;
            jsonChunkLength = chunkLength;
        }
        offset += chunkLength;
    }

    if (!jsonChunkOffset.has_value() || !jsonChunkLength.has_value())
    {
        error = "GLB contains no JSON chunk";
        return std::nullopt;
    }

    try
    {
        const char* begin = reinterpret_cast<const char*>(
            bytes.data() + *jsonChunkOffset
        );
        return nlohmann::json::parse(
            begin,
            begin + *jsonChunkLength
        );
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return std::nullopt;
    }
}
}  // namespace wisteria::assets
