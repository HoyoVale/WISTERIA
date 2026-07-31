#include "pch.hpp"
#include "timer.hpp"
#include <algorithm>
#include <GLFW/glfw3.h>

double Timer::GetCurrentTime() const
{
    return glfwGetTime();
}

void Timer::Start()
{
    this->lastTime = GetCurrentTime();
}

void Timer::Now()
{
    this->currentTime = GetCurrentTime();
    this->deltaTime = static_cast<float>(this->currentTime - this->lastTime);
    this->lastTime = this->currentTime;
    this->deltaTime = std::clamp(this->deltaTime, 0.0f, 0.1f);
}
