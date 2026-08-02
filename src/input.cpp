#include "pch.hpp"
#include "input.hpp"
#include <GLFW/glfw3.h>
#include <optional>
#include <stdexcept>

namespace
{
Input* GetInput(GLFWwindow* window) noexcept
{
    return static_cast<Input*>(glfwGetWindowUserPointer(window));
}

std::optional<InputKey> ToInputKey(int key) noexcept
{
    switch (key)
    {
    case GLFW_KEY_W: return InputKey::W;
    case GLFW_KEY_A: return InputKey::A;
    case GLFW_KEY_S: return InputKey::S;
    case GLFW_KEY_D: return InputKey::D;
    case GLFW_KEY_Q: return InputKey::Q;
    case GLFW_KEY_E: return InputKey::E;
    case GLFW_KEY_LEFT_SHIFT: return InputKey::LeftShift;
    case GLFW_KEY_ESCAPE: return InputKey::Escape;
    case GLFW_KEY_R: return InputKey::R;
    case GLFW_KEY_P: return InputKey::P;
    case GLFW_KEY_B: return InputKey::B;
    case GLFW_KEY_L: return InputKey::L;
    case GLFW_KEY_F3: return InputKey::F3;
    case GLFW_KEY_SPACE: return InputKey::Space;
    case GLFW_KEY_LEFT: return InputKey::Left;
    case GLFW_KEY_RIGHT: return InputKey::Right;
    default: return std::nullopt;
    }
}

std::optional<InputMouseButton> ToInputMouseButton(int button) noexcept
{
    switch (button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return InputMouseButton::Left;
    case GLFW_MOUSE_BUTTON_RIGHT: return InputMouseButton::Right;
    case GLFW_MOUSE_BUTTON_MIDDLE: return InputMouseButton::Middle;
    default: return std::nullopt;
    }
}
}

Input::~Input()
{
    this->Detach();
}

void Input::Attach(GLFWwindow& window)
{
    if (this->window == &window)
        return;
    if (this->window != nullptr)
        throw std::logic_error("Input is already attached to another window");
    if (glfwGetWindowUserPointer(&window) != nullptr)
        throw std::logic_error("GLFW window user pointer is already in use");

    this->window = &window;
    glfwSetWindowUserPointer(this->window, this);
    glfwSetKeyCallback(this->window, Input::KeyCallback);
    glfwSetMouseButtonCallback(this->window, Input::MouseButtonCallback);
    glfwSetCursorPosCallback(this->window, Input::CursorPositionCallback);
    glfwSetScrollCallback(this->window, Input::ScrollCallback);
    this->SetCursorCaptured(this->cursorCaptured);
}

void Input::Detach() noexcept
{
    if (this->window == nullptr)
        return;

    if (glfwGetWindowUserPointer(this->window) == this)
    {
        glfwSetKeyCallback(this->window, nullptr);
        glfwSetMouseButtonCallback(this->window, nullptr);
        glfwSetCursorPosCallback(this->window, nullptr);
        glfwSetScrollCallback(this->window, nullptr);
        glfwSetWindowUserPointer(this->window, nullptr);
    }

    this->window = nullptr;
    this->hasPreviousCursorPosition = false;
}

bool Input::IsAttached() const noexcept
{
    return this->window != nullptr;
}

void Input::BeginFrame() noexcept
{
    this->keysPressed.fill(false);
    this->keysReleased.fill(false);
    this->mouseButtonsPressed.fill(false);
    this->mouseButtonsReleased.fill(false);
    this->cursorDelta = {};
    this->scrollDeltaY = 0.0;
}

bool Input::IsKeyDown(InputKey key) const noexcept
{
    return this->keysDown[Input::Index(key)];
}

bool Input::WasKeyPressed(InputKey key) const noexcept
{
    return this->keysPressed[Input::Index(key)];
}

bool Input::WasKeyReleased(InputKey key) const noexcept
{
    return this->keysReleased[Input::Index(key)];
}

bool Input::IsMouseButtonDown(InputMouseButton button) const noexcept
{
    return this->mouseButtonsDown[Input::Index(button)];
}

bool Input::WasMouseButtonPressed(InputMouseButton button) const noexcept
{
    return this->mouseButtonsPressed[Input::Index(button)];
}

bool Input::WasMouseButtonReleased(InputMouseButton button) const noexcept
{
    return this->mouseButtonsReleased[Input::Index(button)];
}

MouseDelta Input::CursorDelta() const noexcept
{
    return this->cursorDelta;
}

double Input::ScrollDeltaY() const noexcept
{
    return this->scrollDeltaY;
}

void Input::SetCursorCaptured(bool captured)
{
    this->cursorCaptured = captured;
    this->hasPreviousCursorPosition = false;
    this->cursorDelta = {};

    if (this->window == nullptr)
        return;

    glfwSetInputMode(
        this->window,
        GLFW_CURSOR,
        captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
    );

    if (glfwRawMouseMotionSupported())
    {
        glfwSetInputMode(
            this->window,
            GLFW_RAW_MOUSE_MOTION,
            captured ? GLFW_TRUE : GLFW_FALSE
        );
    }
}

bool Input::IsCursorCaptured() const noexcept
{
    return this->cursorCaptured;
}

void Input::HandleKey(InputKey key, bool down) noexcept
{
    const std::size_t index = Input::Index(key);
    if (down == this->keysDown[index])
        return;

    this->keysDown[index] = down;
    if (down)
        this->keysPressed[index] = true;
    else
        this->keysReleased[index] = true;
}

void Input::HandleMouseButton(InputMouseButton button, bool down) noexcept
{
    const std::size_t index = Input::Index(button);
    if (down == this->mouseButtonsDown[index])
        return;

    this->mouseButtonsDown[index] = down;
    if (down)
        this->mouseButtonsPressed[index] = true;
    else
        this->mouseButtonsReleased[index] = true;
}

void Input::HandleCursorPosition(double x, double y) noexcept
{
    if (!this->hasPreviousCursorPosition)
    {
        this->previousCursorX = x;
        this->previousCursorY = y;
        this->hasPreviousCursorPosition = true;
        return;
    }

    this->cursorDelta.x += x - this->previousCursorX;
    this->cursorDelta.y += y - this->previousCursorY;
    this->previousCursorX = x;
    this->previousCursorY = y;
}

void Input::HandleScroll(double yOffset) noexcept
{
    this->scrollDeltaY += yOffset;
}

void Input::KeyCallback(
    GLFWwindow* window,
    int key,
    int,
    int action,
    int
)
{
    Input* input = GetInput(window);
    const std::optional<InputKey> mappedKey = ToInputKey(key);
    if (input == nullptr || !mappedKey.has_value())
        return;

    if (action == GLFW_PRESS)
        input->HandleKey(*mappedKey, true);
    else if (action == GLFW_RELEASE)
        input->HandleKey(*mappedKey, false);
}

void Input::MouseButtonCallback(
    GLFWwindow* window,
    int button,
    int action,
    int
)
{
    Input* input = GetInput(window);
    const std::optional<InputMouseButton> mappedButton =
        ToInputMouseButton(button);
    if (input == nullptr || !mappedButton.has_value())
        return;

    if (action == GLFW_PRESS)
        input->HandleMouseButton(*mappedButton, true);
    else if (action == GLFW_RELEASE)
        input->HandleMouseButton(*mappedButton, false);
}

void Input::CursorPositionCallback(GLFWwindow* window, double x, double y)
{
    Input* input = GetInput(window);
    if (input != nullptr)
        input->HandleCursorPosition(x, y);
}

void Input::ScrollCallback(GLFWwindow* window, double, double yOffset)
{
    Input* input = GetInput(window);
    if (input != nullptr)
        input->HandleScroll(yOffset);
}

std::size_t Input::Index(InputKey key) noexcept
{
    return static_cast<std::size_t>(key);
}

std::size_t Input::Index(InputMouseButton button) noexcept
{
    return static_cast<std::size_t>(button);
}
