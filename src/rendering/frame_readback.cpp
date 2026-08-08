#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/frame_readback.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace wisteria
{
namespace
{
struct ReadbackState
{
    GLint readFramebuffer = 0;
    GLint readBuffer = GL_BACK;
    GLint packAlignment = 4;
    GLint pixelPackBuffer = 0;
    GLint packRowLength = 0;
    GLint packSkipPixels = 0;
    GLint packSkipRows = 0;
};

ReadbackState CaptureReadbackState()
{
    ReadbackState state;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &state.readBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state.packAlignment);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state.pixelPackBuffer);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &state.packRowLength);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &state.packSkipPixels);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &state.packSkipRows);
    return state;
}

void RestoreReadbackState(const ReadbackState& state)
{
    // Framebuffer first: glReadBuffer(GL_BACK) is only valid while the
    // default framebuffer is the read framebuffer; setting it while our
    // target FBO is still bound would be GL_INVALID_OPERATION.
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(state.readFramebuffer)
    );
    glReadBuffer(static_cast<GLenum>(state.readBuffer));
    glPixelStorei(GL_PACK_SKIP_ROWS, state.packSkipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, state.packSkipPixels);
    glPixelStorei(GL_PACK_ROW_LENGTH, state.packRowLength);
    glPixelStorei(GL_PACK_ALIGNMENT, state.packAlignment);
    glBindBuffer(
        GL_PIXEL_PACK_BUFFER,
        static_cast<GLuint>(state.pixelPackBuffer)
    );
}
}

Rgba8Frame ReadbackRgba8(const SceneFramebuffer& target)
{
    if (!target.IsValid())
    {
        throw std::logic_error(
            "Cannot read back an invalid scene framebuffer"
        );
    }
    const std::uint32_t width = static_cast<std::uint32_t>(target.Width());
    const std::uint32_t height = static_cast<std::uint32_t>(target.Height());
    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) *
        4U;
    if (byteCount > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error(
            "RGBA8 readback size overflows the host address space"
        );
    }

    const ReadbackState previousState = CaptureReadbackState();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);

    Rgba8Frame frame;
    frame.width = width;
    frame.height = height;
    frame.pixels.resize(static_cast<std::size_t>(byteCount));
    glReadPixels(
        0,
        0,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        frame.pixels.data()
    );
    RestoreReadbackState(previousState);

    // Canonical contract: top-left origin, top -> bottom rows. OpenGL
    // returns bottom-left, so flip the rows.
    const std::size_t rowBytes =
        static_cast<std::size_t>(width) * 4U;
    std::vector<std::uint8_t> flipped(frame.pixels.size());
    for (std::uint32_t row = 0U; row < height; ++row)
    {
        std::memcpy(
            flipped.data() + static_cast<std::size_t>(height - 1U - row) *
                rowBytes,
            frame.pixels.data() + static_cast<std::size_t>(row) * rowBytes,
            rowBytes
        );
    }
    frame.pixels = std::move(flipped);
    return frame;
}
}  // namespace wisteria
