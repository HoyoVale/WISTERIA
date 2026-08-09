#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace wisteria
{
// R1.8 Final Micro Fix: stable, platform-independent explicit byte encoding
// used for immutable asset/runtime-semantic fingerprints. Little-endian raw
// bit patterns; FNV-1a64 accumulation. No std::string formatting or float
// text conversion anywhere, so the same asset yields the same fingerprint
// on every platform.
class FingerprintBuilder
{
public:
    void Bytes(const void* data, std::size_t size)
    {
        const std::uint8_t* bytes =
            static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0U; index < size; ++index)
        {
            this->state ^= bytes[index];
            this->state *= kPrime;
        }
    }

    void U8(std::uint8_t value)
    {
        Bytes(&value, sizeof(value));
    }

    void Bool(bool value)
    {
        U8(value ? 1U : 0U);
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            U8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            U8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }

    void F32(float value)
    {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    void String(std::string_view value)
    {
        U64(value.size());
        Bytes(value.data(), value.size());
    }

    void Vec2(const glm::vec2& value)
    {
        F32(value.x);
        F32(value.y);
    }

    void Vec3(const glm::vec3& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
    }

    void Vec4(const glm::vec4& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
        F32(value.w);
    }

    void Quat(const glm::quat& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
        F32(value.w);
    }

    void Mat4(const glm::mat4& value)
    {
        for (glm::length_t column = 0U; column < 4U; ++column)
        {
            for (glm::length_t row = 0U; row < 4U; ++row)
                F32(value[column][row]);
        }
    }

    std::uint64_t Result() const noexcept
    {
        return this->state;
    }

private:
    static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    static constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t state = kOffsetBasis;
};
}  // namespace wisteria
