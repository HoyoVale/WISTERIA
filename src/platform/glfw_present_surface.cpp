#include "wisteria/common/pch.hpp"

#include "wisteria/platform/glfw_present_surface.hpp"
#include "wisteria/platform/window.hpp"
#include "wisteria/rendering/renderer.hpp"

namespace wisteria
{
GlfwPresentSurface::GlfwPresentSurface(Window& nextWindow)
    : window(nextWindow)
{
}

int GlfwPresentSurface::Width() const noexcept
{
    return this->window.GetFramebufferSize().width;
}

int GlfwPresentSurface::Height() const noexcept
{
    return this->window.GetFramebufferSize().height;
}

void GlfwPresentSurface::Present(
    Renderer& renderer,
    const SceneFramebuffer& scene
)
{
    renderer.Present(scene, this->Width(), this->Height());
}

void GlfwPresentSurface::Swap()
{
    this->window.SwapBuffers();
}
}  // namespace wisteria
