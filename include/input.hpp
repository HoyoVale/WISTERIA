#pragma once

#include <array>
#include <cstddef>

struct GLFWwindow;

enum class InputKey : std::size_t
{
    W,
    A,
    S,
    D,
    Q,
    E,
    LeftShift,
    Escape,
    R,
    Count
};

enum class InputMouseButton : std::size_t
{
    Left,
    Right,
    Middle,
    Count
};

struct MouseDelta
{
    double x = 0.0;
    double y = 0.0;
};

// Stores frame-local input transitions independently from gameplay code.
// BeginFrame() must be called immediately before glfwPollEvents().
class Input
{
public:
    Input() = default;
    ~Input();

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;

    void Attach(GLFWwindow& window);
    void Detach() noexcept;
    bool IsAttached() const noexcept;

    void BeginFrame() noexcept;

    bool IsKeyDown(InputKey key) const noexcept;
    bool WasKeyPressed(InputKey key) const noexcept;
    bool WasKeyReleased(InputKey key) const noexcept;

    bool IsMouseButtonDown(InputMouseButton button) const noexcept;
    bool WasMouseButtonPressed(InputMouseButton button) const noexcept;
    bool WasMouseButtonReleased(InputMouseButton button) const noexcept;

    MouseDelta CursorDelta() const noexcept;
    double ScrollDeltaY() const noexcept;

    void SetCursorCaptured(bool captured);
    bool IsCursorCaptured() const noexcept;

    // Platform-adapter entry points. GLFW callbacks use these methods; keeping
    // them public also allows deterministic input tests without a real window.
    void HandleKey(InputKey key, bool down) noexcept;
    void HandleMouseButton(InputMouseButton button, bool down) noexcept;
    void HandleCursorPosition(double x, double y) noexcept;
    void HandleScroll(double yOffset) noexcept;

private:
    static constexpr std::size_t KeyCount =
        static_cast<std::size_t>(InputKey::Count);
    static constexpr std::size_t MouseButtonCount =
        static_cast<std::size_t>(InputMouseButton::Count);

    static void KeyCallback(
        GLFWwindow* window,
        int key,
        int scanCode,
        int action,
        int modifiers
    );
    static void MouseButtonCallback(
        GLFWwindow* window,
        int button,
        int action,
        int modifiers
    );
    static void CursorPositionCallback(GLFWwindow* window, double x, double y);
    static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    static std::size_t Index(InputKey key) noexcept;
    static std::size_t Index(InputMouseButton button) noexcept;

private:
    GLFWwindow* window = nullptr;
    std::array<bool, KeyCount> keysDown{};
    std::array<bool, KeyCount> keysPressed{};
    std::array<bool, KeyCount> keysReleased{};
    std::array<bool, MouseButtonCount> mouseButtonsDown{};
    std::array<bool, MouseButtonCount> mouseButtonsPressed{};
    std::array<bool, MouseButtonCount> mouseButtonsReleased{};
    MouseDelta cursorDelta;
    double scrollDeltaY = 0.0;
    double previousCursorX = 0.0;
    double previousCursorY = 0.0;
    bool hasPreviousCursorPosition = false;
    bool cursorCaptured = false;
};
