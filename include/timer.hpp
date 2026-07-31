#pragma once

class Timer{
public:
    Timer();
    ~Timer();

    inline float GetDeltaTime() const { return  this->deltaTime; };
    double GetCurrentTime();
    void Start();
    void Now();
private:
    double lastTime = 0.0;
    double currentTime = 0.0;
    float deltaTime = 0.0f;
};