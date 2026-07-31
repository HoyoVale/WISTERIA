#include "pch.hpp"
#include "timer.hpp"
#include <GLFW/glfw3.h>

Timer::Timer()
{
}

Timer::~Timer()
{

}

double Timer::GetCurrentTime()
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
    this->deltaTime = glm::min(this->deltaTime, 0.1f);
}