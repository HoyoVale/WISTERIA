#include "wisteria/common/pch.hpp"
#include "wisteria/core/timer.hpp"
#include <algorithm>
#include <chrono>

namespace wisteria
{
double Timer::GetCurrentTime() const
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
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
}  // namespace wisteria
