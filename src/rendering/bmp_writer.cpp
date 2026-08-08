#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/bmp_writer.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace wisteria
{
void WriteBmp24(
    const std::filesystem::path& path,
    int width,
    int height,
    std::span<const std::uint8_t> rgbaTopDown
)
{
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument(
            "BMP dimensions must be positive"
        );
    }
    const std::size_t expected =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4U;
    if (rgbaTopDown.size() != expected)
    {
        throw std::invalid_argument(
            "BMP RGBA buffer size does not match width * height * 4"
        );
    }

    const std::size_t rowSize =
        ((static_cast<std::size_t>(width) * 3U + 3U) / 4U) * 4U;
    const std::size_t dataSize = rowSize * static_cast<std::size_t>(height);
    const std::uint32_t fileSize =
        54U + static_cast<std::uint32_t>(dataSize);

    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        throw std::runtime_error(
            "Cannot open BMP file: " + path.string()
        );
    }
    const auto put32 = [&output](std::uint32_t value)
    {
        const unsigned char bytes[4] = {
            static_cast<unsigned char>(value & 0xFFU),
            static_cast<unsigned char>((value >> 8) & 0xFFU),
            static_cast<unsigned char>((value >> 16) & 0xFFU),
            static_cast<unsigned char>((value >> 24) & 0xFFU)
        };
        output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    };
    const auto put16 = [&output](std::uint16_t value)
    {
        const unsigned char bytes[2] = {
            static_cast<unsigned char>(value & 0xFFU),
            static_cast<unsigned char>((value >> 8) & 0xFFU)
        };
        output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    };

    output.put('B');
    output.put('M');
    put32(fileSize);
    put16(0);
    put16(0);
    put32(54);
    put32(40);
    put32(static_cast<std::uint32_t>(width));
    put32(static_cast<std::uint32_t>(height));
    put16(1);
    put16(24);
    put32(0);
    put32(static_cast<std::uint32_t>(dataSize));
    put32(2835);
    put32(2835);
    put32(0);
    put32(0);

    // BMP with positive height stores rows bottom-up: file row 0 is the
    // image bottom. The input is canonical top-left, so write rows in
    // reverse order of the input.
    std::vector<unsigned char> row(rowSize, 0U);
    for (int y = height - 1; y >= 0; --y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t pixel =
                (static_cast<std::size_t>(y) * width +
                 static_cast<std::size_t>(x)) * 4U;
            row[static_cast<std::size_t>(x) * 3U] =
                rgbaTopDown[pixel + 2U];
            row[static_cast<std::size_t>(x) * 3U + 1U] =
                rgbaTopDown[pixel + 1U];
            row[static_cast<std::size_t>(x) * 3U + 2U] =
                rgbaTopDown[pixel];
        }
        output.write(
            reinterpret_cast<const char*>(row.data()),
            static_cast<std::streamsize>(row.size())
        );
    }
    output.close();
    if (!output)
    {
        throw std::runtime_error(
            "Failed to flush BMP file: " + path.string()
        );
    }
}
}  // namespace wisteria
