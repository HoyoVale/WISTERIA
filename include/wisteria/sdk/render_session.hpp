#pragma once

#include "wisteria/native/wisteria_stable_render.h"
#include "wisteria/sdk/context.hpp"
#include "wisteria/sdk/entity.hpp"
#include "wisteria/sdk/status.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace wisteria::sdk
{

// Typed view of WISTERIA_RENDER_CAMERA_V1.
struct RenderCamera
{
    std::array<float, 3> position{0.0f, 3.0f, 3.0f};
    std::array<float, 3> target{0.0f, 0.0f, 0.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    float verticalFovDegrees = 45.0f;
    float nearClip = 0.1f;
    float farClip = 100.0f;
};

// RAII owner of a stable headless/offline render session.
class RenderSession
{
public:
    explicit RenderSession(
        Context& context,
        bool forceSoftware = false
    )
        : context_(context)
    {
        WisteriaRenderSessionOptionsV1 options{};
        options.struct_size = sizeof(options);
        options.struct_version = 1U;
        options.force_software = forceSoftware ? 1U : 0U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_render_session_create(
                context_.Handle(),
                &options,
                &handle_
            ),
            "render_session_create"
        );
    }

    ~RenderSession()
    {
        if (handle_ != 0U)
            (void)wisteria_stable_render_session_destroy(
                context_.Handle(),
                handle_
            );
    }

    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;

    RenderSession(RenderSession&& other) noexcept
        : context_(other.context_),
          handle_(std::exchange(other.handle_, 0U))
    {
    }

    RenderSession& operator=(RenderSession&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != 0U)
                (void)wisteria_stable_render_session_destroy(
                    context_.Handle(),
                    handle_
                );
            handle_ = std::exchange(other.handle_, 0U);
        }
        return *this;
    }

    WisteriaRenderSession Handle() const noexcept
    {
        return handle_;
    }

    // Renders the entity's exact runtime state and returns canonical
    // top-left RGBA8 pixels.
    std::vector<std::uint8_t> RenderOffline(
        Entity& entity,
        const RenderCamera& camera,
        std::uint32_t width,
        std::uint32_t height
    )
    {
        WisteriaRenderCameraV1 nativeCamera{};
        nativeCamera.struct_size = sizeof(nativeCamera);
        nativeCamera.struct_version = 1U;
        nativeCamera.position[0] = camera.position[0];
        nativeCamera.position[1] = camera.position[1];
        nativeCamera.position[2] = camera.position[2];
        nativeCamera.target[0] = camera.target[0];
        nativeCamera.target[1] = camera.target[1];
        nativeCamera.target[2] = camera.target[2];
        nativeCamera.up[0] = camera.up[0];
        nativeCamera.up[1] = camera.up[1];
        nativeCamera.up[2] = camera.up[2];
        nativeCamera.vertical_fov_degrees = camera.verticalFovDegrees;
        nativeCamera.near_clip = camera.nearClip;
        nativeCamera.far_clip = camera.farClip;

        std::uint64_t requiredBytes = 0U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_render_session_render(
                context_.Handle(),
                handle_,
                entity.Handle(),
                &nativeCamera,
                width,
                height,
                nullptr,
                &requiredBytes
            ),
            "render_session_render_size"
        );

        std::vector<std::uint8_t> rgba(requiredBytes);
        CheckStatus(
            context_.Handle(),
            wisteria_stable_render_session_render(
                context_.Handle(),
                handle_,
                entity.Handle(),
                &nativeCamera,
                width,
                height,
                rgba.data(),
                &requiredBytes
            ),
            "render_session_render"
        );
        rgba.resize(requiredBytes);
        return rgba;
    }

    std::uint64_t SequenceRange(
        Entity& entity,
        const RenderCamera& camera,
        const std::filesystem::path& outputDirectory,
        std::uint64_t startFrame,
        std::uint64_t endFrame,
        bool writePng = true,
        bool writeRaw = false
    )
    {
        WisteriaRenderCameraV1 nativeCamera{};
        nativeCamera.struct_size = sizeof(nativeCamera);
        nativeCamera.struct_version = 1U;
        nativeCamera.position[0] = camera.position[0];
        nativeCamera.position[1] = camera.position[1];
        nativeCamera.position[2] = camera.position[2];
        nativeCamera.target[0] = camera.target[0];
        nativeCamera.target[1] = camera.target[1];
        nativeCamera.target[2] = camera.target[2];
        nativeCamera.up[0] = camera.up[0];
        nativeCamera.up[1] = camera.up[1];
        nativeCamera.up[2] = camera.up[2];
        nativeCamera.vertical_fov_degrees = camera.verticalFovDegrees;
        nativeCamera.near_clip = camera.nearClip;
        nativeCamera.far_clip = camera.farClip;

        WisteriaSequenceOptionsV1 options{};
        options.struct_size = sizeof(options);
        options.struct_version = 1U;
        options.start_frame = startFrame;
        options.end_frame = endFrame;
        options.write_png = writePng ? 1U : 0U;
        options.write_raw = writeRaw ? 1U : 0U;

        std::uint64_t lastCommitted = 0U;
        CheckStatus(
            context_.Handle(),
            wisteria_stable_render_session_sequence_range(
                context_.Handle(),
                handle_,
                entity.Handle(),
                &nativeCamera,
                outputDirectory.string().c_str(),
                &options,
                &lastCommitted
            ),
            "render_session_sequence_range"
        );
        return lastCommitted;
    }

private:
    Context& context_;
    WisteriaRenderSession handle_ = 0U;
};

}  // namespace wisteria::sdk
