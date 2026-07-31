#pragma once

class Timer{
public:
    Timer() = default;
    ~Timer() = default;

    inline float GetDeltaTime() const noexcept { return this->deltaTime; }
    double GetCurrentTime() const;
    void Start();
    void Now();
private:
    double lastTime = 0.0;
    double currentTime = 0.0;
    float deltaTime = 0.0f;
};
