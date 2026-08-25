#pragma once

#include "wisteria/core/version.hpp"
#include "wisteria/native/wisteria_stable_runtime.h"
#include "wisteria/sdk/status.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

namespace wisteria::sdk
{

// RAII owner of a WisteriaStableContext.
//
// Threading: the stable C ABI defines a Context as creator-thread-affine.
// All calls through this wrapper must stay on the thread that constructed
// the Context. The wrapper does not add runtime thread checks.
class Context
{
public:
    Context()
    {
        const std::uint32_t status =
            wisteria_stable_context_create(&handle_);
        if (status != WISTERIA_STATUS_OK)
        {
            handle_ = 0U;
            throw StatusError(
                status,
                "wisteria_stable_context_create failed"
            );
        }
    }

    ~Context()
    {
        if (handle_ != 0U)
            (void)wisteria_stable_context_destroy(handle_);
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&& other) noexcept
        : handle_(std::exchange(other.handle_, 0U))
    {
    }

    Context& operator=(Context&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != 0U)
                (void)wisteria_stable_context_destroy(handle_);
            handle_ = std::exchange(other.handle_, 0U);
        }
        return *this;
    }

    WisteriaStableContext Handle() const noexcept
    {
        return handle_;
    }

    std::uint32_t RuntimeAbiVersion() const
    {
        WisteriaStableContextInfoV1 info{};
        info.struct_size = sizeof(info);
        info.struct_version = 1U;
        CheckStatus(
            handle_,
            wisteria_stable_context_info(handle_, &info),
            "context_info"
        );
        return info.abi_version;
    }

    const char* LastError() const noexcept
    {
        return handle_ != 0U
            ? wisteria_stable_last_error(handle_)
            : nullptr;
    }

    static constexpr std::string_view ProductVersion() noexcept
    {
        return WISTERIA_VERSION_STRING;
    }

private:
    WisteriaStableContext handle_ = 0U;
};

}  // namespace wisteria::sdk
