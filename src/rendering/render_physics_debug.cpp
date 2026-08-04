#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

namespace wisteria
{
namespace
{
struct PhysicsDebugVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
};
}

void Renderer::EnsurePhysicsDebugResources()
{
    if (this->physicsDebugProgram == nullptr)
    {
        auto nextShader = std::make_unique<Shader>(
            wisteria::assets::Shader("physics_debug.vert"),
            wisteria::assets::Shader("physics_debug.frag")
        );
        auto nextProgram = std::make_unique<Program>(
            nextShader->GetShaderList()
        );
        this->physicsDebugShader = std::move(nextShader);
        this->physicsDebugProgram = std::move(nextProgram);
    }
    if (this->physicsDebugVao == 0)
        glGenVertexArrays(1, &this->physicsDebugVao);
    if (this->physicsDebugBuffer == 0)
        glGenBuffers(1, &this->physicsDebugBuffer);
    if (this->physicsDebugVao == 0 || this->physicsDebugBuffer == 0)
        throw std::runtime_error("Cannot create physics debug draw resources");

    glBindVertexArray(this->physicsDebugVao);
    glBindBuffer(GL_ARRAY_BUFFER, this->physicsDebugBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PhysicsDebugVertex),
        reinterpret_cast<const void*>(offsetof(PhysicsDebugVertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PhysicsDebugVertex),
        reinterpret_cast<const void*>(offsetof(PhysicsDebugVertex, color))
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::DrawPhysicsDebug(
    const Scene& scene,
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    std::vector<PhysicsDebugLine> lines;
    if (scene.Physics().DebugDrawEnabled())
    {
        const std::span<const PhysicsDebugLine> worldLines =
            scene.Physics().DebugLines();
        lines.insert(lines.end(), worldLines.begin(), worldLines.end());
    }
    for (const std::unique_ptr<Entity>& entity : scene.Entities())
        entity->AppendPhysicsDebugLines(lines);
    if (lines.empty())
        return;

    this->EnsurePhysicsDebugResources();
    std::vector<PhysicsDebugVertex> vertices;
    vertices.reserve(lines.size() * 2U);
    for (const PhysicsDebugLine& line : lines)
    {
        vertices.push_back(PhysicsDebugVertex{line.from, line.color});
        vertices.push_back(PhysicsDebugVertex{line.to, line.color});
    }
    const std::size_t bytes = vertices.size() * sizeof(PhysicsDebugVertex);
    glBindBuffer(GL_ARRAY_BUFFER, this->physicsDebugBuffer);
    if (bytes > this->physicsDebugCapacityBytes)
    {
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(bytes),
            vertices.data(),
            GL_DYNAMIC_DRAW
        );
        this->physicsDebugCapacityBytes = bytes;
    }
    else
    {
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(bytes),
            vertices.data()
        );
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    this->physicsDebugProgram->Use();
    this->physicsDebugProgram->UniformMat4f("view", view);
    this->physicsDebugProgram->UniformMat4f("projection", projection);
    glBindVertexArray(this->physicsDebugVao);
    glDrawArrays(
        GL_LINES,
        0,
        static_cast<GLsizei>(vertices.size())
    );
    glBindVertexArray(0);
    this->physicsDebugProgram->unUse();
    glDepthMask(GL_TRUE);
}
}  // namespace wisteria
