#include "wisteria/common/pch.hpp"

#include "wisteria/platform/glfw_present_surface.hpp"
#include "wisteria/platform/window.hpp"

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
}  // namespace wisteria
