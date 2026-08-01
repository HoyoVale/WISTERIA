#include "pch.hpp"
#include "environment.hpp"
#include "shader.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stb_image.h>
#include <utility>
#include <vector>

namespace
{
constexpr unsigned int CubemapFaceCount = 6;

bool IsPowerOfTwo(unsigned int value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

unsigned int MaximumMipLevels(unsigned int resolution)
{
    unsigned int levels = 0;
    while (resolution != 0)
    {
        ++levels;
        resolution >>= 1U;
    }
    return levels;
}

std::filesystem::path ShaderPath(const char* name)
{
    return std::filesystem::current_path() / "assets" / "shaders" / name;
}

glm::mat4 CaptureProjection()
{
    return glm::perspective(
        glm::radians(90.0f),
        1.0f,
        0.1f,
        10.0f
    );
}

std::array<glm::mat4, CubemapFaceCount> CaptureViews()
{
    const glm::vec3 origin(0.0f);
    return {
        glm::lookAt(origin, glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(origin, glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(origin, glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(origin, glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(origin, glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(origin, glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };
}

glm::vec3 CubemapDirection(
    unsigned int face,
    float horizontal,
    float vertical
)
{
    switch (face)
    {
    case 0: return glm::normalize(glm::vec3( 1.0f, -vertical, -horizontal));
    case 1: return glm::normalize(glm::vec3(-1.0f, -vertical,  horizontal));
    case 2: return glm::normalize(glm::vec3( horizontal,  1.0f,  vertical));
    case 3: return glm::normalize(glm::vec3( horizontal, -1.0f, -vertical));
    case 4: return glm::normalize(glm::vec3( horizontal, -vertical,  1.0f));
    default:return glm::normalize(glm::vec3(-horizontal, -vertical, -1.0f));
    }
}

void ConfigureCubemap(GLuint texture, GLenum minificationFilter)
{
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minificationFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void AllocateCubemapLevel(
    unsigned int resolution,
    unsigned int mipLevel
)
{
    for (unsigned int face = 0; face < CubemapFaceCount; ++face)
    {
        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            static_cast<GLint>(mipLevel),
            GL_RGB16F,
            static_cast<GLsizei>(resolution),
            static_cast<GLsizei>(resolution),
            0,
            GL_RGB,
            GL_FLOAT,
            nullptr
        );
    }
}

void RequireCompleteFramebuffer(const char* operation)
{
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        throw std::runtime_error(
            std::string("Incomplete framebuffer while ") + operation
        );
    }
}

class ScopedOpenGlState
{
public:
    ScopedOpenGlState()
    {
        glGetIntegerv(GL_VIEWPORT, this->viewport.data());
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &this->framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &this->renderbuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &this->program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &this->vertexArray);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &this->activeTexture);
        glGetIntegerv(GL_DEPTH_FUNC, &this->depthFunction);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &this->depthWriteMask);
        this->depthTest = glIsEnabled(GL_DEPTH_TEST);
        this->cullFace = glIsEnabled(GL_CULL_FACE);
        this->blend = glIsEnabled(GL_BLEND);
    }

    ~ScopedOpenGlState()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(this->framebuffer));
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(this->renderbuffer));
        glViewport(
            this->viewport[0],
            this->viewport[1],
            this->viewport[2],
            this->viewport[3]
        );
        glUseProgram(static_cast<GLuint>(this->program));
        glBindVertexArray(static_cast<GLuint>(this->vertexArray));
        glActiveTexture(static_cast<GLenum>(this->activeTexture));
        glDepthFunc(static_cast<GLenum>(this->depthFunction));
        glDepthMask(this->depthWriteMask);
        SetEnabled(GL_DEPTH_TEST, this->depthTest);
        SetEnabled(GL_CULL_FACE, this->cullFace);
        SetEnabled(GL_BLEND, this->blend);
    }

private:
    static void SetEnabled(GLenum capability, GLboolean enabled)
    {
        if (enabled == GL_TRUE)
            glEnable(capability);
        else
            glDisable(capability);
    }

    std::array<GLint, 4> viewport{};
    GLint framebuffer = 0;
    GLint renderbuffer = 0;
    GLint program = 0;
    GLint vertexArray = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint depthFunction = GL_LESS;
    GLboolean depthWriteMask = GL_TRUE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean blend = GL_FALSE;
};

std::vector<std::uint8_t> ReadBinaryFile(
    const std::filesystem::path& filePath
)
{
    std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open environment image: " + filePath.string());

    const std::streampos end = stream.tellg();
    if (end <= 0)
        throw std::runtime_error("Environment image is empty: " + filePath.string());
    if (static_cast<std::uintmax_t>(end) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error("Environment image is too large");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!stream)
        throw std::runtime_error("Cannot read environment image: " + filePath.string());
    return bytes;
}
}

EnvironmentMapData EnvironmentMapData::ProceduralSky()
{
    return EnvironmentMapData{};
}

EnvironmentMapData EnvironmentMapData::FromEquirectangular(
    std::filesystem::path filePath
)
{
    if (filePath.empty())
        throw std::invalid_argument("Environment image path must not be empty");

    EnvironmentMapData result;
    result.equirectangularPath = std::move(filePath);
    return result;
}

EnvironmentMap::EnvironmentMap(EnvironmentMapData data)
    : data(std::move(data))
{
    if (!IsPowerOfTwo(this->data.environmentResolution) ||
        !IsPowerOfTwo(this->data.irradianceResolution) ||
        !IsPowerOfTwo(this->data.prefilterResolution) ||
        !IsPowerOfTwo(this->data.brdfResolution))
    {
        throw std::invalid_argument(
            "Environment texture resolutions must be non-zero powers of two"
        );
    }
    if (this->data.prefilterMipLevels == 0 ||
        this->data.prefilterMipLevels >
            MaximumMipLevels(this->data.prefilterResolution))
    {
        throw std::invalid_argument(
            "Environment prefilter mip count is invalid for its resolution"
        );
    }
    if (!std::isfinite(this->data.intensity) || this->data.intensity < 0.0f)
        throw std::invalid_argument("Environment intensity must be finite and non-negative");
}

EnvironmentMap::~EnvironmentMap()
{
    this->Release();
}

void EnvironmentMap::Attach()
{
    if (this->attached)
        return;

    GLint maximumCubemapSize = 0;
    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximumCubemapSize);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    const unsigned int largestCubemap = std::max({
        this->data.environmentResolution,
        this->data.irradianceResolution,
        this->data.prefilterResolution
    });
    if (maximumCubemapSize <= 0 ||
        largestCubemap > static_cast<unsigned int>(maximumCubemapSize) ||
        maximumTextureSize <= 0 ||
        this->data.brdfResolution > static_cast<unsigned int>(maximumTextureSize))
    {
        throw std::runtime_error("Environment texture resolution exceeds OpenGL limits");
    }

    ScopedOpenGlState previousState;
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    try
    {
        this->CreateGeometry();
        glGenFramebuffers(1, &this->captureFramebuffer);
        glGenRenderbuffers(1, &this->captureRenderbuffer);
        if (this->captureFramebuffer == 0 || this->captureRenderbuffer == 0)
            throw std::runtime_error("Cannot create environment capture framebuffer");

        glBindFramebuffer(GL_FRAMEBUFFER, this->captureFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, this->captureRenderbuffer);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER,
            this->captureRenderbuffer
        );

        this->CreateEnvironmentCubemap();
        this->CreateIrradianceMap();
        this->CreatePrefilterMap();
        this->CreateBrdfLut();
        this->CreateSkyboxProgram();
        this->attached = true;
    }
    catch (...)
    {
        this->Release();
        throw;
    }
}

bool EnvironmentMap::IsAttached() const noexcept
{
    return this->attached;
}

void EnvironmentMap::BindIrradiance(unsigned int unit) const
{
    if (!this->attached)
        throw std::logic_error("Environment map must be attached before binding");
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, this->irradianceCubemap);
}

void EnvironmentMap::BindPrefilter(unsigned int unit) const
{
    if (!this->attached)
        throw std::logic_error("Environment map must be attached before binding");
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, this->prefilterCubemap);
}

void EnvironmentMap::BindBrdfLut(unsigned int unit) const
{
    if (!this->attached)
        throw std::logic_error("Environment map must be attached before binding");
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, this->brdfLut);
}

void EnvironmentMap::DrawSkybox(
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    if (!this->attached)
        throw std::logic_error("Environment map must be attached before drawing");
    if (!this->data.drawSkybox)
        return;

    ScopedOpenGlState previousState;
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    this->skyboxProgram->Use();
    this->skyboxProgram->UniformMat4f("view", glm::mat4(glm::mat3(view)));
    this->skyboxProgram->UniformMat4f("projection", projection);
    this->skyboxProgram->Uniform1f("environmentIntensity", this->data.intensity);
    this->skyboxProgram->UniformTex("environmentMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, this->environmentCubemap);
    this->RenderCube();
}

float EnvironmentMap::Intensity() const noexcept
{
    return this->data.intensity;
}

void EnvironmentMap::SetIntensity(float intensity)
{
    if (!std::isfinite(intensity) || intensity < 0.0f)
        throw std::invalid_argument("Environment intensity must be finite and non-negative");
    this->data.intensity = intensity;
}

bool EnvironmentMap::ShouldDrawSkybox() const noexcept
{
    return this->data.drawSkybox;
}

void EnvironmentMap::SetDrawSkybox(bool enabled) noexcept
{
    this->data.drawSkybox = enabled;
}

float EnvironmentMap::MaxReflectionLod() const noexcept
{
    return static_cast<float>(this->data.prefilterMipLevels - 1U);
}

const EnvironmentMapData& EnvironmentMap::Data() const noexcept
{
    return this->data;
}

void EnvironmentMap::CreateGeometry()
{
    static constexpr std::array<float, 108> CubeVertices = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1,-1, 1,
        -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1,-1, -1,-1, 1, -1, 1, 1,
         1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1,-1,  1,-1, 1,  1, 1, 1,
        -1,-1,-1,  1,-1,-1,  1,-1, 1,  1,-1, 1, -1,-1, 1, -1,-1,-1,
        -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1
    };
    static constexpr std::array<float, 8> QuadVertices = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &this->cubeVao);
    glGenBuffers(1, &this->cubeVbo);
    glBindVertexArray(this->cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, this->cubeVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(CubeVertices),
        CubeVertices.data(),
        GL_STATIC_DRAW
    );
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    glGenVertexArrays(1, &this->quadVao);
    glGenBuffers(1, &this->quadVbo);
    glBindVertexArray(this->quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, this->quadVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(QuadVertices),
        QuadVertices.data(),
        GL_STATIC_DRAW
    );
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    if (this->cubeVao == 0 || this->cubeVbo == 0 ||
        this->quadVao == 0 || this->quadVbo == 0)
    {
        throw std::runtime_error("Cannot create environment rendering geometry");
    }
}

void EnvironmentMap::CreateEnvironmentCubemap()
{
    if (this->data.equirectangularPath.empty())
        this->CreateProceduralCubemap();
    else
        this->CreateEquirectangularCubemap();
}

void EnvironmentMap::CreateProceduralCubemap()
{
    glGenTextures(1, &this->environmentCubemap);
    if (this->environmentCubemap == 0)
        throw std::runtime_error("Cannot create procedural environment cubemap");
    ConfigureCubemap(this->environmentCubemap, GL_LINEAR_MIPMAP_LINEAR);

    const unsigned int resolution = this->data.environmentResolution;
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.35f, 0.65f, 0.25f));
    std::vector<float> pixels(
        static_cast<std::size_t>(resolution) * resolution * 3U
    );

    for (unsigned int face = 0; face < CubemapFaceCount; ++face)
    {
        for (unsigned int y = 0; y < resolution; ++y)
        {
            for (unsigned int x = 0; x < resolution; ++x)
            {
                const float horizontal =
                    2.0f * (static_cast<float>(x) + 0.5f) /
                    static_cast<float>(resolution) - 1.0f;
                const float vertical =
                    2.0f * (static_cast<float>(y) + 0.5f) /
                    static_cast<float>(resolution) - 1.0f;
                const glm::vec3 direction = CubemapDirection(
                    face,
                    horizontal,
                    vertical
                );

                glm::vec3 color;
                if (direction.y >= 0.0f)
                {
                    const float height = std::pow(direction.y, 0.35f);
                    color = glm::mix(
                        glm::vec3(0.65f, 0.75f, 0.95f),
                        glm::vec3(0.035f, 0.12f, 0.32f),
                        height
                    );
                }
                else
                {
                    const float depth = std::min(-direction.y, 1.0f);
                    color = glm::mix(
                        glm::vec3(0.22f, 0.19f, 0.17f),
                        glm::vec3(0.025f, 0.03f, 0.04f),
                        depth
                    );
                }

                const float sunAlignment = std::max(
                    glm::dot(direction, sunDirection),
                    0.0f
                );
                const float sun = std::pow(sunAlignment, 900.0f) * 18.0f;
                const float glow = std::pow(sunAlignment, 24.0f) * 0.35f;
                color += glm::vec3(1.0f, 0.82f, 0.58f) * (sun + glow);

                const std::size_t offset =
                    (static_cast<std::size_t>(y) * resolution + x) * 3U;
                pixels[offset] = color.r;
                pixels[offset + 1U] = color.g;
                pixels[offset + 2U] = color.b;
            }
        }

        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_RGB16F,
            static_cast<GLsizei>(resolution),
            static_cast<GLsizei>(resolution),
            0,
            GL_RGB,
            GL_FLOAT,
            pixels.data()
        );
    }
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
}

void EnvironmentMap::CreateEquirectangularCubemap()
{
    const std::vector<std::uint8_t> bytes =
        ReadBinaryFile(this->data.equirectangularPath);
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Environment image is too large for stb_image");

    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
        stbi_loadf_from_memory(
            bytes.data(),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels,
            3
        ),
        stbi_image_free
    );
    if (pixels == nullptr)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "Cannot decode environment image: " +
            std::string(reason != nullptr ? reason : "unknown stb_image error")
        );
    }

    GLuint equirectangularTexture = 0;
    glGenTextures(1, &equirectangularTexture);
    if (equirectangularTexture == 0)
        throw std::runtime_error("Cannot create equirectangular environment texture");

    try
    {
        glBindTexture(GL_TEXTURE_2D, equirectangularTexture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB16F,
            width,
            height,
            0,
            GL_RGB,
            GL_FLOAT,
            pixels.get()
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &this->environmentCubemap);
        if (this->environmentCubemap == 0)
            throw std::runtime_error("Cannot create environment cubemap");
        ConfigureCubemap(this->environmentCubemap, GL_LINEAR_MIPMAP_LINEAR);
        AllocateCubemapLevel(this->data.environmentResolution, 0);

        Shader shader(
            ShaderPath("environment_cube.vert").string(),
            ShaderPath("equirectangular_to_cube.frag").string()
        );
        Program program(shader.GetShaderList());
        program.Use();
        program.UniformTex("equirectangularMap", 0);
        program.UniformMat4f("projection", CaptureProjection());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, equirectangularTexture);

        glBindFramebuffer(GL_FRAMEBUFFER, this->captureFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, this->captureRenderbuffer);
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH_COMPONENT24,
            this->data.environmentResolution,
            this->data.environmentResolution
        );
        glViewport(
            0,
            0,
            this->data.environmentResolution,
            this->data.environmentResolution
        );

        const auto views = CaptureViews();
        for (unsigned int face = 0; face < CubemapFaceCount; ++face)
        {
            program.UniformMat4f("view", views[face]);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                this->environmentCubemap,
                0
            );
            RequireCompleteFramebuffer("converting an equirectangular environment");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            this->RenderCube();
        }

        glBindTexture(GL_TEXTURE_CUBE_MAP, this->environmentCubemap);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    }
    catch (...)
    {
        glDeleteTextures(1, &equirectangularTexture);
        throw;
    }
    glDeleteTextures(1, &equirectangularTexture);
}

void EnvironmentMap::CreateIrradianceMap()
{
    glGenTextures(1, &this->irradianceCubemap);
    if (this->irradianceCubemap == 0)
        throw std::runtime_error("Cannot create irradiance cubemap");
    ConfigureCubemap(this->irradianceCubemap, GL_LINEAR);
    AllocateCubemapLevel(this->data.irradianceResolution, 0);

    Shader shader(
        ShaderPath("environment_cube.vert").string(),
        ShaderPath("irradiance_convolution.frag").string()
    );
    Program program(shader.GetShaderList());
    program.Use();
    program.UniformTex("environmentMap", 0);
    program.UniformMat4f("projection", CaptureProjection());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, this->environmentCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, this->captureFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, this->captureRenderbuffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT24,
        this->data.irradianceResolution,
        this->data.irradianceResolution
    );
    glViewport(
        0,
        0,
        this->data.irradianceResolution,
        this->data.irradianceResolution
    );

    const auto views = CaptureViews();
    for (unsigned int face = 0; face < CubemapFaceCount; ++face)
    {
        program.UniformMat4f("view", views[face]);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            this->irradianceCubemap,
            0
        );
        RequireCompleteFramebuffer("convolving environment irradiance");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->RenderCube();
    }
}

void EnvironmentMap::CreatePrefilterMap()
{
    glGenTextures(1, &this->prefilterCubemap);
    if (this->prefilterCubemap == 0)
        throw std::runtime_error("Cannot create prefiltered environment cubemap");
    ConfigureCubemap(this->prefilterCubemap, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_MAX_LEVEL,
        static_cast<GLint>(this->data.prefilterMipLevels - 1U)
    );
    for (unsigned int mip = 0; mip < this->data.prefilterMipLevels; ++mip)
    {
        const unsigned int resolution = std::max(
            1U,
            this->data.prefilterResolution >> mip
        );
        AllocateCubemapLevel(resolution, mip);
    }

    Shader shader(
        ShaderPath("environment_cube.vert").string(),
        ShaderPath("prefilter_environment.frag").string()
    );
    Program program(shader.GetShaderList());
    program.Use();
    program.UniformTex("environmentMap", 0);
    program.UniformMat4f("projection", CaptureProjection());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, this->environmentCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, this->captureFramebuffer);
    const auto views = CaptureViews();
    for (unsigned int mip = 0; mip < this->data.prefilterMipLevels; ++mip)
    {
        const unsigned int resolution = std::max(
            1U,
            this->data.prefilterResolution >> mip
        );
        glBindRenderbuffer(GL_RENDERBUFFER, this->captureRenderbuffer);
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH_COMPONENT24,
            resolution,
            resolution
        );
        glViewport(0, 0, resolution, resolution);
        const float roughness = this->data.prefilterMipLevels > 1
            ? static_cast<float>(mip) /
                static_cast<float>(this->data.prefilterMipLevels - 1U)
            : 0.0f;
        program.Uniform1f("roughness", roughness);

        for (unsigned int face = 0; face < CubemapFaceCount; ++face)
        {
            program.UniformMat4f("view", views[face]);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                this->prefilterCubemap,
                static_cast<GLint>(mip)
            );
            RequireCompleteFramebuffer("prefiltering an environment reflection");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            this->RenderCube();
        }
    }
}

void EnvironmentMap::CreateBrdfLut()
{
    glGenTextures(1, &this->brdfLut);
    if (this->brdfLut == 0)
        throw std::runtime_error("Cannot create BRDF integration texture");
    glBindTexture(GL_TEXTURE_2D, this->brdfLut);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RG16F,
        this->data.brdfResolution,
        this->data.brdfResolution,
        0,
        GL_RG,
        GL_FLOAT,
        nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, this->captureFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, this->captureRenderbuffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT24,
        this->data.brdfResolution,
        this->data.brdfResolution
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        this->brdfLut,
        0
    );
    RequireCompleteFramebuffer("integrating the BRDF lookup table");
    glViewport(0, 0, this->data.brdfResolution, this->data.brdfResolution);

    Shader shader(
        ShaderPath("brdf_lut.vert").string(),
        ShaderPath("brdf_lut.frag").string()
    );
    Program program(shader.GetShaderList());
    program.Use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    this->RenderQuad();
}

void EnvironmentMap::CreateSkyboxProgram()
{
    auto shader = std::make_unique<Shader>(
        ShaderPath("skybox.vert").string(),
        ShaderPath("skybox.frag").string()
    );
    auto program = std::make_unique<Program>(shader->GetShaderList());
    this->skyboxShader = std::move(shader);
    this->skyboxProgram = std::move(program);
}

void EnvironmentMap::RenderCube() const
{
    glBindVertexArray(this->cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void EnvironmentMap::RenderQuad() const
{
    glBindVertexArray(this->quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void EnvironmentMap::Release() noexcept
{
    this->skyboxProgram.reset();
    this->skyboxShader.reset();
    if (this->brdfLut != 0)
        glDeleteTextures(1, &this->brdfLut);
    if (this->prefilterCubemap != 0)
        glDeleteTextures(1, &this->prefilterCubemap);
    if (this->irradianceCubemap != 0)
        glDeleteTextures(1, &this->irradianceCubemap);
    if (this->environmentCubemap != 0)
        glDeleteTextures(1, &this->environmentCubemap);
    if (this->captureRenderbuffer != 0)
        glDeleteRenderbuffers(1, &this->captureRenderbuffer);
    if (this->captureFramebuffer != 0)
        glDeleteFramebuffers(1, &this->captureFramebuffer);
    if (this->quadVbo != 0)
        glDeleteBuffers(1, &this->quadVbo);
    if (this->quadVao != 0)
        glDeleteVertexArrays(1, &this->quadVao);
    if (this->cubeVbo != 0)
        glDeleteBuffers(1, &this->cubeVbo);
    if (this->cubeVao != 0)
        glDeleteVertexArrays(1, &this->cubeVao);

    this->brdfLut = 0;
    this->prefilterCubemap = 0;
    this->irradianceCubemap = 0;
    this->environmentCubemap = 0;
    this->captureRenderbuffer = 0;
    this->captureFramebuffer = 0;
    this->quadVbo = 0;
    this->quadVao = 0;
    this->cubeVbo = 0;
    this->cubeVao = 0;
    this->attached = false;
}
