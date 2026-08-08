#include "wisteria/common/pch.hpp"
#include "wisteria/common/png_encoder.hpp"
#include "wisteria/vendor/miniz.h"

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace wisteria
{
namespace
{
void AppendChunk(
    std::vector<std::uint8_t>& output,
    const char type[4],
    std::span<const std::uint8_t> data
)
{
    const std::uint32_t length = static_cast<std::uint32_t>(data.size());
    const auto put32 = [&output](std::uint32_t value)
    {
        output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
        output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
        output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
        output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    };
    put32(length);
    const std::size_t crcStart = output.size();
    output.insert(output.end(), type, type + 4);
    output.insert(output.end(), data.begin(), data.end());
    std::uint32_t crc = static_cast<std::uint32_t>(
        mz_crc32(0U, nullptr, 0U)
    );
    crc = static_cast<std::uint32_t>(mz_crc32(
        crc,
        output.data() + crcStart,
        output.size() - crcStart
    ));
    put32(crc);
}
}

std::vector<std::uint8_t> EncodePngRgba8(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint8_t> rgba
)
{
    if (width == 0U || height == 0U)
    {
        throw std::invalid_argument("PNG dimensions must be positive");
    }
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * 4U;
    const std::uint64_t expected = rowBytes * height;
    if (expected != rgba.size())
    {
        throw std::invalid_argument(
            "PNG RGBA buffer size does not match width * height * 4"
        );
    }

    // Scanlines: one filter byte (0 = None) + RGBA row.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(expected + height));
    for (std::uint32_t row = 0U; row < height; ++row)
    {
        raw.push_back(0U);
        const std::uint8_t* source =
            rgba.data() + static_cast<std::size_t>(row) * rowBytes;
        raw.insert(raw.end(), source, source + rowBytes);
    }

    mz_ulong compressedBytes =
        mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedBytes);
    const int status = mz_compress2(
        compressed.data(),
        &compressedBytes,
        raw.data(),
        static_cast<mz_ulong>(raw.size()),
        6
    );
    if (status != MZ_OK)
    {
        throw std::runtime_error("PNG compression failed");
    }
    compressed.resize(compressedBytes);

    std::vector<std::uint8_t> output;
    output.reserve(64U + raw.size());
    constexpr std::uint8_t kSignature[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    output.insert(output.end(), kSignature, kSignature + 8);

    std::array<std::uint8_t, 13> header{};
    header[0] = static_cast<std::uint8_t>((width >> 24) & 0xFFU);
    header[1] = static_cast<std::uint8_t>((width >> 16) & 0xFFU);
    header[2] = static_cast<std::uint8_t>((width >> 8) & 0xFFU);
    header[3] = static_cast<std::uint8_t>(width & 0xFFU);
    header[4] = static_cast<std::uint8_t>((height >> 24) & 0xFFU);
    header[5] = static_cast<std::uint8_t>((height >> 16) & 0xFFU);
    header[6] = static_cast<std::uint8_t>((height >> 8) & 0xFFU);
    header[7] = static_cast<std::uint8_t>(height & 0xFFU);
    header[8] = 8U;   // bit depth
    header[9] = 6U;   // color type RGBA
    header[10] = 0U;  // compression
    header[11] = 0U;  // filter
    header[12] = 0U;  // interlace
    AppendChunk(output, "IHDR", header);
    AppendChunk(output, "IDAT", compressed);
    AppendChunk(output, "IEND", {});
    return output;
}
}  // namespace wisteria
