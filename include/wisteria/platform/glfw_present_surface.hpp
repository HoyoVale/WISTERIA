#pragma once

#include "wisteria/rendering/present_surface.hpp"

namespace wisteria
{
class Window;

// OpenGL/GLFW platform presentation endpoint: the default framebuffer of
// one native window. It exposes only dimensions (PresentSurface contract);
// the backend-created PresentationTarget owns Present/Swap. This is an
// approved platform bridge (Gate B).
class GlfwPresentSurface final : public PresentSurface
{
public:
    explicit GlfwPresentSurface(Window& window);
    ~GlfwPresentSurface() override = default;

    int Width() const noexcept override;
    int Height() const noexcept override;

private:
    Window& window;
};
}  // namespace wisteria
