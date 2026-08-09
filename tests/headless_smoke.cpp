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

#include <glad/gl.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

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

    std::printf(
        "[headless-smoke] PASS: EGL lifecycle + FBO readback\n"
    );
    return 0;
}
