#pragma once

#include "wisteria/rendering/present_surface.hpp"

namespace wisteria
{
class Window;

// OpenGL/GLFW presentation endpoint: the default framebuffer of one native
// window. This is an approved platform bridge (Gate B): it may call GL via
// Renderer::Present and owns SwapBuffers, but no GL type enters the neutral
// PresentSurface contract.
class GlfwPresentSurface final : public PresentSurface
{
public:
    explicit GlfwPresentSurface(Window& window);
    ~GlfwPresentSurface() override = default;

    int Width() const noexcept override;
    int Height() const noexcept override;
    void Present(
        Renderer& renderer,
        const SceneFramebuffer& scene
    ) override;
    void Swap() override;

private:
    Window& window;
};
}  // namespace wisteria
