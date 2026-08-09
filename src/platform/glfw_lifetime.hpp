#pragma once

#include <GLFW/glfw3.h>

#include <cstddef>
#include <mutex>

// R1.9 Final Fix: process-global GLFW lifetime shared by every owner
// (desktop Application windows and the headless GLFW-hidden provider). Two
// independent glfwInit/glfwTerminate refcounts could terminate GLFW while
// another owner still believes it is alive.
namespace wisteria::platform
{
inline std::mutex gGlfwLifetimeMutex{};
inline std::size_t gGlfwLifetimeCount = 0U;

inline bool AcquireGlfwLifetime()
{
    std::lock_guard<std::mutex> lock(gGlfwLifetimeMutex);
    if (gGlfwLifetimeCount == 0U && glfwInit() != GLFW_TRUE)
        return false;
    ++gGlfwLifetimeCount;
    return true;
}

inline void ReleaseGlfwLifetime() noexcept
{
    std::lock_guard<std::mutex> lock(gGlfwLifetimeMutex);
    if (gGlfwLifetimeCount == 0U)
        return;
    --gGlfwLifetimeCount;
    if (gGlfwLifetimeCount == 0U)
        glfwTerminate();
}
}  // namespace wisteria::platform
