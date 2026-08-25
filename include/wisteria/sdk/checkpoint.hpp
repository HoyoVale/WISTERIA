#pragma once

#include "wisteria/native/wisteria_stable_runtime.h"
#include "wisteria/sdk/context.hpp"
#include "wisteria/sdk/entity.hpp"
#include "wisteria/sdk/status.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace wisteria::sdk
{

// RAII owner of a Context-owned checkpoint value object.
class Checkpoint
{
public:
    Checkpoint(Context& context, Entity& entity)
        : context_(context)
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_checkpoint_create(
                context_.Handle(),
                entity.Handle(),
                &handle_
            ),
            "checkpoint_create"
        );
    }

    ~Checkpoint()
    {
        if (handle_ != 0U)
            (void)wisteria_stable_checkpoint_destroy(
                context_.Handle(),
                handle_
            );
    }

    Checkpoint(const Checkpoint&) = delete;
    Checkpoint& operator=(const Checkpoint&) = delete;

    Checkpoint(Checkpoint&& other) noexcept
        : context_(other.context_),
          handle_(std::exchange(other.handle_, 0U))
    {
    }

    Checkpoint& operator=(Checkpoint&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != 0U)
                (void)wisteria_stable_checkpoint_destroy(
                    context_.Handle(),
                    handle_
                );
            handle_ = std::exchange(other.handle_, 0U);
        }
        return *this;
    }

    WisteriaCheckpoint Handle() const noexcept
    {
        return handle_;
    }

    WisteriaCheckpointInfoV1 Info() const
    {
        WisteriaCheckpointInfoV1 info{};
        info.struct_size = sizeof(info);
        info.struct_version = 1U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_checkpoint_info(
                context_.Handle(),
                handle_,
                &info
            ),
            "checkpoint_info"
        );
        return info;
    }

    void Restore(Entity& entity)
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_checkpoint_restore(
                context_.Handle(),
                handle_,
                entity.Handle()
            ),
            "checkpoint_restore"
        );
    }

    std::vector<std::uint8_t> Serialize() const
    {
        std::uint64_t size = 0U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_checkpoint_serialize(
                context_.Handle(),
                handle_,
                nullptr,
                &size
            ),
            "checkpoint_serialize_size"
        );

        std::vector<std::uint8_t> bytes(size);
        CheckStatus(
            context_.Handle(),
            wisteria_stable_checkpoint_serialize(
                context_.Handle(),
                handle_,
                bytes.data(),
                &size
            ),
            "checkpoint_serialize"
        );
        bytes.resize(size);
        return bytes;
    }

    static Checkpoint Deserialize(
        Context& context,
        std::span<const std::uint8_t> bytes
    )
    {
        WisteriaCheckpoint handle = 0U;
        CheckStatus(
            context.Handle(),
            wisteria_stable_checkpoint_deserialize(
                context.Handle(),
                bytes.data(),
                static_cast<std::uint64_t>(bytes.size()),
                &handle
            ),
            "checkpoint_deserialize"
        );
        return Checkpoint(context, handle);
    }

private:
    Checkpoint(Context& context, WisteriaCheckpoint handle) noexcept
        : context_(context),
          handle_(handle)
    {
    }

    Context& context_;
    WisteriaCheckpoint handle_ = 0U;
};

}  // namespace wisteria::sdk
