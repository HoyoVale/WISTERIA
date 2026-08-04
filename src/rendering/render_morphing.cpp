#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

void Renderer::BeginMorphingFrame()
{
    if (this->morphingFrame == std::numeric_limits<std::uint64_t>::max())
    {
        this->ReleaseMorphingCache();
        this->morphingFrame = 1U;
        return;
    }
    ++this->morphingFrame;

    for (auto stateIterator = this->morphingCache.begin();
         stateIterator != this->morphingCache.end();)
    {
        auto& meshes = stateIterator->second;
        for (auto meshIterator = meshes.begin();
             meshIterator != meshes.end();)
        {
            MorphCacheEntry& entry = meshIterator->second;
            if (entry.lastUsedFrame + 1U < this->morphingFrame)
            {
                if (entry.buffer != 0)
                    glDeleteBuffers(1, &entry.buffer);
                meshIterator = meshes.erase(meshIterator);
            }
            else
            {
                ++meshIterator;
            }
        }
        if (meshes.empty())
            stateIterator = this->morphingCache.erase(stateIterator);
        else
            ++stateIterator;
    }
}

void Renderer::ReleaseMorphingCache() noexcept
{
    for (auto& [morphState, meshes] : this->morphingCache)
    {
        (void)morphState;
        for (auto& [mesh, entry] : meshes)
        {
            (void)mesh;
            if (entry.buffer != 0)
                glDeleteBuffers(1, &entry.buffer);
        }
    }
    this->morphingCache.clear();
}

void Renderer::UploadMorphing(
    VAO& vertexArray,
    const ShaderInterface& shaderInterface,
    const Mesh& mesh,
    const MorphState* morphState
)
{
    constexpr GLuint PositionLocation = 9U;
    constexpr GLuint FirstUvLocation = 10U;
    const auto disableAttributes = [&vertexArray]()
    {
        vertexArray.Bind();
        glDisableVertexAttribArray(PositionLocation);
        glVertexAttrib3f(PositionLocation, 0.0f, 0.0f, 0.0f);
        for (std::size_t channel = 0U;
             channel < MmdUvChannelCount;
             ++channel)
        {
            const GLuint location = FirstUvLocation +
                static_cast<GLuint>(channel);
            glDisableVertexAttribArray(location);
            glVertexAttrib4f(location, 0.0f, 0.0f, 0.0f, 0.0f);
        }
        vertexArray.unBind();
    };

    if (!shaderInterface.morphingSupported ||
        !mesh.HasMorphTargets() || morphState == nullptr)
    {
        disableAttributes();
        return;
    }

    MorphCacheEntry& entry = this->morphingCache[morphState][&mesh];
    entry.lastUsedFrame = this->morphingFrame;
    if (!entry.initialized || entry.revision != morphState->Revision())
    {
        entry.active = mesh.CalculateMorphDeltas(
            morphState->EffectiveWeights(),
            entry.offsets
        );
        if (entry.active)
        {
            if (entry.offsets.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<GLsizeiptr>::max()
                ) / sizeof(MorphVertexDelta))
            {
                throw std::overflow_error("Morph vertex buffer is too large");
            }
            if (entry.buffer == 0)
            {
                glGenBuffers(1, &entry.buffer);
                if (entry.buffer == 0)
                {
                    throw std::runtime_error(
                        "Cannot create dynamic morph vertex buffer"
                    );
                }
            }

            const std::size_t byteCount =
                entry.offsets.size() * sizeof(MorphVertexDelta);
            glBindBuffer(GL_ARRAY_BUFFER, entry.buffer);
            if (byteCount > entry.capacityBytes)
            {
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(byteCount),
                    entry.offsets.data(),
                    GL_DYNAMIC_DRAW
                );
                entry.capacityBytes = byteCount;
            }
            else
            {
                glBufferSubData(
                    GL_ARRAY_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(byteCount),
                    entry.offsets.data()
                );
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        entry.revision = morphState->Revision();
        entry.initialized = true;
    }

    if (!entry.active)
    {
        disableAttributes();
        return;
    }

    vertexArray.Bind();
    glBindBuffer(GL_ARRAY_BUFFER, entry.buffer);
    glEnableVertexAttribArray(PositionLocation);
    glVertexAttribPointer(
        PositionLocation,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(MorphVertexDelta)),
        reinterpret_cast<const void*>(offsetof(MorphVertexDelta, position))
    );
    for (std::size_t channel = 0U;
         channel < MmdUvChannelCount;
         ++channel)
    {
        const GLuint location = FirstUvLocation +
            static_cast<GLuint>(channel);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(MorphVertexDelta)),
            reinterpret_cast<const void*>(
                offsetof(MorphVertexDelta, uv) +
                channel * sizeof(glm::vec4)
            )
        );
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertexArray.unBind();
}
