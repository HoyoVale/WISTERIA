#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

// Owns only an OpenGL framebuffer object. Attachments are owned by the
// specialized render target that creates them.
namespace wisteria
{
class Framebuffer
{
public:
    Framebuffer() = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    void Create();
    void Bind(GLenum target = GL_FRAMEBUFFER) const;
    static void BindDefault(GLenum target = GL_FRAMEBUFFER);
    void AttachTexture2D(
        GLenum attachment,
        GLuint texture,
        GLint mipLevel = 0
    ) const;
    void AttachRenderbuffer(GLenum attachment, GLuint renderbuffer) const;
    void RequireComplete(GLenum target = GL_FRAMEBUFFER) const;
    void Release() noexcept;

    GLuint Id() const noexcept;
    bool IsCreated() const noexcept;

private:
    GLuint framebuffer = 0;
};

// The off-screen render target used for a complete scene frame. Its color
// texture can be sampled by post-processing shaders, while its depth buffer
// can be shared with auxiliary passes such as Weighted Blended OIT.
class SceneFramebuffer
{
public:
    SceneFramebuffer() = default;
    ~SceneFramebuffer();

    SceneFramebuffer(const SceneFramebuffer&) = delete;
    SceneFramebuffer& operator=(const SceneFramebuffer&) = delete;
    SceneFramebuffer(SceneFramebuffer&&) = delete;
    SceneFramebuffer& operator=(SceneFramebuffer&&) = delete;

    void Resize(int width, int height);
    void Bind() const;
    void Clear(const glm::vec4& color) const;
    void BindColorTexture(unsigned int textureUnit) const;
    void Release() noexcept;

    bool IsValid() const noexcept;
    int Width() const noexcept;
    int Height() const noexcept;
    GLuint Id() const noexcept;
    GLuint ColorTexture() const noexcept;
    GLuint DepthRenderbuffer() const noexcept;

private:
    Framebuffer framebuffer;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    int width = 0;
    int height = 0;
};
}  // namespace wisteria
