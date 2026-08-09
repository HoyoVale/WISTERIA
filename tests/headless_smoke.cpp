// R1.7 Phase 0B: wisteria_headless_smoke.
//
// Proves WISTERIA can own a GL context with no window system:
//   - no GLFW window is created
//   - DISPLAY / WAYLAND_DISPLAY are not required
//   - EGL provider: MakeCurrent -> GL loader -> minimal FBO -> clear ->
//     glReadPixels verification
//   - EGL lifecycle: Create -> Current -> Release -> Current -> Destroy
//
// Exit codes: 0 = PASS, 1 = probe failure, 2 = usage error.

#include "wisteria/platform/headless_context.hpp"
#include "wisteria/rendering/graphics_device.hpp"
#include "wisteria/rendering/headless_render_session.hpp"
#include "wisteria/rendering/light.hpp"
#include "wisteria/scene/offline_frame_sequence.hpp"
#include "wisteria/scene/scene.hpp"

#include <glad/gl.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
void PrintUsage(const char* program)
{
    std::printf(
        "Usage: %s [--software]\n"
        "  --software   force software EGL device (llvmpipe/softpipe);\n"
        "               fails explicitly when unavailable\n",
        program
    );
}

void PrintDiagnostics(const wisteria::IHeadlessContext& context)
{
    std::printf(
        "[headless-smoke] provider=%s platform=%s egl=%s (%s) "
        "gl=%s renderer=%s version=%s software=%s\n",
        std::string(context.ProviderName()).c_str(),
        std::string(context.PlatformName()).c_str(),
        std::string(context.EglVersion()).c_str(),
        std::string(context.EglVendor()).c_str(),
        std::string(context.Vendor()).c_str(),
        std::string(context.Renderer()).c_str(),
        std::string(context.Version()).c_str(),
        context.IsSoftware() ? "yes" : "no"
    );
}

// Minimal FBO probe: clear the engine-style RGBA8 target and read one pixel
// back. This is exactly the GPU operation RenderOffline depends on.
bool RenderProbe(wisteria::IHeadlessContext& context, float r, float g, float b)
{
    context.MakeCurrent();

    GLuint texture = 0U;
    GLuint framebuffer = 0U;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        4,
        4,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        texture,
        0
    );

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: framebuffer incomplete (0x%04x)\n",
            status
        );
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        return false;
    }

    glViewport(0, 0, 4, 4);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    unsigned char pixel[4] = {0U, 0U, 0U, 0U};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    const unsigned char expected[4] = {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        255U
    };
    const bool matched =
        pixel[0] == expected[0] &&
        pixel[1] == expected[1] &&
        pixel[2] == expected[2] &&
        pixel[3] == expected[3];

    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);

    if (!matched)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: readback pixel = %u,%u,%u,%u "
            "expected %u,%u,%u,%u\n",
            pixel[0], pixel[1], pixel[2], pixel[3],
            expected[0], expected[1], expected[2], expected[3]
        );
    }
    return matched;
}

// R1.7 Phase 0D: zero-window session compatibility probe. A real Scene
// (Box.glb + directional light) is rendered through HeadlessRenderSession's
// renderer into SceneFramebuffer -> RGBA8. The frame must be non-black and
// opaque, proving the full engine render path works without any window.
bool RunSessionProbe(bool forceSoftware)
{
    wisteria::HeadlessContextOptions options;
    options.forceSoftware = forceSoftware;
    auto context = wisteria::CreateHeadlessContext(options);
    if (context == nullptr)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: session context creation\n"
        );
        return false;
    }

    try
    {
        wisteria::HeadlessRenderSession session(std::move(context));
        wisteria::ModelAsset& box = session.GetResources().LoadModel(
            "r17::sessionBox",
            "tests/assets/models/Box.glb"
        );

        wisteria::Scene scene;
        scene.CreateDirectionalLight(wisteria::DirectionalLightData{
            .Direction = {-0.35f, -0.75f, -0.45f},
            .Color = glm::vec3(1.0f, 0.96f, 0.92f),
            .Intensity = 1.0f
        });
        scene.InstantiateModel(box);

        wisteria::OfflineRenderRequest request;
        request.width = 64U;
        request.height = 64U;
        request.camera = wisteria::Camera(wisteria::CameraParam{
            .Position = {0.0f, 3.0f, 3.0f},
            .Target = {0.0f, 0.0f, 0.0f},
            .Up = {0.0f, 1.0f, 0.0f},
            .VerticalFovDegrees = 45.0f
        });
        request.projection = glm::perspective(
            glm::radians(45.0f),
            1.0f,
            0.1f,
            100.0f
        );

        const wisteria::Rgba8Frame frame =
            session.RenderOffline(scene, request);
        const bool sizeOk =
            frame.width == request.width &&
            frame.height == request.height &&
            frame.pixels.size() ==
                static_cast<std::size_t>(frame.width) *
                    static_cast<std::size_t>(frame.height) * 4U;
        bool nonBlack = false;
        bool opaque = true;
        if (sizeOk)
        {
            for (std::size_t index = 0U; index < frame.pixels.size();
                 index += 4U)
            {
                if (frame.pixels[index] != 0U ||
                    frame.pixels[index + 1U] != 0U ||
                    frame.pixels[index + 2U] != 0U)
                {
                    nonBlack = true;
                }
                if (frame.pixels[index + 3U] != 255U)
                    opaque = false;
            }
        }
        if (!sizeOk || !nonBlack || !opaque)
        {
            std::fprintf(
                stderr,
                "[headless-smoke] FAIL: session render probe "
                "(size=%u, nonBlack=%d, opaque=%d)\n",
                sizeOk ? frame.width : 0U,
                nonBlack ? 1 : 0,
                opaque ? 1 : 0
            );
            return false;
        }
        std::printf(
            "[headless-smoke] session probe PASS "
            "(provider=%s platform=%s renderer=%s)\n",
            std::string(session.GetContext().ProviderName()).c_str(),
            std::string(session.GetContext().PlatformName()).c_str(),
            std::string(session.GetContext().Renderer()).c_str()
        );
        return true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: session probe exception: %s\n",
            error.what()
        );
        return false;
    }
}

// R1.7 Phase 0D + R1.8 Phase 0D: OfflineFrameSequence on a zero-window
// session. Renders a deterministic 0..2 sequence through the GENERIC runtime
// (animated_triangle.gltf + visible Box.glb) and verifies PNGs, manifest
// and A/B checkpoints, proving the sequence orchestration is backend-neutral.
bool RunSequenceProbe(bool forceSoftware)
{
    wisteria::HeadlessContextOptions options;
    options.forceSoftware = forceSoftware;
    auto context = wisteria::CreateHeadlessContext(options);
    if (context == nullptr)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: sequence context creation\n"
        );
        return false;
    }

    try
    {
        wisteria::HeadlessRenderSession session(std::move(context));
        wisteria::ResourceManager& resources = session.GetResources();

        wisteria::ModelAsset& genericModel = resources.LoadModel(
            "r17::seqGeneric",
            "tests/data/animated_triangle.gltf"
        );
        wisteria::ModelAsset& boxModel = resources.LoadModel(
            "r17::seqBox",
            "tests/assets/models/Box.glb"
        );

        wisteria::Scene scene;
        scene.CreateDirectionalLight(wisteria::DirectionalLightData{
            .Direction = {-0.35f, -0.75f, -0.45f},
            .Color = glm::vec3(1.0f, 0.96f, 0.92f),
            .Intensity = 1.0f
        });
        wisteria::Entity& entity = scene.InstantiateModel(genericModel);
        scene.InstantiateModel(boxModel);

        wisteria::ModelInstance& instance = entity.GetModelInstance();
        wisteria::IModelRuntimeDriver* runtime =
            instance.TryGetRuntime();
        if (runtime == nullptr ||
            !runtime->Capabilities().deterministic.supportsExactFrameStepping)
        {
            std::fprintf(
                stderr,
                "[headless-smoke] FAIL: sequence lost the generic "
                "deterministic runtime\n"
            );
            return false;
        }

        wisteria::OfflineRenderRequest baseRequest;
        baseRequest.width = 64U;
        baseRequest.height = 64U;
        baseRequest.camera = wisteria::Camera(wisteria::CameraParam{
            .Position = {0.0f, 3.0f, 3.0f},
            .Target = {0.0f, 0.0f, 0.0f},
            .Up = {0.0f, 1.0f, 0.0f},
            .VerticalFovDegrees = 45.0f
        });
        baseRequest.projection = glm::perspective(
            glm::radians(45.0f),
            1.0f,
            0.1f,
            100.0f
        );

        const std::filesystem::path outputDir =
            std::filesystem::temp_directory_path() /
            "wisteria_headless_seq";
        std::error_code ignored;
        std::filesystem::remove_all(outputDir, ignored);

        wisteria::OfflineFrameSequenceConfig config;
        config.outputDirectory = outputDir;
        config.renderRequest = baseRequest;
        config.writePng = true;
        config.writeRaw = false;
        config.overwritePolicy = wisteria::SequenceOverwritePolicy::Reject;

        wisteria::OfflineFrameSequence sequence(
            scene,
            session.GetRenderer(),
            *runtime,
            instance,
            config
        );
        session.MakeCurrent();
        sequence.RenderRange(0U, 2U);

        const bool committed =
            !sequence.Failed() &&
            sequence.LastCommittedFrame().value_or(99U) == 2U;
        const bool persisted =
            std::filesystem::is_regular_file(outputDir / "00000000.png") &&
            std::filesystem::is_regular_file(outputDir / "00000002.png") &&
            std::filesystem::is_regular_file(outputDir / "manifest.jsonl") &&
            std::filesystem::is_regular_file(outputDir / "checkpoint-A.bin") &&
            std::filesystem::is_regular_file(outputDir / "checkpoint-B.bin");
        std::filesystem::remove_all(outputDir, ignored);

        if (!committed || !persisted)
        {
            std::fprintf(
                stderr,
                "[headless-smoke] FAIL: sequence probe "
                "(committed=%d persisted=%d)\n",
                committed ? 1 : 0,
                persisted ? 1 : 0
            );
            return false;
        }
        std::printf(
            "[headless-smoke] sequence probe PASS "
            "(frames 0..2, manifest + A/B checkpoints)\n"
        );
        return true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: sequence probe exception: %s\n",
            error.what()
        );
        return false;
    }
}
}  // namespace

int main(int argumentCount, char* arguments[])
{
    wisteria::HeadlessContextOptions options;
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::strcmp(arguments[index], "--software") == 0)
        {
            options.forceSoftware = true;
        }
        else if (std::strcmp(arguments[index], "--help") == 0)
        {
            PrintUsage(arguments[0]);
            return 0;
        }
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", arguments[index]);
            PrintUsage(arguments[0]);
            return 2;
        }
    }

    // EGL lifecycle: Create -> Current -> Release -> Current -> Destroy.
    std::unique_ptr<wisteria::IHeadlessContext> context =
        wisteria::CreateHeadlessContext(options);
    if (context == nullptr)
    {
        std::fprintf(stderr, "[headless-smoke] FAIL: context creation\n");
        return 1;
    }
    // Final Micro Fix invariant: the factory must not leave a native context
    // current or any tracker registered.
    if (wisteria::GraphicsDevice::CurrentContext() != nullptr ||
        wisteria::GraphicsDevice::CurrentShareGroup() != nullptr)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: factory leaked current tracker state\n"
        );
        return 1;
    }
    context->MakeCurrent();
    if (wisteria::GraphicsDevice::CurrentContext() !=
            context->ContextToken() ||
        wisteria::GraphicsDevice::CurrentShareGroup() !=
            context->ShareGroupToken())
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: MakeCurrent did not register both "
            "identities\n"
        );
        return 1;
    }
    context->ReleaseCurrent();
    if (wisteria::GraphicsDevice::CurrentContext() != nullptr ||
        wisteria::GraphicsDevice::CurrentShareGroup() != nullptr)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: ReleaseCurrent leaked tracker state\n"
        );
        return 1;
    }
    context->MakeCurrent();
    context.reset();

    // Recreate and run the render probe on a fresh context.
    context = wisteria::CreateHeadlessContext(options);
    if (context == nullptr)
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: second context creation\n"
        );
        return 1;
    }
    if (options.forceSoftware && !context->IsSoftware())
    {
        std::fprintf(
            stderr,
            "[headless-smoke] FAIL: forceSoftware did not yield a software "
            "renderer (renderer=%s)\n",
            std::string(context->Renderer()).c_str()
        );
        return 1;
    }
    PrintDiagnostics(*context);

    if (!RenderProbe(*context, 1.0f, 0.0f, 0.0f))
        return 1;
    if (!RenderProbe(*context, 0.0f, 1.0f, 0.0f))
        return 1;

    context->ReleaseCurrent();
    context.reset();

    if (!RunSessionProbe(options.forceSoftware))
        return 1;
    if (!RunSequenceProbe(options.forceSoftware))
        return 1;

    std::printf(
        "[headless-smoke] PASS: lifecycle + FBO + session + sequence "
        "(zero window)\n"
    );
    return 0;
}
