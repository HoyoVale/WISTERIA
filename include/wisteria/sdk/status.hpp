#pragma once

#include "wisteria/native/wisteria_stable_runtime.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace wisteria::sdk
{

// C++ exception carrying the stable C ABI status code. Status codes are the
// authoritative result; the message is human-readable only.
class StatusError final : public std::runtime_error
{
public:
    StatusError(std::uint32_t code, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code)
    {
    }

    std::uint32_t Code() const noexcept
    {
        return code_;
    }

private:
    std::uint32_t code_ = WISTERIA_STATUS_INTERNAL;
};

inline void CheckStatus(
    WisteriaStableContext context,
    std::uint32_t status,
    std::string_view operation
)
{
    if (status == WISTERIA_STATUS_OK)
        return;

    std::string message(operation);
    message += " failed with status ";
    message += std::to_string(status);
    if (context != 0U)
    {
        const char* detail = wisteria_stable_last_error(context);
        if (detail != nullptr && detail[0] != '\0')
        {
            message += ": ";
            message += detail;
        }
    }
    throw StatusError(status, std::move(message));
}

}  // namespace wisteria::sdk
