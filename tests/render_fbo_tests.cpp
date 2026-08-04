#include "wisteria/rendering/framebuffer.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

using namespace wisteria;

namespace
{
constexpr int TestWidth = 64;
constexpr int TestHeight = 64;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void ReadPixel(
    GLuint framebufferId,
    GLenum readBuffer,
    unsigned char rgba[4]
)
{
    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = GL_BACK;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferId);
    glReadBuffer(readBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        TestWidth / 2,
        TestHeight / 2,
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    );

    // R0.2 regression: readback must not corrupt the caller's GL state.
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previousReadFramebuffer)
    );
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
}

void ExpectPixel(
    const char* stage,
    const unsigned char expected[4],
    const unsigned char actual[4]
)
{
    if (std::memcmp(expected, actual, 4) != 0)
    {
        std::fprintf(
            stderr,
            "[RENDER FBO FAIL] %s expected=%u,%u,%u,%u actual=%u,%u,%u,%u\n",
            stage,
            expected[0], expected[1], expected[2], expected[3],
            actual[0], actual[1], actual[2], actual[3]
        );
        throw std::runtime_error(std::string(stage) + " pixel mismatch");
    }
    std::printf(
        "[RENDER FBO] %s = %u,%u,%u,%u\n",
        stage,
        actual[0], actual[1], actual[2], actual[3]
    );
}
}

// Minimal render regression: create a real OpenGL context, clear the engine
// SceneFramebuffer red in frame 1 and green in frame 2, and read both pixels
// back. This exercises framebuffer creation, clear state, texture attachment
// and read-buffer state without loading any model or running MMD physics.
int main()
{
    if (!glfwInit())
    {
        // GLFW built without any platform backend (Linux NULL) cannot even
        // initialize. Treat it like the window-creation skip below so
        // headless CTest runs stay green.
        std::fprintf(stderr, "[RENDER FBO] glfwInit failed\n");
        std::printf(
            "[RENDER FBO] SKIPPED: no GLFW platform backend available\n"
        );
        return 0;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(
        TestWidth,
        TestHeight,
        "wisteria render fbo test",
        nullptr,
        nullptr
    );
    if (window == nullptr)
    {
        // No display backend is available (for example GLFW built with the
        // Linux NULL backend). Headless builds must still pass CTest.
        glfwTerminate();
        std::printf(
            "[RENDER FBO] SKIPPED: no GLFW display backend available\n"
        );
        return 0;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::fprintf(
            stderr,
            "[RENDER FBO] failed to load OpenGL functions\n"
        );
        glfwDestroyWindow(window);
        glfwTerminate();
        return 2;
    }

    try
    {
        SceneFramebuffer sceneFramebuffer;
        sceneFramebuffer.Resize(TestWidth, TestHeight);
        Require(sceneFramebuffer.IsValid(), "SceneFramebuffer is not valid");
        Require(
            sceneFramebuffer.Id() != 0,
            "SceneFramebuffer FBO id is zero"
        );
        Require(
            sceneFramebuffer.ColorTexture() != 0,
            "SceneFramebuffer color texture is zero"
        );

        // Frame 1: clear red, read back the exact color.
        sceneFramebuffer.Clear(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        unsigned char red[4] = {0U, 0U, 0U, 0U};
        ReadPixel(sceneFramebuffer.Id(), GL_COLOR_ATTACHMENT0, red);
        const unsigned char expectedRed[4] = {255U, 0U, 0U, 255U};
        ExpectPixel("frame1-red", expectedRed, red);

        // Frame 2: clear green. The same framebuffer must be reusable and
        // must not retain the previous clear color.
        sceneFramebuffer.Clear(glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        unsigned char green[4] = {0U, 0U, 0U, 0U};
        ReadPixel(sceneFramebuffer.Id(), GL_COLOR_ATTACHMENT0, green);
        const unsigned char expectedGreen[4] = {0U, 255U, 0U, 255U};
        ExpectPixel("frame2-green", expectedGreen, green);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[RENDER FBO FAIL] %s\n", error.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf(
        "[RENDER FBO PASS] two-frame SceneFramebuffer clear/readback\n"
    );
    return 0;
}
