#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/environment.hpp"

#include <fstream>
#include <limits>
#include <cstdint>
#include <stdexcept>
#include "wisteria/vendor/stb_image.h"
#include <utility>
#include <vector>

namespace wisteria
{
namespace
{
std::vector<std::uint8_t> ReadBinaryFile(
    const std::filesystem::path& filePath
)
{
    std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open environment image: " + filePath.string());

    const std::streampos end = stream.tellg();
    if (end <= 0)
        throw std::runtime_error("Environment image is empty: " + filePath.string());
    if (static_cast<std::uintmax_t>(end) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error("Environment image is too large");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!stream)
        throw std::runtime_error("Cannot read environment image: " + filePath.string());
    return bytes;
}
}  // namespace

std::shared_ptr<const EnvironmentHdrImage> DecodeEquirectangularHdr(
    const std::filesystem::path& filePath
)
{
    const std::vector<std::uint8_t> bytes = ReadBinaryFile(filePath);
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Environment image is too large for stb_image");

    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
        stbi_loadf_from_memory(
            bytes.data(),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels,
            3
        ),
        stbi_image_free
    );
    if (pixels == nullptr)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "Cannot decode environment image: " +
            std::string(reason != nullptr ? reason : "unknown stb_image error")
        );
    }
    if (width <= 0 || height <= 0)
    {
        throw std::runtime_error(
            "Environment image decoder returned non-positive dimensions"
        );
    }
    const std::uint64_t width64 = static_cast<std::uint64_t>(width);
    const std::uint64_t height64 = static_cast<std::uint64_t>(height);
    const std::uint64_t pixelCount = width64 * height64;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 3U)
    {
        throw std::length_error(
            "Environment image pixel count is too large"
        );
    }

    auto image = std::make_shared<EnvironmentHdrImage>();
    image->width = static_cast<std::uint32_t>(width);
    image->height = static_cast<std::uint32_t>(height);
    image->rgb.assign(
        pixels.get(),
        pixels.get() +
            static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 3U
    );
    return image;
}
}  // namespace wisteria
