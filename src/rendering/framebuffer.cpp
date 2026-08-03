#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/framebuffer.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace
{
class ScopedFramebufferSetupState
{
public:
    ScopedFramebufferSetupState()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &this->drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &this->readFramebuffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &this->texture2D);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &this->renderbuffer);
    }

    ~ScopedFramebufferSetupState()
    {
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>(this->drawFramebuffer)
        );
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(this->readFramebuffer)
        );
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(this->texture2D));
        glBindRenderbuffer(
            GL_RENDERBUFFER,
            static_cast<GLuint>(this->renderbuffer)
        );
    }

private:
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint texture2D = 0;
    GLint renderbuffer = 0;
};
}

Framebuffer::~Framebuffer()
{
    this->Release();
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : framebuffer(std::exchange(other.framebuffer, 0))
{
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept
{
    if (this == &other)
        return *this;

    this->Release();
    this->framebuffer = std::exchange(other.framebuffer, 0);
    return *this;
}

void Framebuffer::Create()
{
    if (this->framebuffer != 0)
        return;

    glGenFramebuffers(1, &this->framebuffer);
    if (this->framebuffer == 0)
        throw std::runtime_error("Cannot create framebuffer");
}

void Framebuffer::Bind(GLenum target) const
{
    if (this->framebuffer == 0)
        throw std::logic_error("Cannot bind an uninitialized framebuffer");
    glBindFramebuffer(target, this->framebuffer);
}

void Framebuffer::BindDefault(GLenum target)
{
    glBindFramebuffer(target, 0);
}

void Framebuffer::AttachTexture2D(
    GLenum attachment,
    GLuint texture,
    GLint mipLevel
) const
{
    if (this->framebuffer == 0 || texture == 0)
        throw std::logic_error("Framebuffer texture attachment is invalid");

    this->Bind();
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        attachment,
        GL_TEXTURE_2D,
        texture,
        mipLevel
    );
}

void Framebuffer::AttachRenderbuffer(
    GLenum attachment,
    GLuint renderbuffer
) const
{
    if (this->framebuffer == 0 || renderbuffer == 0)
        throw std::logic_error("Framebuffer renderbuffer attachment is invalid");

    this->Bind();
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        attachment,
        GL_RENDERBUFFER,
        renderbuffer
    );
}

void Framebuffer::RequireComplete(GLenum target) const
{
    if (this->framebuffer == 0)
        throw std::logic_error("Cannot validate an uninitialized framebuffer");
    this->Bind(target);
    if (glCheckFramebufferStatus(target) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Framebuffer is incomplete");
}

void Framebuffer::Release() noexcept
{
    if (this->framebuffer != 0)
        glDeleteFramebuffers(1, &this->framebuffer);
    this->framebuffer = 0;
}

GLuint Framebuffer::Id() const noexcept
{
    return this->framebuffer;
}

bool Framebuffer::IsCreated() const noexcept
{
    return this->framebuffer != 0;
}

SceneFramebuffer::~SceneFramebuffer()
{
    this->Release();
}

void SceneFramebuffer::Resize(int width, int height)
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Scene framebuffer dimensions must be positive");
    if (this->width == width && this->height == height && this->IsValid())
        return;

    GLint maximumTextureSize = 0;
    GLint maximumRenderbufferSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximumRenderbufferSize);
    const int maximumSize = std::min(maximumTextureSize, maximumRenderbufferSize);
    if (maximumSize <= 0 || width > maximumSize || height > maximumSize)
        throw std::runtime_error("Scene framebuffer dimensions exceed OpenGL limits");

    ScopedFramebufferSetupState previousState;
    this->framebuffer.Create();
    if (this->colorTexture == 0)
        glGenTextures(1, &this->colorTexture);
    if (this->depthRenderbuffer == 0)
        glGenRenderbuffers(1, &this->depthRenderbuffer);
    if (this->colorTexture == 0 || this->depthRenderbuffer == 0)
        throw std::runtime_error("Cannot create scene framebuffer attachments");

    this->framebuffer.Bind();
    glBindTexture(GL_TEXTURE_2D, this->colorTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        width,
        height,
        0,
        GL_RGBA,
        GL_HALF_FLOAT,
        nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    this->framebuffer.AttachTexture2D(
        GL_COLOR_ATTACHMENT0,
        this->colorTexture
    );

    glBindRenderbuffer(GL_RENDERBUFFER, this->depthRenderbuffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT24,
        width,
        height
    );
    this->framebuffer.AttachRenderbuffer(
        GL_DEPTH_ATTACHMENT,
        this->depthRenderbuffer
    );

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    this->framebuffer.RequireComplete();
    this->width = width;
    this->height = height;
}

void SceneFramebuffer::Bind() const
{
    if (!this->IsValid())
        throw std::logic_error("Cannot bind an uninitialized scene framebuffer");
    this->framebuffer.Bind();
    glViewport(0, 0, this->width, this->height);
}

void SceneFramebuffer::Clear(const glm::vec4& color) const
{
    this->Bind();
    glClearColor(color.r, color.g, color.b, color.a);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SceneFramebuffer::BindColorTexture(unsigned int textureUnit) const
{
    if (!this->IsValid())
        throw std::logic_error("Scene framebuffer has no color texture");
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, this->colorTexture);
}

void SceneFramebuffer::Release() noexcept
{
    if (this->depthRenderbuffer != 0)
        glDeleteRenderbuffers(1, &this->depthRenderbuffer);
    if (this->colorTexture != 0)
        glDeleteTextures(1, &this->colorTexture);
    this->framebuffer.Release();

    this->depthRenderbuffer = 0;
    this->colorTexture = 0;
    this->width = 0;
    this->height = 0;
}

bool SceneFramebuffer::IsValid() const noexcept
{
    return this->framebuffer.IsCreated() &&
        this->colorTexture != 0 &&
        this->depthRenderbuffer != 0 &&
        this->width > 0 &&
        this->height > 0;
}

int SceneFramebuffer::Width() const noexcept
{
    return this->width;
}

int SceneFramebuffer::Height() const noexcept
{
    return this->height;
}

GLuint SceneFramebuffer::Id() const noexcept
{
    return this->framebuffer.Id();
}

GLuint SceneFramebuffer::ColorTexture() const noexcept
{
    return this->colorTexture;
}

GLuint SceneFramebuffer::DepthRenderbuffer() const noexcept
{
    return this->depthRenderbuffer;
}
