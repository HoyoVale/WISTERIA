#pragma once

#include "wisteria/native/wisteria_stable_runtime.h"
#include "wisteria/sdk/context.hpp"
#include "wisteria/sdk/status.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace wisteria::sdk
{

// Typed view of WISTERIA_RUNTIME_CREATION_OPTIONS_V1. Defaults mirror the
// engine's current deterministic MMD physics configuration.
struct RuntimeCreationOptions
{
    std::uint32_t compatibility = WISTERIA_PROFILE_ID_RAW;
    float fixedTimeStep = 1.0f / 120.0f;
    std::int32_t maxSubSteps = 10;
    std::array<float, 3> gravity{0.0f, -98.0f, 0.0f};
    bool physicsEnabled = true;
};

// RAII owner of a stable runtime entity created from a model path.
class Entity
{
public:
    Entity(
        Context& context,
        std::string_view modelPathUtf8,
        const RuntimeCreationOptions& options = {}
    )
        : context_(context)
    {
        WisteriaRuntimeCreationOptionsV1 nativeOptions{};
        nativeOptions.struct_size = sizeof(nativeOptions);
        nativeOptions.struct_version = 1U;
        nativeOptions.compatibility = options.compatibility;
        nativeOptions.fixed_time_step = options.fixedTimeStep;
        nativeOptions.max_sub_steps = options.maxSubSteps;
        nativeOptions.gravity[0] = options.gravity[0];
        nativeOptions.gravity[1] = options.gravity[1];
        nativeOptions.gravity[2] = options.gravity[2];
        nativeOptions.physics_enabled = options.physicsEnabled ? 1 : 0;

        const std::string modelPath(modelPathUtf8);
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_create(
                context_.Handle(),
                &nativeOptions,
                modelPath.c_str(),
                &handle_
            ),
            "entity_create"
        );
    }

    ~Entity()
    {
        if (handle_ != 0U)
            (void)wisteria_stable_entity_destroy(
                context_.Handle(),
                handle_
            );
    }

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    Entity(Entity&& other) noexcept
        : context_(other.context_),
          handle_(std::exchange(other.handle_, 0U))
    {
    }

    Entity& operator=(Entity&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != 0U)
                (void)wisteria_stable_entity_destroy(
                    context_.Handle(),
                    handle_
                );
            handle_ = std::exchange(other.handle_, 0U);
        }
        return *this;
    }

    WisteriaEntity Handle() const noexcept
    {
        return handle_;
    }

    WisteriaRuntimeCapabilitiesV1 Capabilities() const
    {
        WisteriaRuntimeCapabilitiesV1 capabilities{};
        capabilities.struct_size = sizeof(capabilities);
        capabilities.struct_version = 1U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_capabilities(
                context_.Handle(),
                handle_,
                &capabilities
            ),
            "entity_capabilities"
        );
        return capabilities;
    }

    std::uint64_t AssetFingerprint() const
    {
        std::uint64_t fingerprint = 0U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_asset_fingerprint(
                context_.Handle(),
                handle_,
                &fingerprint
            ),
            "entity_asset_fingerprint"
        );
        return fingerprint;
    }

    void SetMorphOverride(std::string_view name, float weight)
    {
        const std::string morphName(name);
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_set_morph_override(
                context_.Handle(),
                handle_,
                morphName.c_str(),
                weight
            ),
            "entity_set_morph_override"
        );
    }

    void ClearMorphOverride(std::string_view name)
    {
        const std::string morphName(name);
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_clear_morph_override(
                context_.Handle(),
                handle_,
                morphName.c_str()
            ),
            "entity_clear_morph_override"
        );
    }

    void ClearAllMorphOverrides()
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_clear_all_morph_overrides(
                context_.Handle(),
                handle_
            ),
            "entity_clear_all_morph_overrides"
        );
    }

    void LoadMotion(std::string_view vmdPathUtf8)
    {
        const std::string vmdPath(vmdPathUtf8);
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_load_motion(
                context_.Handle(),
                handle_,
                vmdPath.c_str()
            ),
            "entity_load_motion"
        );
    }

    void UnloadMotion()
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_unload_motion(
                context_.Handle(),
                handle_
            ),
            "entity_unload_motion"
        );
    }

    void PrepareFrameZero()
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_prepare_frame_zero(
                context_.Handle(),
                handle_
            ),
            "entity_prepare_frame_zero"
        );
    }

    void StepExact(std::uint64_t frame)
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_step_exact(
                context_.Handle(),
                handle_,
                frame
            ),
            "entity_step_exact"
        );
    }

    void ReplayExact(std::uint64_t target)
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_replay_exact(
                context_.Handle(),
                handle_,
                target
            ),
            "entity_replay_exact"
        );
    }

    void SetPreviewFrame(double frame)
    {
        CheckStatus(
            context_.Handle(),
            wisteria_stable_entity_set_preview_frame(
                context_.Handle(),
                handle_,
                frame
            ),
            "entity_set_preview_frame"
        );
    }

private:
    Context& context_;
    WisteriaEntity handle_ = 0U;
};

}  // namespace wisteria::sdk
