#include "wisteria/rendering/framebuffer.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/graphics_device.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/rendering/primitives/cube.hpp"
#include "wisteria/scene/scene.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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

int DarkestPixelSum(GLuint framebufferId)
{
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(TestWidth) * TestHeight * 4U
    );
    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = GL_BACK;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferId);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0,
        0,
        TestWidth,
        TestHeight,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previousReadFramebuffer)
    );
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    int darkest = std::numeric_limits<int>::max();
    for (std::size_t index = 0U; index < pixels.size(); index += 4U)
    {
        const int sum = static_cast<int>(pixels[index]) +
            static_cast<int>(pixels[index + 1U]) +
            static_cast<int>(pixels[index + 2U]);
        darkest = std::min(darkest, sum);
    }
    return darkest;
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

// The demo ground quad. The fixed winding (0,2,1 / 0,3,2) is
// counter-clockwise when viewed from +Y so the plane is front-facing for
// every camera above the ground; the old winding (0,1,2 / 0,2,3) produced a
// -Y geometric normal that back-face culling erased. This helper lets the
// regression test pin that contract down to the pixel level.
DefaultModelData BuildGroundQuad(bool frontFacing)
{
    constexpr std::size_t GroundStride = 15U;  // pos3 color3 uv2 normal3 tangent4
    constexpr float Half = 4.0f;
    DefaultModelData data;
    data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT},
        {"tangent", 4, FLOAT, false, false, 4U}
    };
    const float positions[4][2] = {
        {-Half, -Half},
        {Half, -Half},
        {Half, Half},
        {-Half, Half}
    };
    const float uvs[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    for (int index = 0; index < 4; ++index)
    {
        const float vertex[GroundStride] = {
            positions[index][0], 0.0f, positions[index][1],
            0.75f, 0.75f, 0.75f,
            uvs[index][0], uvs[index][1],
            0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        for (std::size_t component = 0U;
             component < GroundStride;
             ++component)
        {
            data.vertices.push_back(vertex[component]);
        }
    }
    data.indices = frontFacing
        ? std::vector<unsigned int>{0U, 2U, 1U, 0U, 3U, 2U}
        : std::vector<unsigned int>{0U, 1U, 2U, 0U, 2U, 3U};
    return data;
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

        // Ground winding regression: the plane must be front-facing from
        // above, otherwise back-face culling erases it before the ground
        // shadow pass can depth-test against it.
        {
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            Camera camera(CameraParam{
                .Position = {0.0f, 3.0f, 3.0f},
                .Target = {0.0f, 0.0f, 0.0f},
                .Up = {0.0f, 1.0f, 0.0f},
                .VerticalFovDegrees = 45.0f
            });
            const glm::mat4 projection = glm::perspective(
                glm::radians(45.0f),
                1.0f,
                0.1f,
                100.0f
            );
            const glm::vec4 clearColor(0.0f, 0.0f, 0.4f, 1.0f);
            const unsigned char expectedClear[4] = {0U, 0U, 102U, 255U};

            // Correct winding: the center pixel must show the shaded ground,
            // not the clear color.
            {
                Scene scene;
                Mesh& ground = resources.CreateMesh(
                    "test::groundFront",
                    BuildGroundQuad(true)
                );
                MaterialData groundData;
                groundData.textureSources.clear();
                groundData.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
                groundData.groundPlane = true;
                Material& groundMaterial = resources.CreateMaterial(
                    "test::groundFrontMat",
                    groundData
                );
                scene.CreateEntity(ground, groundMaterial);

                sceneFramebuffer.Clear(clearColor);
                Renderer renderer;
                renderer.Render(
                    scene,
                    camera,
                    projection,
                    sceneFramebuffer
                );
                Require(
                    glGetError() == GL_NO_ERROR,
                    "GL error during front-facing ground render"
                );
                unsigned char pixel[4] = {0U, 0U, 0U, 0U};
                ReadPixel(
                    sceneFramebuffer.Id(),
                    GL_COLOR_ATTACHMENT0,
                    pixel
                );
                const bool groundVisible =
                    pixel[0] > 20U || pixel[1] > 20U || pixel[2] > 20U;
                Require(
                    groundVisible,
                    "ground plane was not rendered (winding/culling regression)"
                );
                std::printf(
                    "[RENDER FBO] ground-front pixel=%u,%u,%u,%u\n",
                    pixel[0], pixel[1], pixel[2], pixel[3]
                );
            }

            // The old reversed winding is back-facing from above and must be
            // culled; pinning this documents the exact regression fixed by
            // flipping the index order.
            {
                Scene scene;
                Mesh& ground = resources.CreateMesh(
                    "test::groundBack",
                    BuildGroundQuad(false)
                );
                MaterialData groundData;
                groundData.textureSources.clear();
                groundData.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
                groundData.groundPlane = true;
                Material& groundMaterial = resources.CreateMaterial(
                    "test::groundBackMat",
                    groundData
                );
                scene.CreateEntity(ground, groundMaterial);

                sceneFramebuffer.Clear(clearColor);
                Renderer renderer;
                renderer.Render(
                    scene,
                    camera,
                    projection,
                    sceneFramebuffer
                );
                Require(
                    glGetError() == GL_NO_ERROR,
                    "GL error during back-facing ground render"
                );
                unsigned char pixel[4] = {0U, 0U, 0U, 0U};
                ReadPixel(
                    sceneFramebuffer.Id(),
                    GL_COLOR_ATTACHMENT0,
                    pixel
                );
                ExpectPixel("ground-back-culled", expectedClear, pixel);
            }
        }

        // Ground shadow uniformity regression: overlapping flattened casters
        // must not darken the shadow (per-part alpha accumulation). The pass
        // writes the silhouette into a stencil mask and blends the shadow
        // color exactly once per pixel, so one cube and two exactly
        // overlapping cubes must produce the same darkest pixel.
        {
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            Camera camera(CameraParam{
                .Position = {0.0f, 3.0f, 3.0f},
                .Target = {0.0f, 0.0f, 0.0f},
                .Up = {0.0f, 1.0f, 0.0f},
                .VerticalFovDegrees = 45.0f
            });
            const glm::mat4 projection = glm::perspective(
                glm::radians(45.0f),
                1.0f,
                0.1f,
                100.0f
            );
            const glm::vec4 clearColor(0.4f, 0.5f, 0.6f, 1.0f);

            Mesh& ground = resources.CreateMesh(
                "test::uniformGround",
                BuildGroundQuad(true)
            );
            MaterialData groundData;
            groundData.textureSources.clear();
            groundData.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
            groundData.groundPlane = true;
            Material& groundMaterial = resources.CreateMaterial(
                "test::uniformGroundMat",
                groundData
            );

            Mesh& cubeMesh = resources.CreateMesh(
                "test::uniformCube",
                cubeData
            );
            MaterialData cubeData;
            cubeData.textureSources.clear();
            cubeData.baseColorFactor = {0.95f, 0.8f, 0.7f, 1.0f};
            cubeData.castSelfShadow = true;
            cubeData.receiveSelfShadow = true;
            cubeData.groundShadow = true;
            Material& cubeMaterial = resources.CreateMaterial(
                "test::uniformCubeMat",
                cubeData
            );

            const Transform cubeTransform(glm::vec3(0.0f, 2.0f, 0.0f));
            Renderer renderer;

            Scene singleCasterScene;
            singleCasterScene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = {1.0f, 0.96f, 0.92f},
                .Intensity = 1.0f
            });
            singleCasterScene.CreateEntity(ground, groundMaterial);
            singleCasterScene.CreateEntity(
                cubeMesh,
                cubeMaterial,
                cubeTransform
            );
            sceneFramebuffer.Clear(clearColor);
            renderer.Render(
                singleCasterScene,
                camera,
                projection,
                sceneFramebuffer
            );
            Require(
                glGetError() == GL_NO_ERROR,
                "GL error during single-caster ground shadow render"
            );
            const int darkestSingle = DarkestPixelSum(
                sceneFramebuffer.Id()
            );

            Scene overlappingCastersScene;
            overlappingCastersScene.CreateDirectionalLight(
                DirectionalLightData{
                    .Direction = {-0.35f, -0.75f, -0.45f},
                    .Color = {1.0f, 0.96f, 0.92f},
                    .Intensity = 1.0f
                }
            );
            overlappingCastersScene.CreateEntity(ground, groundMaterial);
            overlappingCastersScene.CreateEntity(
                cubeMesh,
                cubeMaterial,
                cubeTransform
            );
            overlappingCastersScene.CreateEntity(
                cubeMesh,
                cubeMaterial,
                cubeTransform
            );
            sceneFramebuffer.Clear(clearColor);
            renderer.Render(
                overlappingCastersScene,
                camera,
                projection,
                sceneFramebuffer
            );
            Require(
                glGetError() == GL_NO_ERROR,
                "GL error during overlapping-caster ground shadow render"
            );
            const int darkestOverlap = DarkestPixelSum(
                sceneFramebuffer.Id()
            );

            Require(
                std::abs(darkestSingle - darkestOverlap) <= 2,
                "overlapping ground-shadow casters changed shadow darkness "
                "(per-part alpha accumulation regression)"
            );
            std::printf(
                "[RENDER FBO] shadow-darkest single=%d overlap=%d\n",
                darkestSingle,
                darkestOverlap
            );
        }
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
        "[RENDER FBO PASS] clear/readback, winding and shadow uniformity\n"
    );
    return 0;
}
