#include "wisteria/rendering/framebuffer.hpp"
#include "wisteria/rendering/frame_readback.hpp"
#include "wisteria/rendering/offline_render.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/runtime/model_instance.hpp"
#include "wisteria/scene/offline_frame_sequence.hpp"
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
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
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

        // GraphicsDevice deferred deletion: GPU objects destroyed without
        // the owning context current must be queued, not passed to glDelete*
        // with no context, and released once the context returns.
        {
            GraphicsDevice deferredDevice;
            deferredDevice.SetShareGroupToken(window);
            GraphicsDevice::SetCurrentShareGroup(window);
            {
                Texture immediate(
                    TextureData::FromRgba8(
                        2,
                        2,
                        std::vector<std::uint8_t>(16U, 255U)
                    ),
                    &deferredDevice
                );
                immediate.Attach();
            }
            Require(
                deferredDevice.PendingDeleteCount() == 0U,
                "device must delete immediately with its context current"
            );

            GraphicsDevice::SetCurrentShareGroup(nullptr);
            {
                Texture queued(
                    TextureData::FromRgba8(
                        2,
                        2,
                        std::vector<std::uint8_t>(16U, 255U)
                    ),
                    &deferredDevice
                );
                queued.Attach();
            }
            Require(
                deferredDevice.PendingDeleteCount() == 1U,
                "device must queue deletions without its context current"
            );
            GraphicsDevice::SetCurrentShareGroup(window);
            deferredDevice.FlushPendingDeletes();
            Require(
                deferredDevice.PendingDeleteCount() == 0U,
                "flush must release queued objects"
            );
            std::printf(
                "[RENDER FBO] deferred-delete queue flushed\n"
            );
        }

        // R1.7 Final Fix: two-context share-group ownership matrix.
        // Context A and context B belong to one share group, but each has its
        // own namespace for context-local objects (VAO/FBO).
        {
            GLFWwindow* sharedWindow = glfwCreateWindow(
                4,
                4,
                "wisteria shared context B",
                nullptr,
                window
            );
            Require(
                sharedWindow != nullptr,
                "second shared GLFW context creation failed"
            );

            GraphicsDevice ownershipDevice;
            // One share-group identity for both contexts.
            ownershipDevice.SetShareGroupToken(window);

            // Context A: create one shared object and two context-local ones.
            glfwMakeContextCurrent(window);
            GraphicsDevice::SetCurrentContext(window);
            GraphicsDevice::SetCurrentShareGroup(window);
            GLuint textureA = 0U;
            GLuint vertexArrayA = 0U;
            GLuint framebufferA = 0U;
            glGenTextures(1, &textureA);
            glGenVertexArrays(1, &vertexArrayA);
            glGenFramebuffers(1, &framebufferA);
            Require(
                textureA != 0U && vertexArrayA != 0U &&
                    framebufferA != 0U,
                "ownership matrix object creation failed"
            );

            // Switch to B: same share group, different context.
            glfwMakeContextCurrent(sharedWindow);
            GraphicsDevice::SetCurrentContext(sharedWindow);
            GraphicsDevice::SetCurrentShareGroup(window);

            // Shared texture: legal to delete from any share-group context.
            ownershipDevice.DeleteResource(
                GraphicsDevice::ResourceKind::Texture,
                textureA
            );
            Require(
                ownershipDevice.PendingDeleteCount() == 0U,
                "shared texture must delete immediately on a sibling "
                "context"
            );

            // Context-local VAO/FBO: must queue, never delete on B.
            ownershipDevice.DeleteResource(
                GraphicsDevice::ResourceKind::VertexArray,
                vertexArrayA,
                window
            );
            ownershipDevice.DeleteResource(
                GraphicsDevice::ResourceKind::Framebuffer,
                framebufferA,
                window
            );
            Require(
                ownershipDevice.PendingDeleteCount() == 2U,
                "context-local objects must queue on a sibling context"
            );

            // Flushing while B is current must not release A's local objects.
            ownershipDevice.FlushPendingDeletes();
            Require(
                ownershipDevice.PendingDeleteCount() == 2U,
                "sibling context must not flush context-local queue"
            );

            // Back on the owning context A: flush releases both.
            glfwMakeContextCurrent(window);
            GraphicsDevice::SetCurrentContext(window);
            GraphicsDevice::SetCurrentShareGroup(window);
            ownershipDevice.FlushPendingDeletes();
            Require(
                ownershipDevice.PendingDeleteCount() == 0U,
                "owning context must flush context-local queue"
            );

            // Destroying the current context leaves both trackers empty.
            glfwMakeContextCurrent(sharedWindow);
            GraphicsDevice::SetCurrentContext(sharedWindow);
            GraphicsDevice::SetCurrentShareGroup(window);
            glfwDestroyWindow(sharedWindow);
            GraphicsDevice::SetCurrentContext(nullptr);
            GraphicsDevice::SetCurrentShareGroup(nullptr);
            Require(
                GraphicsDevice::CurrentContext() == nullptr &&
                    GraphicsDevice::CurrentShareGroup() == nullptr,
                "trackers must be empty after destroying the context"
            );

            // Restore context A for the remaining tests.
            glfwMakeContextCurrent(window);
            GraphicsDevice::SetCurrentContext(window);
            GraphicsDevice::SetCurrentShareGroup(window);
            std::printf(
                "[RENDER FBO] two-context share-group ownership matrix\n"
            );
        }

        // Renderer VAO cache: a mesh destroyed while a VAO is cached must not
        // dangle; a later mesh at the same address rebuilds the VAO.
        {
            GraphicsDevice cacheDevice;
            ResourceManager cacheResources;
            cacheResources.BindGraphicsDevice(cacheDevice);
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
            Renderer renderer;
            std::optional<Mesh> mesh;

            auto renderMesh = [&]()
            {
                Scene scene;
                MaterialData groundData;
                groundData.textureSources.clear();
                groundData.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
                groundData.groundPlane = true;
                Material groundMaterial(
                    groundData,
                    std::make_shared<ProgramCache>(),
                    &cacheDevice
                );
                scene.CreateEntity(*mesh, groundMaterial);
                sceneFramebuffer.Clear(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
                renderer.Render(
                    scene,
                    camera,
                    projection,
                    sceneFramebuffer
                );
                Require(
                    glGetError() == GL_NO_ERROR,
                    "GL error during mesh cache lifetime render"
                );
            };

            mesh.emplace(
                BuildGroundQuad(true),
                0U,
                std::vector<MeshMorphTarget>{},
                std::vector<std::uint32_t>{},
                &cacheDevice
            );
            renderMesh();
            mesh.reset();
            mesh.emplace(
                BuildGroundQuad(true),
                0U,
                std::vector<MeshMorphTarget>{},
                std::vector<std::uint32_t>{},
                &cacheDevice
            );
            renderMesh();
            std::printf(
                "[RENDER FBO] mesh lifetime cache rebuilt\n"
            );
        }

        // Imported PBR model renders end-to-end (R1-07): a glTF quad with a
        // baseColorFactor material must draw through the assimp -> PBR ->
        // basicTex chain, not just the programmatic ground/cube path.
        {
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            const std::filesystem::path modelPath =
                std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
                "pbr_quad.gltf";
            Require(
                std::filesystem::is_regular_file(modelPath),
                "pbr_quad.gltf fixture is missing"
            );
            ModelAsset& model = resources.LoadModel(
                "test::pbrQuad",
                modelPath
            );
            Scene scene;
            scene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = {1.0f, 0.96f, 0.92f},
                .Intensity = 1.0f
            });
            scene.InstantiateModel(model);
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
            sceneFramebuffer.Clear(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
            Renderer renderer;
            renderer.Render(
                scene,
                camera,
                projection,
                sceneFramebuffer
            );
            Require(
                glGetError() == GL_NO_ERROR,
                "GL error during imported PBR render"
            );
            unsigned char pixel[4] = {0U, 0U, 0U, 0U};
            ReadPixel(sceneFramebuffer.Id(), GL_COLOR_ATTACHMENT0, pixel);
            const bool orangeQuadVisible =
                pixel[0] > 100U &&
                pixel[0] > pixel[1] &&
                pixel[0] > pixel[2];
            Require(
                orangeQuadVisible,
                "imported PBR quad did not render with its base color"
            );
            std::printf(
                "[RENDER FBO] imported-pbr pixel=%u,%u,%u,%u\n",
                pixel[0], pixel[1], pixel[2], pixel[3]
            );
        }

        // R1.6 Phase 0B: ReadbackRgba8 canonical dimensions, color and GL
        // state preservation (read FBO / read buffer / pack alignment /
        // PBO / pack row/skip / viewport untouched).
        {
            SceneFramebuffer readbackTarget;
            readbackTarget.Resize(4, 4);
            readbackTarget.Clear(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

            GLint initialViewport[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, initialViewport);
            glViewport(1, 2, 3, 4);

            GLuint pixelPackBuffer = 0U;
            glGenBuffers(1, &pixelPackBuffer);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelPackBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 8);
            glPixelStorei(GL_PACK_ROW_LENGTH, 7);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
            glPixelStorei(GL_PACK_SKIP_ROWS, 1);

            GLint expectedReadFramebuffer = 0;
            GLint expectedReadBuffer = GL_BACK;
            GLint expectedPackAlignment = 0;
            GLint expectedPixelPackBuffer = 0;
            GLint expectedPackRowLength = 0;
            GLint expectedPackSkipPixels = 0;
            GLint expectedPackSkipRows = 0;
            glGetIntegerv(
                GL_READ_FRAMEBUFFER_BINDING,
                &expectedReadFramebuffer
            );
            glGetIntegerv(GL_READ_BUFFER, &expectedReadBuffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &expectedPackAlignment);
            glGetIntegerv(
                GL_PIXEL_PACK_BUFFER_BINDING,
                &expectedPixelPackBuffer
            );
            glGetIntegerv(GL_PACK_ROW_LENGTH, &expectedPackRowLength);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &expectedPackSkipPixels);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &expectedPackSkipRows);

            const Rgba8Frame stateFrame = ReadbackRgba8(readbackTarget);
            Require(
                stateFrame.width == 4U &&
                    stateFrame.height == 4U &&
                    stateFrame.pixels.size() == 4U * 4U * 4U,
                "ReadbackRgba8 returned wrong dimensions or size"
            );
            Require(
                stateFrame.pixels[0] == 255U &&
                    stateFrame.pixels[1] == 0U &&
                    stateFrame.pixels[2] == 0U,
                "ReadbackRgba8 did not preserve the clear color"
            );

            GLint actualReadFramebuffer = 0;
            GLint actualReadBuffer = GL_BACK;
            GLint actualPackAlignment = 0;
            GLint actualPixelPackBuffer = 0;
            GLint actualPackRowLength = 0;
            GLint actualPackSkipPixels = 0;
            GLint actualPackSkipRows = 0;
            GLint actualViewport[4] = {0, 0, 0, 0};
            glGetIntegerv(
                GL_READ_FRAMEBUFFER_BINDING,
                &actualReadFramebuffer
            );
            glGetIntegerv(GL_READ_BUFFER, &actualReadBuffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &actualPackAlignment);
            glGetIntegerv(
                GL_PIXEL_PACK_BUFFER_BINDING,
                &actualPixelPackBuffer
            );
            glGetIntegerv(GL_PACK_ROW_LENGTH, &actualPackRowLength);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &actualPackSkipPixels);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &actualPackSkipRows);
            glGetIntegerv(GL_VIEWPORT, actualViewport);
            Require(
                actualReadFramebuffer == expectedReadFramebuffer &&
                    actualReadBuffer == expectedReadBuffer &&
                    actualPackAlignment == expectedPackAlignment &&
                    actualPixelPackBuffer == expectedPixelPackBuffer &&
                    actualPackRowLength == expectedPackRowLength &&
                    actualPackSkipPixels == expectedPackSkipPixels &&
                    actualPackSkipRows == expectedPackSkipRows &&
                    actualViewport[0] == 1 &&
                    actualViewport[1] == 2 &&
                    actualViewport[2] == 3 &&
                    actualViewport[3] == 4,
                "ReadbackRgba8 leaked GL readback or viewport state"
            );

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glDeleteBuffers(1, &pixelPackBuffer);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glViewport(
                initialViewport[0],
                initialViewport[1],
                initialViewport[2],
                initialViewport[3]
            );
        }

        // R1.6 Phase 0B: top-left orientation contract (top red, bottom
        // green on a 4x4 target).
        {
            SceneFramebuffer orientationTarget;
            orientationTarget.Resize(4, 4);
            orientationTarget.Clear(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

            const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
            GLboolean previousColorMask[4] = {
                GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE
            };
            glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
            orientationTarget.Bind();
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, 4, 2);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            if (previousScissor == GL_FALSE)
                glDisable(GL_SCISSOR_TEST);
            glColorMask(
                previousColorMask[0],
                previousColorMask[1],
                previousColorMask[2],
                previousColorMask[3]
            );

            const Rgba8Frame orientation = ReadbackRgba8(orientationTarget);
            Require(
                orientation.pixels[0] == 255U &&
                    orientation.pixels[1] == 0U &&
                    orientation.pixels[2] == 0U,
                "Top-left pixel is not red"
            );
            const std::size_t bottomLeft =
                (static_cast<std::size_t>(orientation.height) - 1U) *
                static_cast<std::size_t>(orientation.width) * 4U;
            Require(
                orientation.pixels[bottomLeft] == 0U &&
                    orientation.pixels[bottomLeft + 1U] == 255U &&
                    orientation.pixels[bottomLeft + 2U] == 0U,
                "Bottom-left pixel is not green"
            );
        }

        // R1.6 Phase 0B: static asset offscreen render + readback + viewport
        // guard (P1-4) + repeatable readback.
        {
            const std::filesystem::path boxPath =
                std::filesystem::path("tests") / "assets" / "models" /
                "Box.glb";
            Require(
                std::filesystem::is_regular_file(boxPath),
                "Box.glb fixture is missing"
            );
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            ModelAsset& boxModel = resources.LoadModel("r16::box", boxPath);
            Scene scene;
            scene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = {1.0f, 0.96f, 0.92f},
                .Intensity = 1.0f
            });
            scene.InstantiateModel(boxModel);
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
            SceneFramebuffer target;
            target.Resize(TestWidth, TestHeight);
            Renderer renderer;

            target.Clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            glViewport(3, 4, 5, 6);
            renderer.Render(scene, camera, projection, target);
            GLint viewportAfter[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, viewportAfter);
            Require(
                viewportAfter[0] == 3 && viewportAfter[1] == 4 &&
                    viewportAfter[2] == 5 && viewportAfter[3] == 6,
                "Renderer::Render leaked viewport state (P1-4)"
            );

            const Rgba8Frame frame = ReadbackRgba8(target);
            Require(
                frame.width == TestWidth &&
                    frame.height == TestHeight &&
                    frame.pixels.size() ==
                        static_cast<std::size_t>(TestWidth) * TestHeight * 4U,
                "Offscreen static readback dimensions are wrong"
            );
            bool anyRgbNonZero = false;
            for (std::size_t index = 0U;
                 index + 2U < frame.pixels.size();
                 index += 4U)
            {
                if (frame.pixels[index] != 0U ||
                    frame.pixels[index + 1U] != 0U ||
                    frame.pixels[index + 2U] != 0U)
                {
                    anyRgbNonZero = true;
                    break;
                }
            }
            Require(
                anyRgbNonZero,
                "Static offscreen render contains no rendered RGB content"
            );
            const Rgba8Frame repeated = ReadbackRgba8(target);
            Require(
                repeated.pixels == frame.pixels,
                "Repeated offscreen readback differs"
            );
        }

        // R1.6 Phase 0B: Generic runtime (animated triangle) enters the same
        // offscreen output boundary and motion changes the pixels.
        {
            const std::filesystem::path trianglePath =
                std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
                "animated_triangle.gltf";
            Require(
                std::filesystem::is_regular_file(trianglePath),
                "animated_triangle.gltf fixture is missing"
            );
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            ModelAsset& triangleModel = resources.LoadModel(
                "r16::animatedTriangle",
                trianglePath
            );
            Scene scene;
            scene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = {1.0f, 0.96f, 0.92f},
                .Intensity = 1.0f
            });
            scene.InstantiateModel(triangleModel);
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
            SceneFramebuffer target;
            target.Resize(TestWidth, TestHeight);
            Renderer renderer;

            scene.Update(0.25f);
            target.Clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            renderer.Render(scene, camera, projection, target);
            const Rgba8Frame firstFrame = ReadbackRgba8(target);
            Require(
                firstFrame.pixels.size() ==
                    static_cast<std::size_t>(TestWidth) * TestHeight * 4U,
                "Generic offscreen readback dimensions are wrong"
            );
            bool anyRgbNonZero = false;
            for (std::size_t index = 0U;
                 index + 2U < firstFrame.pixels.size();
                 index += 4U)
            {
                if (firstFrame.pixels[index] != 0U ||
                    firstFrame.pixels[index + 1U] != 0U ||
                    firstFrame.pixels[index + 2U] != 0U)
                {
                    anyRgbNonZero = true;
                    break;
                }
            }
            Require(
                anyRgbNonZero,
                "Generic offscreen render contains no rendered RGB content"
            );

            scene.Update(0.5f);
            target.Clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            renderer.Render(scene, camera, projection, target);
            const Rgba8Frame movedFrame = ReadbackRgba8(target);
            Require(
                movedFrame.pixels != firstFrame.pixels,
                "Generic animation did not change offscreen pixels"
            );
        }

        // R1.6 Phase 0C: Saba material morph reaches the unified renderer
        // through LastRenderFrameView (resolved MaterialRuntimeOverride),
        // and the morph changes the offscreen pixels. The CORE fixture is
        // textureless, so the UV channel is proven at the render-view level
        // (integration bridge test) while this smoke proves the material
        // override pixel path.
        {
            const std::filesystem::path pmxPath =
                std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
                "extended_morph.pmx";
            Require(
                std::filesystem::is_regular_file(pmxPath),
                "extended_morph.pmx fixture is missing"
            );
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            ModelAsset& pmxModel = resources.LoadModel(
                "r16::sabaRenderSmoke",
                pmxPath
            );
            Scene scene;
            scene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = {1.0f, 0.96f, 0.92f},
                .Intensity = 1.0f
            });
            Entity& entity = scene.InstantiateModel(pmxModel);
            // Regression: a user-added RenderPart without a runtime material
            // slot must resolve through the base-material fallback instead of
            // throwing during Render.
            DefaultModelData extraData;
            extraData.layout = {{"position", 3, FLOAT}};
            extraData.vertices = {
                0.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f
            };
            extraData.indices = {0U, 1U, 2U};
            Mesh extraMesh(std::move(extraData));
            Material extraMaterial(MaterialData{});
            entity.AddRenderPart(
                extraMesh,
                extraMaterial,
                glm::mat4(1.0f),
                std::nullopt
            );
            const glm::vec3 boundsCenter =
                entity.RenderParts()[0].GetMesh().LocalBoundsCenter();
            Camera camera(CameraParam{
                .Position = boundsCenter + glm::vec3(0.0f, 2.0f, -3.0f),
                .Target = boundsCenter,
                .Up = {0.0f, 1.0f, 0.0f},
                .VerticalFovDegrees = 45.0f
            });
            const glm::mat4 projection = glm::perspective(
                glm::radians(45.0f),
                1.0f,
                0.1f,
                100.0f
            );
            SceneFramebuffer target;
            target.Resize(TestWidth, TestHeight);
            Renderer renderer;

            scene.Update(0.0f);
            target.Clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            renderer.Render(scene, camera, projection, target);
            const Rgba8Frame baseFrame = ReadbackRgba8(target);

            IModelRuntimeDriver* runtime =
                entity.GetModelInstance().TryGetRuntime();
            Require(
                runtime != nullptr &&
                    runtime->SetMorphWeight("materialMorph", 1.0f),
                "Saba material morph weight was not accepted"
            );
            scene.Update(0.0f);
            target.Clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            renderer.Render(scene, camera, projection, target);
            const Rgba8Frame morphFrame = ReadbackRgba8(target);
            Require(
                morphFrame.pixels != baseFrame.pixels,
                "Saba material morph did not change the offscreen pixels"
            );
        }

        // R1.6 Phase 0D: explicit offline render request drives the same
        // output chain (Scene -> Renderer -> SceneFramebuffer -> RGBA8)
        // without any Window/Present involvement.
        {
            const std::filesystem::path boxPath =
                std::filesystem::path("tests") / "assets" / "models" /
                "Box.glb";
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            ModelAsset& boxModel = resources.LoadModel(
                "r16::offlineBox",
                boxPath
            );
            Scene scene;
            scene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = {1.0f, 0.96f, 0.92f},
                .Intensity = 1.0f
            });
            scene.InstantiateModel(boxModel);
            Renderer renderer;

            OfflineRenderRequest request;
            request.width = TestWidth;
            request.height = TestHeight;
            request.camera = Camera(CameraParam{
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

            const Rgba8Frame offlineFrame =
                RenderOffline(scene, request, renderer);
            Require(
                offlineFrame.width == TestWidth &&
                    offlineFrame.height == TestHeight &&
                    offlineFrame.pixels.size() ==
                        static_cast<std::size_t>(TestWidth) * TestHeight * 4U,
                "OfflineRenderRequest returned wrong dimensions"
            );
            bool anyRgbNonZero = false;
            for (std::size_t index = 0U;
                 index + 2U < offlineFrame.pixels.size();
                 index += 4U)
            {
                if (offlineFrame.pixels[index] != 0U ||
                    offlineFrame.pixels[index + 1U] != 0U ||
                    offlineFrame.pixels[index + 2U] != 0U)
                {
                    anyRgbNonZero = true;
                    break;
                }
            }
            Require(
                anyRgbNonZero,
                "OfflineRenderRequest returned an empty frame"
            );

            // Hostile caller GL state: RenderOffline must restore the
            // explicitly tracked boundary state after its internal Clear.
            Framebuffer callerFramebuffer;
            callerFramebuffer.Create();
            glBindFramebuffer(
                GL_DRAW_FRAMEBUFFER,
                callerFramebuffer.Id()
            );
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glViewport(1, 2, 3, 4);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, TestWidth, TestHeight);
            glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glStencilMask(0x33);
            glClearColor(0.1f, 0.2f, 0.3f, 0.9f);

            (void)RenderOffline(scene, request, renderer);

            GLint actualDrawFramebuffer = 0;
            GLint actualReadFramebuffer = 0;
            GLint actualViewport[4] = {0, 0, 0, 0};
            GLboolean actualScissor = GL_FALSE;
            GLboolean actualColorMask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
            GLboolean actualDepthMask = GL_TRUE;
            GLint actualStencilFront = 0;
            GLint actualStencilBack = 0;
            GLfloat actualClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glGetIntegerv(
                GL_DRAW_FRAMEBUFFER_BINDING,
                &actualDrawFramebuffer
            );
            glGetIntegerv(
                GL_READ_FRAMEBUFFER_BINDING,
                &actualReadFramebuffer
            );
            glGetIntegerv(GL_VIEWPORT, actualViewport);
            actualScissor = glIsEnabled(GL_SCISSOR_TEST);
            glGetBooleanv(GL_COLOR_WRITEMASK, actualColorMask);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &actualDepthMask);
            glGetIntegerv(GL_STENCIL_WRITEMASK, &actualStencilFront);
            glGetIntegerv(
                GL_STENCIL_BACK_WRITEMASK,
                &actualStencilBack
            );
            glGetFloatv(GL_COLOR_CLEAR_VALUE, actualClearColor);
            const bool clearColorRestored =
                std::abs(actualClearColor[0] - 0.1f) < 0.001f &&
                std::abs(actualClearColor[1] - 0.2f) < 0.001f &&
                std::abs(actualClearColor[2] - 0.3f) < 0.001f &&
                std::abs(actualClearColor[3] - 0.9f) < 0.001f;
            Require(
                actualDrawFramebuffer ==
                    static_cast<GLint>(callerFramebuffer.Id()) &&
                    actualReadFramebuffer == 0 &&
                    actualViewport[0] == 1 &&
                    actualViewport[1] == 2 &&
                    actualViewport[2] == 3 &&
                    actualViewport[3] == 4 &&
                    actualScissor == GL_TRUE &&
                    actualColorMask[0] == GL_FALSE &&
                    actualColorMask[1] == GL_TRUE &&
                    actualColorMask[2] == GL_TRUE &&
                    actualColorMask[3] == GL_FALSE &&
                    actualDepthMask == GL_FALSE &&
                    actualStencilFront == 0x33 &&
                    actualStencilBack == 0x33 &&
                    clearColorRestored,
                "RenderOffline leaked caller GL state"
            );

            // Test hygiene: restore defaults for subsequent state.
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glStencilMask(0xFF);
            glDepthMask(GL_TRUE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDisable(GL_SCISSOR_TEST);
            glViewport(0, 0, TestWidth, TestHeight);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            callerFramebuffer.Release();
        }

        // R1.6 Phase 0E: deterministic frame sequence (from-start, resume,
        // overwrite policies) through the unified offscreen boundary.
        {
            const auto readFile = [](const std::filesystem::path& path)
            {
                std::ifstream stream(path, std::ios::binary);
                Require(
                    stream.is_open(),
                    "Sequence artifact is unreadable: " + path.string()
                );
                return std::vector<std::uint8_t>(
                    std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>()
                );
            };
            const auto frameFileName = [](std::size_t frame)
            {
                std::ostringstream stream;
                stream << std::setw(8) << std::setfill('0') << frame
                       << ".png";
                return stream.str();
            };

            const std::filesystem::path pmxPath =
                std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
                "pmx_physics.pmx";
            GraphicsDevice device;
            ResourceManager resources;
            resources.BindGraphicsDevice(device);
            ModelAsset& pmxModel = resources.LoadModel(
                "r16::sequence",
                pmxPath
            );
            Scene scene;
            scene.CreateDirectionalLight(DirectionalLightData{
                .Direction = {-0.35f, -0.75f, -0.45f},
                .Color = glm::vec3(1.0f, 0.96f, 0.92f),
                .Intensity = 1.0f
            });
            Entity& entity = scene.InstantiateModel(pmxModel);
            Renderer renderer;
            ModelInstance& instance = entity.GetModelInstance();
            auto* runtime = dynamic_cast<MmdRuntimeModel*>(
                instance.TryGetRuntime()
            );
            Require(runtime != nullptr, "Sequence test lost the MMD runtime");
            // A visible static box makes the offline frames meaningful
            // (the pmx-physics fixture itself renders black without IBL).
            const std::filesystem::path boxPath =
                std::filesystem::path("tests") / "assets" / "models" /
                "Box.glb";
            ModelAsset& boxModel = resources.LoadModel(
                "r16::sequenceBox",
                boxPath
            );
            Entity& boxEntity = scene.InstantiateModel(boxModel);

            OfflineRenderRequest baseRequest;
            baseRequest.width = TestWidth;
            baseRequest.height = TestHeight;
            baseRequest.camera = Camera(CameraParam{
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

            const std::filesystem::path dirA =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_a";
            const std::filesystem::path dirB =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_b";
            std::error_code ignored;
            std::filesystem::remove_all(dirA, ignored);
            std::filesystem::remove_all(dirB, ignored);

            OfflineFrameSequenceConfig configA;
            configA.outputDirectory = dirA;
            configA.renderRequest = baseRequest;
            configA.writePng = true;
            configA.writeRaw = false;
            configA.overwritePolicy = SequenceOverwritePolicy::Reject;

            OfflineFrameSequence sequenceA(
                scene,
                renderer,
                *runtime,
                instance,
                configA
            );
            sequenceA.RenderRange(0U, 2U);
            Require(
                !sequenceA.Failed() &&
                    sequenceA.LastCommittedFrame().value_or(99U) == 2U,
                "From-start sequence did not commit frames 0..2"
            );
            Require(
                std::filesystem::is_regular_file(
                    dirA / "00000000.png"
                ) &&
                    std::filesystem::is_regular_file(
                        dirA / "00000002.png"
                    ) &&
                    std::filesystem::is_regular_file(
                        dirA / "manifest.jsonl"
                    ) &&
                    std::filesystem::is_regular_file(
                        dirA / "checkpoint-A.bin"
                    ) &&
                    std::filesystem::is_regular_file(
                        dirA / "checkpoint-B.bin"
                    ),
                "Sequence did not persist PNGs, manifest and checkpoints"
            );

            // From-start reference 0..3 in a separate output directory.
            OfflineFrameSequenceConfig configB = configA;
            configB.outputDirectory = dirB;
            OfflineFrameSequence sequenceB(
                scene,
                renderer,
                *runtime,
                instance,
                configB
            );
            sequenceB.RenderRange(0U, 3U);
            Require(
                !sequenceB.Failed(),
                "From-start reference sequence failed"
            );
            const std::vector<std::uint8_t> referenceFrame3 =
                readFile(dirB / "00000003.png");
            for (std::size_t frame = 0U; frame <= 2U; ++frame)
            {
                Require(
                    readFile(
                        dirA / frameFileName(frame)
                    ) == readFile(
                        dirB / frameFileName(frame)
                    ),
                    "Identical frames differ between output directories"
                );
            }

            // Resume: restore checkpoint 2, step 3, frame 3 must equal the
            // from-start frame 3 (same build / render environment).
            OfflineFrameSequence resume(
                scene,
                renderer,
                *runtime,
                instance,
                configA
            );
            resume.Resume(3U);
            Require(
                !resume.Failed() &&
                    resume.LastCommittedFrame().value_or(99U) == 3U,
                "Resume did not commit frame 3"
            );
            Require(
                readFile(dirA / "00000003.png") == referenceFrame3,
                "Resume frame differs from the from-start sequence"
            );

            // Reject policy on existing artifacts -> fail-stop.
            OfflineFrameSequence reject(
                scene,
                renderer,
                *runtime,
                instance,
                configA
            );
            bool rejected = false;
            try
            {
                reject.RenderRange(0U, 1U);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(
                rejected && reject.Failed(),
                "Reject policy did not fail on existing artifacts"
            );

            // Overwrite policy succeeds.
            OfflineFrameSequenceConfig configOverwrite = configA;
            configOverwrite.overwritePolicy =
                SequenceOverwritePolicy::Overwrite;
            OfflineFrameSequence overwrite(
                scene,
                renderer,
                *runtime,
                instance,
                configOverwrite
            );
            overwrite.RenderRange(0U, 1U);
            Require(
                !overwrite.Failed(),
                "Overwrite policy failed"
            );

            // VerifySkip re-renders, compares rgbaHash, and keeps files.
            const std::vector<std::uint8_t> frame0Before =
                readFile(dirA / "00000000.png");
            OfflineFrameSequenceConfig configVerify = configA;
            configVerify.overwritePolicy =
                SequenceOverwritePolicy::VerifySkip;
            OfflineFrameSequence verify(
                scene,
                renderer,
                *runtime,
                instance,
                configVerify
            );
            verify.RenderRange(0U, 2U);
            Require(
                !verify.Failed(),
                "VerifySkip policy failed"
            );
            Require(
                readFile(dirA / "00000000.png") == frame0Before,
                "VerifySkip rewrote an identical artifact"
            );

            // start > 0 sequential pre-roll: RenderRange(2,3) must equal the
            // reference frames 2..3 rendered from-start.
            const std::filesystem::path dirC =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_c";
            std::filesystem::remove_all(dirC, ignored);
            OfflineFrameSequenceConfig configC = configA;
            configC.outputDirectory = dirC;
            OfflineFrameSequence preroll(
                scene,
                renderer,
                *runtime,
                instance,
                configC
            );
            preroll.RenderRange(2U, 3U);
            Require(
                !preroll.Failed(),
                "Sequential pre-roll failed"
            );
            Require(
                readFile(dirC / "00000002.png") ==
                        readFile(dirB / "00000002.png") &&
                    readFile(dirC / "00000003.png") ==
                        readFile(dirB / "00000003.png"),
                "Pre-rolled frames differ from the from-start reference"
            );

            // Crash tail recovery: append a partial JSONL record, then resume
            // must truncate it and continue committing.
            {
                std::ofstream tail(dirA / "manifest.jsonl", std::ios::app);
                Require(tail.is_open(), "Cannot append crash tail");
                tail << "{\"type\":\"frame\",\"frameIndex\":4";
                tail.close();
            }
            OfflineFrameSequence crashResume(
                scene,
                renderer,
                *runtime,
                instance,
                configA
            );
            crashResume.Resume(4U);
            Require(
                !crashResume.Failed() &&
                    crashResume.LastCommittedFrame().value_or(99U) == 4U,
                "Resume did not recover from a partial JSONL tail"
            );
            Require(
                std::filesystem::is_regular_file(
                    dirA / "00000004.png"
                ),
                "Resume after crash tail did not commit frame 4"
            );

            // A/B alternation: after committed frame 4 (slot A), the next
            // committed frame must use slot B, and slot A must still hold
            // frame 4's checkpoint.
            OfflineFrameSequence alternate(
                scene,
                renderer,
                *runtime,
                instance,
                configA
            );
            alternate.RenderRange(6U, 6U);
            Require(
                !alternate.Failed() &&
                    alternate.LastCommittedFrame().value_or(99U) == 6U,
                "Non-sequential RenderRange failed"
            );
            {
                std::ifstream manifest(dirA / "manifest.jsonl");
                std::string line;
                bool frame6OnB = false;
                while (std::getline(manifest, line))
                {
                    if (line.find("\"frameIndex\":6") != std::string::npos &&
                        line.find("\"checkpointSlot\":\"B\"") !=
                            std::string::npos)
                    {
                        frame6OnB = true;
                    }
                }
                Require(
                    frame6OnB,
                    "Frame 6 did not alternate to checkpoint slot B"
                );
            }
            Require(
                std::filesystem::is_regular_file(
                    dirA / "checkpoint-A.bin"
                ),
                "Slot A was disturbed by the non-sequential commit"
            );

            // VerifySkip integrity: corrupted/deleted artifacts must be
            // rejected even when the RGBA hash matches.
            {
                const std::vector<std::uint8_t> goodFrame0 =
                    readFile(dirB / "00000000.png");
                auto corruptFrame0 = goodFrame0;
                corruptFrame0[corruptFrame0.size() / 2U] ^= 0xFFU;
                {
                    std::ofstream corrupt(
                        dirA / "00000000.png",
                        std::ios::binary
                    );
                    corrupt.write(
                        reinterpret_cast<const char*>(
                            corruptFrame0.data()
                        ),
                        static_cast<std::streamsize>(
                            corruptFrame0.size()
                        )
                    );
                }
                OfflineFrameSequence verifyCorrupt(
                    scene,
                    renderer,
                    *runtime,
                    instance,
                    configVerify
                );
                bool corruptRejected = false;
                try
                {
                    verifyCorrupt.RenderRange(0U, 0U);
                }
                catch (const std::exception&)
                {
                    corruptRejected = true;
                }
                Require(
                    corruptRejected && verifyCorrupt.Failed(),
                    "VerifySkip accepted a corrupted committed artifact"
                );

                std::filesystem::remove(dirA / "00000000.png", ignored);
                OfflineFrameSequence verifyMissing(
                    scene,
                    renderer,
                    *runtime,
                    instance,
                    configVerify
                );
                bool missingRejected = false;
                try
                {
                    verifyMissing.RenderRange(0U, 0U);
                }
                catch (const std::exception&)
                {
                    missingRejected = true;
                }
                Require(
                    missingRejected && verifyMissing.Failed(),
                    "VerifySkip accepted a missing committed artifact"
                );

                // Restore the artifact: VerifySkip must pass again.
                {
                    std::ofstream restore(
                        dirA / "00000000.png",
                        std::ios::binary
                    );
                    restore.write(
                        reinterpret_cast<const char*>(goodFrame0.data()),
                        static_cast<std::streamsize>(goodFrame0.size())
                    );
                }
                OfflineFrameSequence verifyRestored(
                    scene,
                    renderer,
                    *runtime,
                    instance,
                    configVerify
                );
                verifyRestored.RenderRange(0U, 0U);
                Require(
                    !verifyRestored.Failed(),
                    "VerifySkip failed after the artifact was restored"
                );
            }

            // Orphan artifact without a committed record: Reject/VerifySkip
            // fail, Overwrite recovers.
            const std::filesystem::path dirE =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_e";
            std::filesystem::remove_all(dirE, ignored);
            std::filesystem::create_directories(dirE, ignored);
            {
                const std::vector<std::uint8_t> orphan =
                    readFile(dirB / "00000000.png");
                std::ofstream orphanFile(
                    dirE / "00000000.png",
                    std::ios::binary
                );
                orphanFile.write(
                    reinterpret_cast<const char*>(orphan.data()),
                    static_cast<std::streamsize>(orphan.size())
                );
            }
            OfflineFrameSequenceConfig configE = configA;
            configE.outputDirectory = dirE;
            OfflineFrameSequence orphanReject(
                scene,
                renderer,
                *runtime,
                instance,
                configE
            );
            bool orphanRejected = false;
            try
            {
                orphanReject.RenderRange(0U, 0U);
            }
            catch (const std::exception&)
            {
                orphanRejected = true;
            }
            Require(
                orphanRejected && orphanReject.Failed(),
                "Reject accepted an orphan artifact"
            );
            configE.overwritePolicy = SequenceOverwritePolicy::Overwrite;
            OfflineFrameSequence orphanOverwrite(
                scene,
                renderer,
                *runtime,
                instance,
                configE
            );
            orphanOverwrite.RenderRange(0U, 0U);
            Require(
                !orphanOverwrite.Failed() &&
                    orphanOverwrite.LastCommittedFrame().value_or(99U) == 0U,
                "Overwrite did not recover an orphan artifact"
            );

            // No camera track: a host custom projection must stay untouched,
            // even when the base camera FOV differs.
            const glm::mat4 customProjection = glm::perspective(
                glm::radians(50.0f),
                1.0f,
                0.05f,
                250.0f
            );
            const std::filesystem::path dirF =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_f";
            const std::filesystem::path dirG =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_g";
            std::filesystem::remove_all(dirF, ignored);
            std::filesystem::remove_all(dirG, ignored);
            OfflineFrameSequenceConfig configF = configA;
            configF.outputDirectory = dirF;
            configF.renderRequest.projection = customProjection;
            OfflineFrameSequenceConfig configG = configF;
            configG.outputDirectory = dirG;
            configG.renderRequest.camera = Camera(CameraParam{
                .Position = {0.0f, 3.0f, 3.0f},
                .Target = {0.0f, 0.0f, 0.0f},
                .Up = {0.0f, 1.0f, 0.0f},
                .VerticalFovDegrees = 60.0f
            });
            OfflineFrameSequence noTrackFovA(
                scene,
                renderer,
                *runtime,
                instance,
                configF
            );
            OfflineFrameSequence noTrackFovB(
                scene,
                renderer,
                *runtime,
                instance,
                configG
            );
            noTrackFovA.RenderRange(0U, 0U);
            noTrackFovB.RenderRange(0U, 0U);
            Require(
                !noTrackFovA.Failed() && !noTrackFovB.Failed(),
                "No-track FOV sequence failed"
            );
            Require(
                readFile(dirF / "00000000.png") ==
                    readFile(dirG / "00000000.png"),
                "Projection was rebuilt without an applied camera track"
            );

            // Cursor regression: historical Overwrite/VerifySkip must never
            // regress the committed cursor or the A/B slot.
            OfflineFrameSequenceConfig configCursor = configA;
            configCursor.overwritePolicy =
                SequenceOverwritePolicy::Overwrite;
            OfflineFrameSequence cursorSession(
                scene,
                renderer,
                *runtime,
                instance,
                configCursor
            );
            cursorSession.RenderRange(4U, 4U);
            Require(
                !cursorSession.Failed(),
                "Historical overwrite failed"
            );
            Require(
                cursorSession.LastCommittedFrame().value_or(99U) == 6U,
                "Historical overwrite regressed the committed cursor"
            );
            const std::vector<std::uint8_t> checkpointB6 =
                readFile(dirA / "checkpoint-B.bin");
            cursorSession.RenderRange(7U, 7U);
            Require(
                !cursorSession.Failed() &&
                    cursorSession.LastCommittedFrame().value_or(99U) == 7U,
                "Commit after historical overwrite failed"
            );
            {
                std::ifstream manifest(dirA / "manifest.jsonl");
                std::string line;
                bool frame7OnA = false;
                while (std::getline(manifest, line))
                {
                    if (line.find("\"frameIndex\":7") !=
                            std::string::npos &&
                        line.find("\"checkpointSlot\":\"A\"") !=
                            std::string::npos)
                    {
                        frame7OnA = true;
                    }
                }
                Require(
                    frame7OnA,
                    "Frame 7 did not alternate to checkpoint slot A"
                );
            }
            Require(
                readFile(dirA / "checkpoint-B.bin") == checkpointB6,
                "Slot B was destroyed before the frame-7 manifest commit"
            );

            // Append-only forward commit log: an uncommitted historical
            // frame below the tail must be rejected without touching the
            // manifest or the checkpoint slot.
            {
                const std::vector<std::uint8_t> checkpointABefore =
                    readFile(dirA / "checkpoint-A.bin");
                OfflineFrameSequence backfill(
                    scene,
                    renderer,
                    *runtime,
                    instance,
                    configA
                );
                bool backfillRejected = false;
                try
                {
                    backfill.RenderRange(1U, 1U);
                }
                catch (const std::exception&)
                {
                    backfillRejected = true;
                }
                Require(
                    backfillRejected && backfill.Failed(),
                    "Uncommitted historical frame was appended"
                );
                Require(
                    readFile(dirA / "checkpoint-A.bin") ==
                        checkpointABefore,
                    "Rejected backfill disturbed the checkpoint slot"
                );
                std::ifstream manifest(dirA / "manifest.jsonl");
                std::string line;
                MotionFrameIndex lastSeen = 0U;
                while (std::getline(manifest, line))
                {
                    if (line.find("\"type\":\"frame\"") ==
                        std::string::npos)
                    {
                        continue;
                    }
                    const std::size_t key =
                        line.find("\"frameIndex\":");
                    if (key != std::string::npos)
                    {
                        lastSeen = static_cast<MotionFrameIndex>(
                            std::strtoull(
                                line.c_str() + key + 13U,
                                nullptr,
                                10
                            )
                        );
                    }
                }
                Require(
                    lastSeen == 7U,
                    "Rejected backfill changed the manifest tail"
                );
            }

            // Overwrite of the latest committed frame succeeds when both the
            // RGBA and the checkpoint wire hash match.
            {
                const std::vector<std::uint8_t> checkpointABefore =
                    readFile(dirA / "checkpoint-A.bin");
                OfflineFrameSequence latestOverwrite(
                    scene,
                    renderer,
                    *runtime,
                    instance,
                    configCursor
                );
                latestOverwrite.RenderRange(7U, 7U);
                Require(
                    !latestOverwrite.Failed() &&
                        latestOverwrite.LastCommittedFrame().value_or(99U) ==
                            7U,
                    "Latest committed overwrite failed"
                );
                Require(
                    readFile(dirA / "checkpoint-A.bin") ==
                        checkpointABefore,
                    "Latest overwrite changed the checkpoint wire"
                );
            }

            // A manifest truncated to zero by a crash before the first
            // session record completed is equivalent to a fresh directory.
            {
                const std::filesystem::path dirH =
                    std::filesystem::temp_directory_path() /
                    "wisteria_seq_h";
                std::filesystem::remove_all(dirH, ignored);
                std::filesystem::create_directories(dirH, ignored);
                {
                    std::ofstream partial(
                        dirH / "manifest.jsonl",
                        std::ios::binary
                    );
                    partial << "{\"type\":\"session\",\"sessionIdentity\"";
                }
                OfflineFrameSequenceConfig configH = configA;
                configH.outputDirectory = dirH;
                OfflineFrameSequence freshRecovery(
                    scene,
                    renderer,
                    *runtime,
                    instance,
                    configH
                );
                freshRecovery.RenderRange(0U, 0U);
                Require(
                    !freshRecovery.Failed() &&
                        freshRecovery.LastCommittedFrame().value_or(99U) ==
                            0U,
                    "Empty-manifest recovery did not behave like a fresh dir"
                );
                std::ifstream manifest(dirH / "manifest.jsonl");
                const std::string content{
                    std::istreambuf_iterator<char>(manifest),
                    std::istreambuf_iterator<char>()
                };
                std::size_t sessionCount = 0U;
                std::size_t position = 0U;
                while ((position = content.find(
                        "\"type\":\"session\"",
                        position
                    )) != std::string::npos)
                {
                    ++sessionCount;
                    position += 16U;
                }
                Require(
                    sessionCount == 1U &&
                        content.find("\"frameIndex\":0") !=
                            std::string::npos,
                    "Empty-manifest recovery wrote a malformed manifest"
                );
                std::filesystem::remove_all(dirH, ignored);
            }

            // Session identity is path-independent: a copied output directory
            // must resume and must not duplicate the session record.
            const std::filesystem::path dirA2 =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_a2";
            std::filesystem::remove_all(dirA2, ignored);
            std::filesystem::create_directories(dirA2, ignored);
            for (const auto& entry :
                std::filesystem::directory_iterator(dirA))
            {
                std::filesystem::copy(
                    entry.path(),
                    dirA2 / entry.path().filename(),
                    std::filesystem::copy_options::overwrite_existing,
                    ignored
                );
            }
            OfflineFrameSequenceConfig configA2 = configA;
            configA2.outputDirectory = dirA2;
            OfflineFrameSequence resumeCopy(
                scene,
                renderer,
                *runtime,
                instance,
                configA2
            );
            resumeCopy.Resume(6U);
            Require(
                !resumeCopy.Failed() &&
                    resumeCopy.LastCommittedFrame().value_or(99U) == 7U,
                "Copied directory resume failed"
            );
            {
                std::ifstream manifest(dirA2 / "manifest.jsonl");
                const std::string content{
                    std::istreambuf_iterator<char>(manifest),
                    std::istreambuf_iterator<char>()
                };
                std::size_t sessionCount = 0U;
                std::size_t position = 0U;
                while ((position = content.find(
                        "\"type\":\"session\"",
                        position
                    )) != std::string::npos)
                {
                    ++sessionCount;
                    position += 16U;
                }
                Require(
                    sessionCount == 1U,
                    "Manifest duplicated the session record"
                );
            }

            // Overwrite of a committed frame must fail when the re-rendered
            // RGBA diverges from the committed record.
            boxEntity.GetTransform().SetPosition(
                glm::vec3(2.0f, 0.0f, 0.0f)
            );
            OfflineFrameSequence mismatchedOverwrite(
                scene,
                renderer,
                *runtime,
                instance,
                configCursor
            );
            bool mismatchRejected = false;
            try
            {
                mismatchedOverwrite.RenderRange(4U, 4U);
            }
            catch (const std::exception&)
            {
                mismatchRejected = true;
            }
            Require(
                mismatchRejected && mismatchedOverwrite.Failed(),
                "Overwrite accepted a deterministic RGBA mismatch"
            );
            boxEntity.GetTransform().SetPosition(glm::vec3(0.0f));

            // Applied camera-track FOV must reach the projection.
            const std::filesystem::path cameraVmd =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_camera.vmd";
            {
                std::vector<std::uint8_t> bytes;
                const auto appendValue =
                    [&bytes]<typename T>(const T& value)
                {
                    const std::size_t offset = bytes.size();
                    bytes.resize(offset + sizeof(T));
                    std::memcpy(bytes.data() + offset, &value, sizeof(T));
                };
                const auto appendFixed =
                    [&bytes](
                        std::string_view value,
                        std::size_t size
                    )
                {
                    const std::size_t begin = bytes.size();
                    bytes.resize(begin + size, 0U);
                    const std::size_t copySize =
                        std::min(value.size(), size);
                    std::memcpy(
                        bytes.data() + begin,
                        value.data(),
                        copySize
                    );
                };
                appendFixed("Vocaloid Motion Data 0002", 30U);
                appendFixed("testModel", 20U);
                appendValue(std::uint32_t{0U});  // bones
                appendValue(std::uint32_t{0U});  // morphs
                appendValue(std::uint32_t{1U});  // cameras
                appendValue(std::uint32_t{0U});  // frame
                appendValue(8.0f);               // distance
                appendValue(0.0f);
                appendValue(0.0f);
                appendValue(0.0f);               // interest
                appendValue(0.0f);
                appendValue(0.0f);
                appendValue(0.0f);               // rotation
                const std::array<std::uint8_t, 24> interpolation{};
                bytes.insert(
                    bytes.end(),
                    interpolation.begin(),
                    interpolation.end()
                );
                appendValue(std::uint32_t{30U}); // view angle (degrees)
                appendValue(std::uint8_t{1U});   // perspective
                appendValue(std::uint32_t{0U});  // lights
                std::ofstream out(cameraVmd, std::ios::binary);
                Require(out.is_open(), "Cannot write camera VMD");
                out.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())
                );
                out.close();
            }
            Require(
                runtime->LoadCameraMotion(cameraVmd),
                "Sequence runtime rejected the camera VMD"
            );
            const std::filesystem::path dirT =
                std::filesystem::temp_directory_path() /
                "wisteria_seq_t";
            std::filesystem::remove_all(dirT, ignored);
            OfflineFrameSequenceConfig configT = configA;
            configT.outputDirectory = dirT;
            OfflineFrameSequence trackFov(
                scene,
                renderer,
                *runtime,
                instance,
                configT
            );
            trackFov.RenderRange(0U, 0U);
            Require(
                !trackFov.Failed(),
                "Camera-track sequence failed"
            );
            Require(
                readFile(dirT / "00000000.png") !=
                    readFile(dirB / "00000000.png"),
                "Applied camera-track FOV did not reach the projection"
            );

            std::filesystem::remove_all(dirC, ignored);
            std::filesystem::remove_all(dirE, ignored);
            std::filesystem::remove_all(dirF, ignored);
            std::filesystem::remove_all(dirG, ignored);
            std::filesystem::remove_all(dirT, ignored);
            std::filesystem::remove_all(dirA, ignored);
            std::filesystem::remove_all(dirA2, ignored);
            std::filesystem::remove_all(dirB, ignored);
            std::filesystem::remove(cameraVmd, ignored);
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
