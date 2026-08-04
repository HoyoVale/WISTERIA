#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

namespace wisteria
{
void Renderer::EnsureSkinningResources()
{
    if (this->skinningBuffer != 0 && this->skinningTexture != 0)
        return;

    GLint vertexTextureUnits = 0;
    GLint combinedTextureUnits = 0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertexTextureUnits);
    glGetIntegerv(
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        &combinedTextureUnits
    );
    if (vertexTextureUnits < 1 ||
        combinedTextureUnits <= static_cast<GLint>(SkinningTextureUnit))
    {
        throw std::runtime_error(
            "OpenGL does not provide the texture unit required for GPU skinning"
        );
    }

    GLuint nextBuffer = 0;
    GLuint nextTexture = 0;
    glGenBuffers(1, &nextBuffer);
    glGenTextures(1, &nextTexture);
    if (nextBuffer == 0 || nextTexture == 0)
    {
        if (nextTexture != 0)
            glDeleteTextures(1, &nextTexture);
        if (nextBuffer != 0)
            glDeleteBuffers(1, &nextBuffer);
        throw std::runtime_error("Cannot create GPU skinning matrix palette");
    }

    glBindBuffer(GL_TEXTURE_BUFFER, nextBuffer);
    // Allocate one identity texel up front. The vertex shader statically
    // samples boneMatrixPalette, and Mesa validates the sampler's buffer
    // texture at draw time even when GPU skinning is disabled, so the
    // texture buffer must never be empty.
    const glm::mat4 identity(1.0f);
    glBufferData(
        GL_TEXTURE_BUFFER,
        sizeof(glm::mat4),
        &identity[0][0],
        GL_DYNAMIC_DRAW
    );
    glBindTexture(GL_TEXTURE_BUFFER, nextTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, nextBuffer);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    GLint maximumTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximumTexels);
    if (maximumTexels < 4)
    {
        glDeleteTextures(1, &nextTexture);
        glDeleteBuffers(1, &nextBuffer);
        throw std::runtime_error("OpenGL texture buffers cannot store one bone matrix");
    }
    this->skinningBuffer = nextBuffer;
    this->skinningTexture = nextTexture;
    this->maximumSkinningMatrices =
        static_cast<std::size_t>(maximumTexels) / 4U;
}

void Renderer::UploadSkinning(
    Program& program,
    const ShaderInterface& shaderInterface,
    const Mesh& mesh,
    const Pose* pose
)
{
    if (!shaderInterface.skinningSupported)
        return;

    static int skinningLogCounter = 0;
    if ((skinningLogCounter++ % 20000) == 0)
    {
        std::cout << "[SKINNING] dynamic="
                  << (mesh.HasDynamicVertexSource() ? "1" : "0")
                  << " skinned=" << (mesh.IsSkinned() ? "1" : "0")
                  << " bones=" << mesh.RequiredBoneCount()
                  << std::endl;
    }

    // Vertices uploaded by Saba are already CPU-skinned; the GPU skinning
    // pass must be disabled or they would be transformed a second time.
    const bool enabled = mesh.IsSkinned() && pose != nullptr &&
        !mesh.HasDynamicVertexSource();
    program.Uniform1i(shaderInterface.skinningEnabled, enabled ? 1 : 0);

    // The vertex shader statically samples boneMatrixPalette, so Mesa
    // validates the sampler's buffer texture at draw time even when GPU
    // skinning is disabled. Non-skinned meshes (PBR planes, primitives)
    // must also keep a valid buffer texture bound or llvmpipe reports
    // GL_INVALID_OPERATION on the first draw call.
    this->EnsureSkinningResources();
    glActiveTexture(GL_TEXTURE0 + SkinningTextureUnit);
    glBindTexture(GL_TEXTURE_BUFFER, this->skinningTexture);
    program.UniformTex(
        shaderInterface.boneMatrixPalette,
        SkinningTextureUnit
    );

    if (!mesh.IsSkinned())
        return;

    if (!enabled)
        return;
    if (pose->BoneCount() < mesh.RequiredBoneCount())
    {
        throw std::runtime_error(
            "Entity Pose does not contain every bone required by its Mesh"
        );
    }

    const std::span<const glm::mat4> matrices = pose->SkinningMatrices();
    if (matrices.size() > this->maximumSkinningMatrices)
    {
        throw std::runtime_error(
            "Skeleton exceeds GL_MAX_TEXTURE_BUFFER_SIZE"
        );
    }

    if (this->uploadedPose != pose ||
        this->uploadedPoseRevision != pose->Revision())
    {
        if (matrices.size() >
            static_cast<std::size_t>(
                std::numeric_limits<GLsizeiptr>::max()
            ) / sizeof(glm::mat4))
        {
            throw std::overflow_error("Skinning palette is too large");
        }
        glBindBuffer(GL_TEXTURE_BUFFER, this->skinningBuffer);
        glBufferData(
            GL_TEXTURE_BUFFER,
            static_cast<GLsizeiptr>(matrices.size() * sizeof(glm::mat4)),
            matrices.data(),
            GL_DYNAMIC_DRAW
        );
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
        this->uploadedPose = pose;
        this->uploadedPoseRevision = pose->Revision();
    }

    glActiveTexture(GL_TEXTURE0 + SkinningTextureUnit);
    glBindTexture(GL_TEXTURE_BUFFER, this->skinningTexture);
    program.UniformTex(
        shaderInterface.boneMatrixPalette,
        SkinningTextureUnit
    );
}

void Renderer::UploadTransforms(
    Program& program,
    const ShaderInterface& shaderInterface,
    const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    if (shaderInterface.transformMode ==
        TransformUniformMode::CombinedTransform)
    {
        program.UniformMat4f(
            shaderInterface.combinedTransform,
            projection * view * model
        );
        return;
    }

    program.UniformMat4f(shaderInterface.model, model);
    program.UniformMat4f(shaderInterface.view, view);
    program.UniformMat4f(shaderInterface.projection, projection);
}
}  // namespace wisteria
